#!/usr/bin/env -S uv run
"""Creates/updates PMD expected output files using Unicorn.

Runs one of the original PMD 4.8s COM files under an emulated DOS environment
and records the operations that make up the driver's observable output: the
console text and the ordered stream of port writes and timer interrupts.

The script to run is a .RUN file, the same one `trace.c` executes against the
translated driver. See README.md for the format.

Usage: uv run repro.py PMD-DIR CASES-DIR [--replay] NAME[.RUN]
"""

import io
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import BinaryIO

from unicorn import (
    UC_ARCH_X86,
    UC_HOOK_CODE,
    UC_HOOK_INSN,
    UC_HOOK_INTR,
    UC_MODE_16,
    Uc,
    UcError,
)
from unicorn.x86_const import (
    UC_X86_INS_IN,
    UC_X86_INS_OUT,
    UC_X86_REG_AX,
    UC_X86_REG_BX,
    UC_X86_REG_CS,
    UC_X86_REG_DS,
    UC_X86_REG_DX,
    UC_X86_REG_EFLAGS,
    UC_X86_REG_ES,
    UC_X86_REG_IP,
    UC_X86_REG_SI,
    UC_X86_REG_SP,
    UC_X86_REG_SS,
)

MEM_SIZE = 0x110000
PSP_SEG = 0x1000
LOAD_OFFSET = 0x100
ENV_SEG = 0x0F00
# The tail of the fake DOS structures: the list of lists the virus check walks.
DOS_DATA_SEG = 0x0E00
# The worker stack for INT 60h calls, and the stop address whose hook ends each
# emulated call or interrupt handler.
CALL_STACK_SEG = 0xE000
TRAP_SEG = 0xF000
TRAP_OFF = 0x1000
TRAP_ADDR = TRAP_SEG * 16 + TRAP_OFF

CF = 1 << 0
IF = 1 << 9

# Host ports the translated driver leaves to the host: the two PC-98 8259 PIC
# banks and the 8254 PIT, and the 88VA's port 32h, where int_init clears the
# OPN interrupt request. On the 88VA the PICs move to 184h-18Ah (PMD.ASM:265),
# so those four ports are host-owned there and the FM board sits at 044h
# instead. `trace.c` never emits them, so they are not recorded.
HOST_PORTS = {0x00, 0x02, 0x08, 0x0A, 0x32, 0x71, 0x77}
VA_HOST_PORTS = {0x184, 0x186, 0x188, 0x18A}


def is_host_port(port: int, board_kind: str) -> bool:
    return port in HOST_PORTS or (board_kind == "VA" and port in VA_HOST_PORTS)

# Register 0Eh bits 6-7 select the FM interrupt. The fake board reports 00h,
# which is IRQ 3 and INT 0Bh (vector_table in PMD.ASM).
IRQ_VECTOR = 0x0B

# Timer register reload counts in OPN master clocks: 72 for Timer A (12
# operator slots times the /6 prescaler) and 1152 for Timer B.
TIMER_A_STEP = 72
TIMER_B_STEP = 1152


class EmulationError(Exception):
    pass


@dataclass
class Script:
    driver: str = "pmd"
    board: str = "26K"
    chip: str = "YM2203"
    pmdopt: str | None = None
    reads: bool = False
    actions: list[tuple[str, str]] = field(default_factory=list)


def parse_script(path: Path) -> Script:
    script = Script()
    seen_action = False
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        if line.startswith(";"):
            if seen_action:
                continue
            header, sep, value = line[1:].partition(":")
            if not sep:
                continue
            key = header.strip().lower()
            value = value.strip()
            if key == "driver":
                script.driver = value
            elif key == "board":
                script.board = value
            elif key == "chip":
                script.chip = value
            elif key == "pmdopt":
                script.pmdopt = value
            elif key == "reads":
                script.reads = value.lower() == "on"
            else:
                raise EmulationError(f"{path}:{number}: unknown header {key!r}")
            continue
        seen_action = True
        verb, _, rest = line.strip().partition(" ")
        script.actions.append((verb, rest.strip()))
    return script


def hex8(value: int) -> str:
    return f"{value & 0xFF:02x}"


def hexval(value: int) -> str:
    return f"{value:x}"


