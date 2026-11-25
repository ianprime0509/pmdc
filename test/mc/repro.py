#!/usr/bin/env -S uv run
"""Creates/updates MC expected output files using Unicorn.

Runs the original MC.EXE (or the MCH.EXE/EFC.EXE named by the case's
`; Command:` header) under an emulated DOS environment. Only the DOS services
these commands use are implemented: console output, handle-based file I/O, and
process termination.

Usage: uv run repro.py ../../original/mc48s cases cases/INPUT.MML
"""

import io
import struct
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import BinaryIO

from unicorn import (
    UC_ARCH_X86,
    UC_HOOK_INTR,
    UC_MODE_16,
    Uc,
    UcError,
)
from unicorn.x86_const import (
    UC_X86_REG_AX,
    UC_X86_REG_BX,
    UC_X86_REG_CS,
    UC_X86_REG_CX,
    UC_X86_REG_DL,
    UC_X86_REG_DS,
    UC_X86_REG_DX,
    UC_X86_REG_EFLAGS,
    UC_X86_REG_ES,
    UC_X86_REG_IP,
    UC_X86_REG_SP,
    UC_X86_REG_SS,
)

MEM_SIZE = 0x110000
PSP_SEG = 0x1000
LOAD_SEG = PSP_SEG + 0x10
ENV_SEG = 0x0F00
CF = 1 << 0


class EmulationError(Exception):
    pass


class DosMachine:
    """A minimal real-mode DOS environment for one EXE."""

    def __init__(self, exe: bytes, cwd: Path, console: BinaryIO):
        self.cwd = cwd
        self.console = console
        self.handles: dict[int, tuple[BinaryIO, bool, bool]] = {}
        self.next_handle = 5
        self.exit_code: int | None = None
        self.mu = Uc(UC_ARCH_X86, UC_MODE_16)
        self.mu.mem_map(0, MEM_SIZE)
        self.mu.hook_add(UC_HOOK_INTR, self._on_interrupt)
        self._load_exe(exe)

    def _load_exe(self, exe: bytes) -> None:
        fields = struct.unpack_from("<14H", exe, 0)
        (
            magic,
            cblp,
            cp,
            reloc_count,
            header_paras,
            min_alloc,
            _max_alloc,
            ss,
            sp,
            _csum,
            ip,
            cs,
            reloc_offset,
            _ovno,
        ) = fields
        if magic != 0x5A4D:
            raise EmulationError("not an MZ executable")

        header_len = header_paras * 16
        image_len = (cp - 1) * 512 + (cblp or 512) - header_len
        image = bytearray(exe[header_len : header_len + image_len])

        for i in range(reloc_count):
            offset, segment = struct.unpack_from(
                "<HH", exe, reloc_offset + i * 4
            )
            at = segment * 16 + offset
            value = struct.unpack_from("<H", image, at)[0]
            struct.pack_into("<H", image, at, (value + LOAD_SEG) & 0xFFFF)

        self.mu.mem_write(LOAD_SEG * 16, bytes(image))

        self.mu.reg_write(UC_X86_REG_CS, (LOAD_SEG + cs) & 0xFFFF)
        self.mu.reg_write(UC_X86_REG_IP, ip)
        self.mu.reg_write(UC_X86_REG_SS, (LOAD_SEG + ss) & 0xFFFF)
        self.mu.reg_write(UC_X86_REG_SP, sp)
        self.start = ((LOAD_SEG + cs) << 4) + ip

        image_paras = (image_len + 15) // 16
        top = LOAD_SEG + image_paras + min_alloc
        psp = PSP_SEG * 16
        self.mu.mem_write(psp, b"\xcd\x20")
        self.mu.mem_write(psp + 0x02, struct.pack("<H", top))
        self.mu.mem_write(psp + 0x2C, struct.pack("<H", ENV_SEG))
        # An empty DOS environment: two terminating NULs.
        self.mu.mem_write(ENV_SEG * 16, b"\x00\x00")

        self.mu.reg_write(UC_X86_REG_DS, PSP_SEG)
        self.mu.reg_write(UC_X86_REG_ES, PSP_SEG)

    def run(self, args: str, timeout: float = 60.0) -> int:
        tail = (" " + args).encode("cp932")[:255]
        psp = PSP_SEG * 16
        self.mu.mem_write(psp + 0x80, bytes([len(tail)]) + tail + b"\r")

        try:
            self.mu.emu_start(self.start, 0x100000, timeout=int(timeout * 1e6))
        except UcError as err:
            ip = self.mu.reg_read(UC_X86_REG_IP)
            cs = self.mu.reg_read(UC_X86_REG_CS)
            raise EmulationError(f"{err} at {cs:04x}:{ip:04x}") from err

        if self.exit_code is None:
            raise EmulationError("emulation stopped without exiting")
        return self.exit_code

    # -- DOS API ----------------------------------------------------------

    def _on_interrupt(self, mu, intno, _data):
        if intno == 0x20:
            self._exit(0)
        elif intno == 0x21:
            self._int21()
        else:
            raise EmulationError(f"unexpected interrupt {intno:#04x}")
        return True

    def _int21(self) -> None:
        ah = (self.mu.reg_read(UC_X86_REG_AX) >> 8) & 0xFF
        if ah == 0x09:
            self._print_string()
        elif ah == 0x02:
            self._print_char()
        elif ah == 0x3C:
            self._create_file()
        elif ah == 0x3D:
            self._open_file()
        elif ah == 0x3E:
            self._close_file()
        elif ah == 0x3F:
            self._read_file()
        elif ah == 0x40:
            self._write_file()
        elif ah == 0x4C:
            self._exit(self.mu.reg_read(UC_X86_REG_AX) & 0xFF)
        else:
            raise EmulationError(f"unimplemented DOS call AH={ah:#04x}")

    def _print_string(self) -> None:
        addr = self._address(UC_X86_REG_DS, UC_X86_REG_DX)
        for offset in range(0x10000):
            byte = self.mu.mem_read(addr + offset, 1)[0]
            if byte == ord("$"):
                return
            self.console.write(bytes([byte]))

    def _print_char(self) -> None:
        self.console.write(bytes([self.mu.reg_read(UC_X86_REG_DL) & 0xFF]))

    def _create_file(self) -> None:
        name = self._filename()
        path = self._resolve(name, create=True)
        if path is None:
            self._fail(3)
            return
        try:
            handle = open(path, "wb")
        except OSError:
            self._fail(3)
            return
        self._new_handle(handle, readable=False, writable=True)

    def _open_file(self) -> None:
        name = self._filename()
        mode = self.mu.reg_read(UC_X86_REG_AX) & 0xFF
        access = mode & 0x03
        path = self._resolve(name, create=False)
        if path is None:
            self._fail(2)
            return
        try:
            if access == 0:
                handle = open(path, "rb")
            else:
                handle = open(path, "r+b")
        except FileNotFoundError:
            self._fail(2)
            return
        except OSError:
            self._fail(3)
            return
        self._new_handle(
            handle, readable=access in (0, 2), writable=access in (1, 2)
        )

    def _close_file(self) -> None:
        bid = self.mu.reg_read(UC_X86_REG_BX) & 0xFFFF
        entry = self.handles.pop(bid, None)
        if entry is None:
            self._fail(6)
            return
        entry[0].close()
        self.mu.reg_write(UC_X86_REG_AX, 0)
        self._set_cf(False)

    def _read_file(self) -> None:
        bid = self.mu.reg_read(UC_X86_REG_BX) & 0xFFFF
        entry = self.handles.get(bid)
        if entry is None or not entry[1]:
            self._fail(6)
            return
        count = self.mu.reg_read(UC_X86_REG_CX) & 0xFFFF
        addr = self._address(UC_X86_REG_DS, UC_X86_REG_DX)
        try:
            data = entry[0].read(count)
        except OSError:
            self._fail(5)
            return
        self.mu.mem_write(addr, data)
        self.mu.reg_write(UC_X86_REG_AX, len(data))
        self._set_cf(False)

    def _write_file(self) -> None:
        bid = self.mu.reg_read(UC_X86_REG_BX) & 0xFFFF
        entry = self.handles.get(bid)
        if entry is None or not entry[2]:
            self._fail(6)
            return
        count = self.mu.reg_read(UC_X86_REG_CX) & 0xFFFF
        addr = self._address(UC_X86_REG_DS, UC_X86_REG_DX)
        try:
            written = entry[0].write(self.mu.mem_read(addr, count))
        except OSError:
            self._fail(5)
            return
        self.mu.reg_write(UC_X86_REG_AX, written)
        self._set_cf(False)

    def _exit(self, code: int) -> None:
        self.exit_code = code
        for handle, _, _ in self.handles.values():
            handle.close()
        self.handles.clear()
        self.mu.emu_stop()

    # -- Helpers ----------------------------------------------------------

    def _new_handle(
        self, handle: BinaryIO, readable: bool, writable: bool
    ) -> None:
        bid = self.next_handle
        self.next_handle += 1
        self.handles[bid] = (handle, readable, writable)
        self.mu.reg_write(UC_X86_REG_AX, bid)
        self._set_cf(False)

    def _fail(self, code: int) -> None:
        self.mu.reg_write(UC_X86_REG_AX, code)
        self._set_cf(True)

    def _set_cf(self, value: bool) -> None:
        flags = self.mu.reg_read(UC_X86_REG_EFLAGS)
        flags = flags | CF if value else flags & ~CF
        self.mu.reg_write(UC_X86_REG_EFLAGS, flags)

    def _address(self, seg_reg: int, off_reg: int) -> int:
        segment = self.mu.reg_read(seg_reg) & 0xFFFF
        offset = self.mu.reg_read(off_reg) & 0xFFFF
        return (segment << 4) + offset

    def _filename(self) -> str:
        addr = self._address(UC_X86_REG_DS, UC_X86_REG_DX)
        raw = bytearray()
        for offset in range(256):
            byte = self.mu.mem_read(addr + offset, 1)[0]
            if byte == 0:
                break
            raw.append(byte)
        return raw.decode("cp932", errors="surrogateescape")

    def _resolve(self, name: str, create: bool) -> Path | None:
        name = name.replace("\\", "/")
        if len(name) >= 2 and name[1] == ":":
            name = name[2:]
        parts = [part for part in name.split("/") if part not in ("", ".")]
        if any(part == ".." for part in parts) or not parts:
            return None
        path = self.cwd
        for part in parts[:-1]:
            found = self._find(path, part)
            if found is None or not found.is_dir():
                return None
            path = found
        last = self._find(path, parts[-1])
        if last is not None:
            return last
        return path / parts[-1] if create else None

    def _find(self, parent: Path, name: str) -> Path | None:
        exact = parent / name
        if exact.exists():
            return exact
        try:
            entries = list(parent.iterdir())
        except OSError:
            return None
        for entry in entries:
            if entry.name.lower() == name.lower():
                return entry
        return None