def dos_name(name: str) -> str:
    """Formats a file name the way the virus check's FCB entry expects."""
    stem, _, suffix = name.partition(".")
    return f"{stem:<8.8}{suffix:<3.3}"


class Board:
    """The fake FM board: just enough of the OPN to satisfy PMD."""

    def __init__(self, kind: str, chip: str):
        if kind not in ("26K", "86B", "VA", "none"):
            raise EmulationError(f"unsupported board {kind!r}")
        if chip not in ("YM2203", "YM2608"):
            raise EmulationError(f"unsupported chip {chip!r}")
        if (kind, chip) in (("26K", "YM2608"), ("86B", "YM2203")):
            raise EmulationError(f"board {kind} is not a {chip} board")
        self.kind = kind
        if kind == "26K":
            self.base = 0x088
            self.opna = False
        elif kind == "86B":
            self.base = 0x188
            self.opna = True
        elif kind == "VA":
            # The 88VA ships with either chip: PMDVA drives a YM2608 and
            # PMDVA1 a YM2203 (which has no OPNA ID port behavior).
            self.base = 0x044
            self.opna = chip == "YM2608"
        else:
            self.base = None
            self.opna = False
        # The first and second chip's address and data ports. The PC-98
        # boards use word pairs (base, base+2, base+4, base+6); the 88VA
        # drives the YM2608 through four adjacent byte ports.
        if self.kind == "VA":
            self.fm1_addr, self.fm1_data = 0x044, 0x045
            self.fm2_addr, self.fm2_data = 0x046, 0x047
        elif self.base is not None:
            self.fm1_addr, self.fm1_data = self.base, self.base + 2
            self.fm2_addr, self.fm2_data = self.base + 4, self.base + 6
        else:
            self.fm1_addr = self.fm1_data = self.fm2_addr = self.fm2_data = None
        # Each chip's address register and the last data write. The 0AAh
        # probe reads the last data write back.
        self.addr1 = 0
        self.addr2 = 0
        self.latch: dict[int, int] = {}
        # Status flags: 1 = Timer A, 2 = Timer B.
        self.flags = 0
        self.timer_a = 0
        self.timer_b = 0
        self.control = 0
        self.timer_a_running = False
        self.timer_b_running = False
        self.timer_a_deadline = 0
        self.timer_b_deadline = 0
        # check_86b switches the OPNA off, probes, then switches it on. Its
        # second-chip status port reads FFh while off and not busy while on.
        self.opna_on = False

    def candidate_port(self, base: int, port: int) -> bool:
        if base == 0x044:
            return 0x044 <= port <= 0x047
        return base <= port < base + 8 and (port - base) % 2 == 0

    def pair(self, port: int) -> int | None:
        """The base of the candidate chip a port belongs to."""
        for base in (0x044, 0x088, 0x188, 0x288, 0x388):
            if self.candidate_port(base, port):
                return base
        return None

    def present(self, base: int | None) -> bool:
        return base is not None and base == self.base

    def timer_a_period(self) -> int:
        value = (self.timer_a & 0x3FF)
        return (1024 - value) * TIMER_A_STEP

    def timer_b_period(self) -> int:
        return (256 - (self.timer_b & 0xFF)) * TIMER_B_STEP

    def write_register(self, register: int, value: int, now: int) -> None:
        if register == 0x24:
            self.timer_a = (self.timer_a & 0x300) | value
        elif register == 0x25:
            self.timer_a = (self.timer_a & 0x0FF) | ((value & 0x03) << 8)
        elif register == 0x26:
            self.timer_b = value
        elif register == 0x27:
            self.control = value
            if value & 0x10:
                self.flags &= ~1
            if value & 0x20:
                self.flags &= ~2
            self.start_stop_a(now)
            self.start_stop_b(now)

    def start_stop_a(self, now: int) -> None:
        load = self.control & 0x01
        if load and not self.timer_a_running:
            self.timer_a_running = True
            self.timer_a_deadline = now + self.timer_a_period()
        elif not load and self.timer_a_running:
            self.timer_a_running = False

    def start_stop_b(self, now: int) -> None:
        load = self.control & 0x02
        if load and not self.timer_b_running:
            self.timer_b_running = True
            self.timer_b_deadline = now + self.timer_b_period()
        elif not load and self.timer_b_running:
            self.timer_b_running = False

    def next_deadline(self) -> int | None:
        deadlines = []
        if self.timer_a_running:
            deadlines.append(self.timer_a_deadline)
        if self.timer_b_running:
            deadlines.append(self.timer_b_deadline)
        return min(deadlines) if deadlines else None

    def advance(self, now: int) -> int:
        """Expires every timer due at `now` and returns the latched flags."""
        latched = 0
        if self.timer_a_running and self.timer_a_deadline == now:
            if self.control & 0x04:
                self.flags |= 1
                latched |= 1
            self.timer_a_deadline = now + self.timer_a_period()
        if self.timer_b_running and self.timer_b_deadline == now:
            if self.control & 0x08:
                self.flags |= 2
                latched |= 2
            self.timer_b_deadline = now + self.timer_b_period()
        return latched

    def inb(self, port: int) -> int:
        if is_host_port(port, self.kind):
            # The PIC mask register reads back all-masked, so PMD's intset
            # sees a fresh interrupt and skips its saved INT 5h handler.
            return 0xFF
        base = self.pair(port)
        if base is not None:
            if not self.present(base):
                return 0xFF
            if port == self.fm1_addr:
                return self.flags
            if port == self.fm1_data:
                if self.addr1 in (0x0E, 0x0F):
                    return 0x00
                # The 86 board's OPNA answers register FFh with 1, which is
                # what check_86b probes for; an absent chip echoes the probe.
                if self.opna and self.addr1 == 0xFF:
                    return 0x01
                return self.latch.get(port, 0xFF)
            # The second chip: FFh is the probe value check_86b looks for,
            # and the driver's rdychk needs BUSY clear once the OPNA is on.
            # The 88VA has no OPNA gate, and its ADPCM BRDY stays clear so
            # that adpcm_ram_check reports a YMF288 through its poll timeout.
            if port == self.fm2_addr:
                if self.kind == "VA":
                    return 0x00
                return 0x00 if self.opna_on else 0xFF
            return 0xFF
        if port == 0xA460:
            return 0x00 if self.opna else 0xFF
        if port == 0xA66E:
            return 0x00
        if port == 0xA468:
            return 0x00
        raise EmulationError(f"unmodelled port read {port:#06x}")

    def outb(self, port: int, value: int, now: int) -> None:
        base = self.pair(port)
        if base is not None:
            if port == self.fm1_addr:
                self.addr1 = value
            elif port == self.fm1_data:
                self.latch[port] = value
                if self.present(base):
                    self.write_register(self.addr1, value, now)
            elif port == self.fm2_addr:
                self.addr2 = value
            else:
                self.latch[port] = value
            return
        if port == 0xA460:
            self.opna_on = (value & 1) != 0
            return
        if port in (0x5F, 0xA66E, 0xA468):
            return
        raise EmulationError(f"unmodelled port write {port:#06x} = {value:#04x}")