@dataclass
class Headers:
    """The harness headers that precede a test case's MML."""

    outputs: list[str] = field(default_factory=list)
    console: str | None = None
    extra_inputs: list[str] = field(default_factory=list)
    options: str | None = None
    command: str = "mc"


def split_list(text: str) -> list[str]:
    return [item.strip() for item in text.split(",")]


def parse_headers(mml: bytes) -> Headers:
    headers = Headers()
    for line in mml.split(b"\r\n"):
        if not line.startswith(b";"):
            break
        text = line.decode("cp932", errors="surrogateescape")
        if text.startswith("; Output: "):
            headers.outputs = split_list(text[len("; Output: ") :])
        elif text.startswith("; Console: "):
            headers.console = text[len("; Console: ") :]
        elif text.startswith("; Extra input: "):
            headers.extra_inputs = split_list(
                text[len("; Extra input: ") :]
            )
        elif text.startswith("; Options: "):
            headers.options = text[len("; Options: ") :]
        elif text.startswith("; Command: "):
            headers.command = text[len("; Command: ") :]
    return headers


def repro(mcdir: Path, casesdir: Path, inputfile: Path) -> None:
    mml = inputfile.read_bytes()
    headers = parse_headers(mml)
    exe = mcdir / f"{headers.command.upper()}.EXE"
    if not exe.is_file():
        raise SystemExit(f"{exe.name} not found in {mcdir}")
    stem = inputfile.stem
    console_name = headers.console or f"{stem}.OUT"
    command_line = headers.options or inputfile.name

    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        data = work / "data"
        data.mkdir()
        (data / inputfile.name).write_bytes(mml)
        for name in headers.extra_inputs:
            # Extra inputs keep only their file name, like the harness does.
            (data / Path(name).name).write_bytes((casesdir / name).read_bytes())

        console = io.BytesIO()
        machine = DosMachine(exe.read_bytes(), data, console)
        if (status := machine.run(command_line)) != 0:
            raise EmulationError(f"{exe.name} exited with status {status}")

        raw = console.getvalue().replace(b"\r", b"")
        try:
            text = raw.decode("cp932")
        except UnicodeDecodeError as err:
            raise EmulationError(f"cannot decode console output: {err}") from err

        for name in headers.outputs:
            (casesdir / name).write_bytes((data / name).read_bytes())
        (casesdir / console_name).write_bytes(text.encode("utf-8"))


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print(
            "usage: repro.py MC-DIR CASES-DIR INPUT.MML",
            file=sys.stderr,
        )
        return 1
    mcdir, casesdir, inputfile = (Path(arg) for arg in argv[1:])
    try:
        repro(mcdir, casesdir, inputfile)
    except EmulationError as err:
        print(f"{err}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))