class Machine:
    """The DOS environment, the resident image, and the virtual board."""

    def __init__(self, image: bytes, name: str, script: Script,
                 cwd: Path, console: BinaryIO):
        self.script = script
        self.cwd = cwd
        self.console = console
        self.exit_code: int | None = None
        self.now = 0
        self.record: io.StringIO = io.StringIO()
        # Instructions between PIT ticks in the WAIT.INC calibration, and the
        # total budget before `main` is declared hung.
        self.chunk = 100_000
        self.budget = 100_000_000
        # --replay: the golden's timer lines drive delivery instead of the
        # model's schedule, so a wrong timer write does not cascade.
        self.replay: list[tuple[int, int]] | None = None
        self.replay_pos = 0
        self.reads = script.reads
        self.board = Board(script.board, script.chip)
        self.mu = Uc(UC_ARCH_X86, UC_MODE_16)
        self.mu.mem_map(0, MEM_SIZE)
        self.mu.hook_add(UC_HOOK_INTR, self._on_interrupt)
        self.mu.hook_add(UC_HOOK_INSN, self._on_out, None, 1, 0, UC_X86_INS_OUT)
        self.mu.hook_add(UC_HOOK_INSN, self._on_in, None, 1, 0, UC_X86_INS_IN)
        self.mu.hook_add(UC_HOOK_CODE, self._on_trap, None, TRAP_ADDR, TRAP_ADDR + 1)
        self.mu.mem_write(TRAP_ADDR, b"\xf4")
        self._load_com(image, name)

    # -- Loading ---------------------------------------------------------

    def _load_com(self, image: bytes, name: str) -> None:
        if len(image) + LOAD_OFFSET > 0x10000:
            raise EmulationError("COM image does not fit in one segment")
        self.image_size = len(image)
        self.mu.mem_write(PSP_SEG * 16 + LOAD_OFFSET, image)

        psp = PSP_SEG * 16
        self.mu.mem_write(psp, b"\xcd\x20")
        # The MCB before the PSP carries the largest available block, which
        # memchk_init reads at +3.
        mcb = (PSP_SEG - 1) * 16
        self.mu.mem_write(mcb, b"\x4d\x00\x00\xf0\xff")
        self.mu.mem_write(psp + 0x02, struct.pack("<H", 0x9FFF))
        env = "PMDOPT=" + self.script.pmdopt if self.script.pmdopt else ""
        env_bytes = env.encode("cp932") + b"\x00\x00" if env else b"\x00\x00"
        self.mu.mem_write(ENV_SEG * 16, env_bytes)
        self.mu.mem_write(psp + 0x2C, struct.pack("<H", ENV_SEG))

        self._build_virus_data(name)

        self.mu.reg_write(UC_X86_REG_CS, PSP_SEG)
        self.mu.reg_write(UC_X86_REG_DS, PSP_SEG)
        self.mu.reg_write(UC_X86_REG_ES, PSP_SEG)
        self.mu.reg_write(UC_X86_REG_SS, PSP_SEG)
        self.mu.reg_write(UC_X86_REG_SP, 0xFFFE)
        self.mu.reg_write(UC_X86_REG_IP, LOAD_OFFSET)

    def _build_virus_data(self, name: str) -> None:
        # DOS's list of lists points at the first FCB block at +4h. The block
        # is the 6-byte header the virus check walks plus one FCB entry: the
        # game name and the file size at the DOS 3.x offsets (0x20 and 0x11).
        block = 0x20
        data = bytearray(0x200)
        struct.pack_into("<H", data, 4, block)
        struct.pack_into("<H", data, 6, DOS_DATA_SEG)
        struct.pack_into("<H", data, block, 0xFFFF)
        struct.pack_into("<H", data, block + 2, 0x0000)
        struct.pack_into("<H", data, block + 4, 1)
        struct.pack_into("<I", data, block + 6 + 0x11, self.image_size)
        fname = dos_name(name).encode("ascii")
        at = block + 6 + 0x20
        data[at:at + len(fname)] = fname
        self.mu.mem_write(DOS_DATA_SEG * 16, bytes(data))

    def _build_command_tail(self, args: str) -> None:
        tail = (" " + args).encode("cp932")[:255]
        psp = PSP_SEG * 16
        self.mu.mem_write(psp + 0x80, bytes([len(tail)]) + tail + b"\r")

    # -- Execution -------------------------------------------------------

    def run(self) -> int:
        self.mu.reg_write(UC_X86_REG_EFLAGS,
                          self.mu.reg_read(UC_X86_REG_EFLAGS) | IF)
        start = PSP_SEG * 16 + LOAD_OFFSET
        self.mu.reg_write(UC_X86_REG_CS, PSP_SEG)
        self.mu.reg_write(UC_X86_REG_IP, LOAD_OFFSET)
        pc = start
        while self.exit_code is None:
            if self.budget <= 0:
                raise EmulationError("main did not exit within the instruction budget")
            self.budget -= self.chunk
            try:
                self.mu.emu_start(pc, 0, count=self.chunk)
            except UcError as err:
                ip = self.mu.reg_read(UC_X86_REG_IP)
                cs = self.mu.reg_read(UC_X86_REG_CS)
                raise EmulationError(f"{err} at {cs:04x}:{ip:04x}") from err
            if self.exit_code is not None:
                break
            pc = self._pc()
            # WAIT.INC calibrates its loop against the PIT while its own INT 8
            # vector is installed. The virtual clock cannot measure it, so the
            # host ticks the PIT from an instruction count instead; only a
            # nonzero count matters, and /C or Board: none keeps it out of the
            # console.
            if self._pit_installed():
                self._inject_vector(8)
        return self.exit_code

    def _pc(self) -> int:
        return (self.mu.reg_read(UC_X86_REG_CS) << 4) + self.mu.reg_read(UC_X86_REG_IP)

    def _pit_installed(self) -> bool:
        offset, segment = struct.unpack("<HH", self.mu.mem_read(8 * 4, 4))
        return segment == PSP_SEG and offset != 0

    def _inject_vector(self, vector: int) -> None:
        offset, segment = struct.unpack("<HH", self.mu.mem_read(vector * 4, 4))
        saved_cs = self.mu.reg_read(UC_X86_REG_CS)
        saved_ip = self.mu.reg_read(UC_X86_REG_IP)
        self._push_frame(TRAP_SEG, TRAP_OFF)
        self.mu.reg_write(UC_X86_REG_CS, segment)
        self.mu.reg_write(UC_X86_REG_IP, offset)
        self._run_to_trap()
        self.mu.reg_write(UC_X86_REG_CS, saved_cs)
        self.mu.reg_write(UC_X86_REG_IP, saved_ip)

    def _run_to_trap(self) -> None:
        cs = self.mu.reg_read(UC_X86_REG_CS)
        ip = self.mu.reg_read(UC_X86_REG_IP)
        try:
            self.mu.emu_start((cs << 4) + ip, 0, timeout=60 * 10**6)
        except UcError as err:
            raise EmulationError(f"{err} at {cs:04x}:{ip:04x}") from err
        if self.mu.reg_read(UC_X86_REG_CS) << 4 | self.mu.reg_read(UC_X86_REG_IP) != TRAP_ADDR:
            raise EmulationError("emulation stopped away from the trap")

    def _on_trap(self, mu, address, size, user_data) -> None:
        if address == TRAP_ADDR:
            mu.emu_stop()

    # -- Interrupts ------------------------------------------------------

    def _on_interrupt(self, mu, intno, user_data) -> None:
        if intno == 0x20:
            self._exit(0)
        elif intno == 0x21:
            self._int21()
        elif intno == 0x2F:
            self._int2f()
        elif intno == 0x83:
            self._int83()
        elif intno == 0x60:
            raise EmulationError("PMD executed INT 60h internally")
        else:
            raise EmulationError(f"unexpected interrupt {intno:#04x}")

    def _int21(self) -> None:
        ax = self.mu.reg_read(UC_X86_REG_AX)
        ah = (ax >> 8) & 0xFF
        if ah == 0x09:
            self._print_string(0x24)
        elif ah == 0x02:
            self.console.write(bytes([self.mu.reg_read(UC_X86_REG_DX) & 0xFF]))
        elif ah == 0x30:
            # DOS 3.30: the virus check selects its 3.x FCB offsets.
            self.mu.reg_write(UC_X86_REG_AX, 0x1E03)
        elif ah == 0x52:
            self.mu.reg_write(UC_X86_REG_ES, DOS_DATA_SEG)
            self.mu.reg_write(UC_X86_REG_BX, 0)
        elif ah == 0x49:
            self._set_cf(False)
            self.mu.reg_write(UC_X86_REG_AX, 0)
        elif ah == 0x31:
            self._exit((self.mu.reg_read(UC_X86_REG_AX)) & 0xFF)
        elif ah == 0x4C:
            self._exit((self.mu.reg_read(UC_X86_REG_AX)) & 0xFF)
        else:
            raise EmulationError(f"unimplemented DOS call AH={ah:#04x}")

    def _int2f(self) -> None:
        ax = self.mu.reg_read(UC_X86_REG_AX)
        if ax == 0x1600:
            # Not Windows: skip the blanking wait and the virus check.
            self.mu.reg_write(UC_X86_REG_AX, 0x0000)
        else:
            raise EmulationError(f"unimplemented INT 2Fh AX={ax:#06x}")

    def _int83(self) -> None:
        # The VA builds print a NUL-terminated string at DS:SI.
        self._print_string(0x00, si=True)

    def _print_string(self, terminator: int, si: bool = False) -> None:
        if si:
            addr = (self.mu.reg_read(UC_X86_REG_DS) << 4) + self.mu.reg_read(UC_X86_REG_SI)
        else:
            addr = (self.mu.reg_read(UC_X86_REG_DS) << 4) + self.mu.reg_read(UC_X86_REG_DX)
        for offset in range(0x10000):
            byte = self.mu.mem_read(addr + offset, 1)[0]
            if byte == terminator:
                return
            self.console.write(bytes([byte]))

    def _exit(self, code: int) -> None:
        self.exit_code = code
        self.mu.emu_stop()

    def _set_cf(self, value: bool) -> None:
        flags = self.mu.reg_read(UC_X86_REG_EFLAGS)
        flags = flags | CF if value else flags & ~CF
        self.mu.reg_write(UC_X86_REG_EFLAGS, flags)

    # -- Port I/O --------------------------------------------------------

    def _on_out(self, mu, port, size, value, user_data) -> None:
        if is_host_port(port, self.board.kind):
            return
        self.record.write(f"{self.now} outb {hexval(port)} {hexval(value)}\n")
        self.board.outb(port, value, self.now)

    def _on_in(self, mu, port, size, user_data) -> None:
        if self.reads and not is_host_port(port, self.board.kind):
            self.record.write(f"{self.now} inb {hexval(port)}\n")
        return self.board.inb(port)

    # -- INT 60h calls ---------------------------------------------------

    def api(self, ah: int, al: int = 0, dx: int = 0) -> dict[str, int]:
        saved_ss = self.mu.reg_read(UC_X86_REG_SS)
        saved_sp = self.mu.reg_read(UC_X86_REG_SP)
        self.mu.reg_write(UC_X86_REG_SS, CALL_STACK_SEG)
        self.mu.reg_write(UC_X86_REG_SP, 0xFFF0)
        self.mu.reg_write(UC_X86_REG_AX, (ah << 8) | al)
        self.mu.reg_write(UC_X86_REG_DX, dx)

        handler = struct.unpack("<HH", self.mu.mem_read(0x60 * 4, 4))
        self._push_frame(TRAP_SEG, TRAP_OFF)
        self.mu.reg_write(UC_X86_REG_CS, handler[1])
        self.mu.reg_write(UC_X86_REG_IP, handler[0])
        self._run_to_trap()

        result = {
            "ah": (self.mu.reg_read(UC_X86_REG_AX) >> 8) & 0xFF,
            "al": self.mu.reg_read(UC_X86_REG_AX) & 0xFF,
            "dx": self.mu.reg_read(UC_X86_REG_DX),
            "ds": self.mu.reg_read(UC_X86_REG_DS),
        }
        self.mu.reg_write(UC_X86_REG_SS, saved_ss)
        self.mu.reg_write(UC_X86_REG_SP, saved_sp)
        return result

    def _push_frame(self, cs: int, ip: int) -> None:
        sp = self.mu.reg_read(UC_X86_REG_SP)
        ss = self.mu.reg_read(UC_X86_REG_SS)
        flags = self.mu.reg_read(UC_X86_REG_EFLAGS) | IF
        base = (ss << 4)
        # A real-mode interrupt frame: SP points at the return IP.
        self.mu.mem_write(base + sp - 2, struct.pack("<H", flags & 0xFFFF))
        self.mu.mem_write(base + sp - 4, struct.pack("<H", cs))
        self.mu.mem_write(base + sp - 6, struct.pack("<H", ip))
        self.mu.reg_write(UC_X86_REG_SP, sp - 6)

    # -- Virtual timer ---------------------------------------------------

    def _deliver_if_due(self) -> int:
        """Advances to the next timer deadline and delivers one interrupt."""
        if self.replay is not None:
            if self.replay_pos >= len(self.replay):
                raise EmulationError("replay: the golden has no more timer events")
            self.now, latched = self.replay[self.replay_pos]
            self.replay_pos += 1
            self.board.flags |= latched
        else:
            deadline = self.board.next_deadline()
            if deadline is None:
                raise EmulationError("no timer is running")
            self.now = deadline
            latched = self.board.advance(deadline)
        if latched == 0:
            return 0
        name = {1: "a", 2: "b", 3: "ab"}[latched]
        self.record.write(f"{self.now} {name}\n")
        handler = struct.unpack("<HH", self.mu.mem_read(IRQ_VECTOR * 4, 4))
        saved_cs = self.mu.reg_read(UC_X86_REG_CS)
        saved_ip = self.mu.reg_read(UC_X86_REG_IP)
        self._push_frame(TRAP_SEG, TRAP_OFF)
        self.mu.reg_write(UC_X86_REG_CS, handler[1])
        self.mu.reg_write(UC_X86_REG_IP, handler[0])
        self._run_to_trap()
        self.mu.reg_write(UC_X86_REG_CS, saved_cs)
        self.mu.reg_write(UC_X86_REG_IP, saved_ip)
        return latched

    # -- Actions ---------------------------------------------------------

    def do_actions(self) -> None:
        for verb, rest in self.script.actions:
            if verb == "main":
                self._build_command_tail(rest)
                code = self.run()
                self.record.write(f"{self.now} ret exit {hex8(code)}\n")
                if code != 0:
                    # The original process is gone; the script cannot continue.
                    return
            elif verb == "load":
                self._load(rest)
            elif verb == "mstart":
                self.record.write(f"{self.now} ret mstart {hex8(self._result(0x00))}\n")
            elif verb == "mstop":
                self.record.write(f"{self.now} ret mstop {hex8(self._result(0x01))}\n")
            elif verb == "status":
                result = self.api(0x0A)
                self.record.write(
                    f"{self.now} ret status {hex8(result['ah'])} {hex8(result['al'])}\n")
            elif verb == "drvchk":
                result = self.api(0x09)
                self.record.write(
                    f"{self.now} ret drvchk {hex8(result['al'])} {hex8(result['ah'])} "
                    f"{hex8(result['dx'] & 0xFF)}\n")
            elif verb == "size":
                result = self.api(0x22)
                self.record.write(
                    f"{self.now} ret size {hex8(result['al'])} {hex8(result['ah'])} "
                    f"{hex8(result['dx'] & 0xFF)}\n")
            elif verb == "ss":
                # The no-board path leaves AH untouched; the documented result
                # is AL only.
                self.record.write(f"{self.now} ret ss {hex8(self.api(0x05)['al'])}\n")
            elif verb == "fv":
                self.record.write(f"{self.now} ret fv {hex8(self.api(0x08)['al'])}\n")
            elif verb == "fmefcnum":
                self.record.write(f"{self.now} ret fmefcnum {hex8(self.api(0x11)['al'])}\n")
            elif verb == "pcmefcnum":
                self.record.write(f"{self.now} ret pcmefcnum {hex8(self.api(0x12)['al'])}\n")
            elif verb == "psgefcnum":
                result = self.api(0x15)
                self.record.write(
                    f"{self.now} ret psgefcnum {hex8(result['ah'])} {hex8(result['al'])}\n")
            elif verb == "ppsdrv":
                self.record.write(f"{self.now} ret ppsdrv {hex8(self.api(0x17)['al'])}\n")
            elif verb == "joy":
                result = self.api(0x16)
                self.record.write(
                    f"{self.now} ret joy {hex8(result['al'])} {hex8(result['ah'])}\n")
            elif verb in ("fout", "setfv", "partmask", "effon", "fmefon"):
                command = {
                    "fout": 0x02, "setfv": 0x19, "partmask": 0x1E,
                    "effon": 0x03, "fmefon": 0x0C,
                }[verb]
                self.api(command, al=int(rest, 0) & 0xFF)
            elif verb in ("effoff", "fmefoff"):
                self.api(0x04 if verb == "effoff" else 0x0D)
            elif verb == "ff":
                result = self.api(0x1C, dx=int(rest, 0) & 0xFFFF)
                self.record.write(f"{self.now} ret ff {hex8(result['al'])}\n")
            elif verb == "pause":
                self.api(0x1A)
            elif verb == "resume":
                self.api(0x1B)
            elif verb == "run":
                self._run(rest)
            elif verb == "run-until":
                self._run_until(rest)
            else:
                raise EmulationError(f"unknown action {verb!r}")

    def _result(self, ah: int) -> int:
        # The original reports a deferred or rejected call as AH=FFh and
        # leaves the input AH otherwise; the trace compares the flag.
        return 0xFF if self.api(ah)["ah"] == 0xFF else 0x00

    def _load(self, path: str) -> None:
        source = self.cwd / path
        if not source.is_file():
            raise EmulationError(f"fixture {path} not found")
        data = source.read_bytes()
        size = self.api(0x22)["al"] * 1024
        result = self.api(0x06)
        address = (result["ds"] << 4) + result["dx"]
        if len(data) > size:
            raise EmulationError(f"{path}: {len(data)} bytes does not fit in {size} bytes")
        self.mu.mem_write(address, bytes(size))
        self.mu.mem_write(address, data)

    def _run(self, rest: str) -> None:
        parts = rest.split()
        if len(parts) != 2 or parts[0] != "b":
            raise EmulationError("run takes 'b N'")
        limit = int(parts[1], 0)
        count = 0
        spins = 0
        while count < limit:
            latched = self._deliver_if_due()
            if latched & 2:
                count += 1
            elif latched == 0:
                spins += 1
                if spins > 1_000_000:
                    raise EmulationError("run: no timer interrupt latched")

    def _run_until(self, rest: str) -> None:
        parts = rest.split()
        if len(parts) != 4 or parts[0] != "status2" or parts[2] != "max":
            raise EmulationError("run-until takes 'status2 V max N'")
        value = int(parts[1], 16)
        limit = int(parts[3], 0)
        count = 0
        spins = 0
        while True:
            latched = self._deliver_if_due()
            if latched & 2:
                count += 1
            elif latched == 0:
                spins += 1
                if spins > 1_000_000:
                    raise EmulationError("run-until: no timer interrupt latched")
            if latched:
                status2 = self.api(0x0A)["al"]
                if status2 == value:
                    return
            if count >= limit:
                raise EmulationError(
                    f"run-until: status2 did not reach {value:#04x} in {limit} Timer B ticks")


def replay_events(path: Path) -> list[tuple[int, int]]:
    """Reads the timer deliveries from an existing golden as a schedule."""
    events = []
    for line in path.read_text(encoding="utf-8").splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[1] in ("a", "b", "ab"):
            events.append((int(parts[0]), {"a": 1, "b": 2, "ab": 3}[parts[1]]))
    return events


def repro(pmd_dir: Path, cases_dir: Path, name: str, replay: bool = False) -> None:
    script = parse_script(cases_dir / f"{name}.RUN")
    exe = pmd_dir / f"{script.driver.upper()}.COM"
    if not exe.is_file():
        raise EmulationError(f"{exe} not found")
    console = io.BytesIO()
    machine = Machine(exe.read_bytes(), exe.name, script, cases_dir, console)
    if replay:
        machine.replay = replay_events(cases_dir / f"{name}.LOG")
    machine.do_actions()
    text = console.getvalue().replace(b"\r", b"")
    try:
        # The original messages are standard Shift JIS. The JIS mapping is
        # what the sources were converted to UTF-8 with, so decoding with
        # Windows CP932 (which maps 8160h to U+FF5E rather than U+301C) would
        # report a difference in the wave dash of the usage text.
        out = text.decode("shift_jis").encode("utf-8")
    except UnicodeDecodeError as err:
        raise EmulationError(f"cannot decode console output: {err}") from err
    (cases_dir / f"{name}.OUT").write_bytes(out)
    (cases_dir / f"{name}.LOG").write_bytes(machine.record.getvalue().encode("utf-8"))


def main(argv: list[str]) -> int:
    replay = "--replay" in argv[1:]
    args = [arg for arg in argv[1:] if not arg.startswith("--")]
    if len(args) != 3:
        print("usage: repro.py PMD-DIR CASES-DIR [--replay] NAME[.RUN]", file=sys.stderr)
        return 1
    pmd_dir, cases_dir = (Path(arg) for arg in args[:2])
    name = Path(args[2]).stem
    try:
        repro(pmd_dir, cases_dir, name, replay)
    except (EmulationError, OSError) as err:
        print(f"{name}: {err}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
