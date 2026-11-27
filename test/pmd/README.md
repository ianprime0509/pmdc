# PMD trace tests

The original PMD's observable output is not a file: over each timer tick it
writes FM/PSG registers and (with the optional resident drivers) calls into
them. These tests capture that stream from the original binary and compare it
with the same stream produced by the translation.

Cases are grouped by driver under `cases/<driver>/`, and shared modules live
in `cases/fixtures/`. Each case is a script (`NAME.RUN`) plus its fixtures;
two hosts execute it:

- `repro.py` runs one of the reference `.COM` files from `original/pmd48s`
  under Unicorn, with a fake DOS, a fake FM board, and a virtual timer.
- `trace.c`, built once per variant as `pmd-trace-<variant>`, calls the
  translated driver's typed interface with the same board and timer models.

Both write the same two files, and `harness.zig` compares them:

- `NAME.OUT`: the console text, UTF-8, byte for byte.
- `NAME.LOG`: the ordered event trace, line by line.

`zig build test` runs the checked-in goldens; it needs neither Python nor
Unicorn. Regenerating a golden needs both.

## Reproducing a golden

The original binaries live in `original/pmd48s`. The case directory names the
driver, and the header selects the matching COM. Run one case at a time:

    cd test/pmd
    uv run repro.py ../../original/pmd48s cases/pmd INSTALL_26K

That reads `cases/pmd/INSTALL_26K.RUN`, runs `PMD.COM`, and rewrites
`cases/pmd/INSTALL_26K.OUT` and `cases/pmd/INSTALL_26K.LOG`. Fixtures shared by
several cases live in `cases/fixtures`; a script names them relative to its own
directory, e.g. `load ../fixtures/SAMPLE.M`. Add `--replay` to take the timer
schedule from the existing golden instead of from the board's timer registers;
see "Timer model" below for why that is useful.

The trace tests are `zig build test-pmd-traces`, which runs each variant's
harness against `cases/<variant>`.
`zig build test-pmd-traces -Dreplay=true` forwards `--replay` to every host.
The harness accepts `--verbose` for per-case results and `--replay` itself.

## Script format

A `;` starts a comment. Comment lines at the top of the file are headers:

    ; Driver: pmd
    ; Board: 26K
    ; Chip: YM2203
    ; PMDOPT: /M10

- `Driver` is the reference binary: `pmd` for `PMD.COM`, `pmd86` for
  `PMD86.COM`, and so on. It must match the case directory; that directory is
  also what selects the translated build, and a mismatch fails the case rather
  than skipping it.
- `Board` selects the fake board: `26K` (PC-98, chip at 088h), `86B` (PC-98
  86B, chip at 188h, OPNA present), `VA` (PC-88VA, chips at 044h and 046h), or
  `none`, which answers every probe as absent. `86` (PC-88) is not modelled:
  no shipped binary targets that machine.
- `Chip` is `YM2203` or `YM2608`. The timer constants are the same for both,
  so it only documents the case, except on `Board: VA`, where it selects
  whether the OPNA register test succeeds (`pmdva` needs YM2608, `pmdva1`
  YM2203).
- `PMDOPT` sets the environment variable the resident options are read from.
  Optional.
- `Reads: on` records `inb` events on both hosts for diagnosis. No checked-in
  case uses it; reads are otherwise not part of a golden.
- `Drivers` lists the fake resident drivers to install, space separated:
  `pps` (INT 64h), `ppz8` (INT 7Fh), and `p86` (INT 65h, the PCM86 driver).
  The original checks the vectors' residency signature; the translation
  receives the matching mock vtables. `p86` needs a board and a case with a
  PCM86 part; the fake card's FIFO and sample area are described under
  "Board model".

Every other line is one action, in order.

| Action | Register contract | Recorded result |
| --- | --- | --- |
| `main OPTS` | run the COM until its TSR exit; the translation calls `pmd_resident_main` | console, `outb`s, `ret exit STATUS` |
| `change OPTS` | run the COM again against the resident; the translation calls `pmd_change_main` | console, `outb`s, `ret change RESULT`, `ret exit STATUS` |
| `load NAME` | INT 60h AH=06h buffer; the file is read into it | none |
| `loadvoice NAME` | AH=07h buffer; the voice file is read into it | none |
| `loadeffect NAME` | AH=0Bh buffer; the EFC file is read into it | none |
| `loadpcm NAME` | the PCM86 voice table and the fake P86DRV's sample area; a .P86 file | none |
| `loadadpcm NAME` | the ADPCM voice table and the board's ADPCM-B memory; an .ADP file | none |
| `loadrom NAME` | the board's ADPCM-A memory; a rhythm ROM image | none |
| `rom OFFSET COUNT` | host data memory, no driver call | `ret rom OFFSET BYTES...` |
| `mstart` | AH=00h, `pmd_mstart_f` | `ret mstart 00` or `ff` |
| `mstop` | AH=01h, `pmd_mstop_f` | `ret mstop 00` or `ff` |
| `status` | AH=0Ah, `pmd_get_status` | `ret status AH AL` |
| `drvchk` | AH=09h, `pmd_drv_chk` | `ret drvchk AL AH DL` |
| `size` | AH=22h, `pmd_get_size` | `ret size AL AH DL` |
| `ss` | AH=05h, `pmd_get_ss` | `ret ss AL` |
| `fv` | AH=08h, `pmd_get_fv` | `ret fv AL` |
| `fmefcnum` | AH=11h, `pmd_get_fmefc_num` | `ret fmefcnum AL` |
| `pcmefcnum` | AH=12h, `pmd_get_pcmefc_num` | `ret pcmefcnum AL` |
| `psgefcnum` | AH=15h, `pmd_get_psgefcnum` | `ret psgefcnum AH AL` |
| `ppsdrv` | AH=17h, `pmd_get_ppsdrv_flag` | `ret ppsdrv AL` |
| `joy` | AH=16h, `pmd_get_joy` | `ret joy AL AH` |
| `musdat` / `tondat` / `efcdat` | AH=06h/07h/0Bh, the data-area pointers | `ret NAME OFFSET` |
| `pcm` | AH=0Eh, `pmd_get_pcm_adr` | `ret pcm ALIAS` or `none` |
| `workadr` | AH=10h, `pmd_get_workadr` | `ret workadr ALIAS` |
| `memo N` | AH=1Dh, `pmd_get_memo` | `ret memo OFFSET` or `none` |
| `fout N` | AH=02h, AL=N, `pmd_fout` | none |
| `setfv N` | AH=19h, AL=N, `pmd_set_fv` | none |
| `partmask N` | AH=1Eh, AL=N, `pmd_part_mask` | none |
| `effon N` / `effoff` | AH=03h/04h, `pmd_eff_on`/`pmd_effoff` | none |
| `fmefon N` / `fmefoff` | AH=0Ch/0Dh, `pmd_fm_effect_on`/`pmd_fm_effect_off` | none |
| `ff BAR` | AH=1Ch, DX=BAR, `pmd_ff_music` | `ret ff AL` |
| `pause` / `resume` | AH=1Ah/1Bh, `pmd_pause_on`/`pmd_pause_off` | none |
| `run b N` | advance until the N-th Timer B interrupt has been handled | `a`/`b`/`ab` lines and `outb`s |
| `run-until status2 V max N` | advance until a handler leaves `status2` at V (hex), failing after N Timer B interrupts | same |

`mstart` and `mstop` return AH=FFh when the call is deferred or rejected and
otherwise leave the caller's AH alone, so the trace records a normalized
success flag: `00` or `ff`.

`main` records the process exit code last, `ret exit STATUS` (00 for the
ordinary AH=31h TSR exit). That lets an option or usage error be a golden: the
console text and the probe writes are compared, and no later action runs. The
translation reports the same status through `pmd_sys.exit`.

`change` loads the COM a second time, at `CHANGE_SEG`, the way DOS would start
another instance of the same program, and runs it until it exits. That instance
prints the banner, finds PMD resident, and reaches `change_main` through the
original's own resident check, so the console and the shutdown writes are the
original's. It records `ret change 00` when the status was applied and
`ret change 01` when `/R` removed the driver; the latter is detected from the
restored INT 60h vector. An instance that ends on an error records
`ret exit STATUS` instead (`/M` prints `pmderror_mes6` and exits 1, `/H` prints
the usage text and exits 255). The resident survives an error, so the script
continues; after a removal it does not, because the original restores the
vector and frees the block, so a `/R` change has to be a case's last action.

`change` also has to perform the INT 60h instructions that instance executes:
the resident check asks for the work address (AH=10h), and `rescut_main` drives
the whole shutdown through nested calls. `repro.py` performs each of them
through the vector and resumes at the following instruction; the translation
calls the typed entry points directly instead.

`ss` records AL only: with no board the original leaves AH as the caller set
it, and the translation returns an explicit no-board value there.

There is no `assert` verb: every action that returns something records a
`ret` event, so the golden is the assertion.

Note that `load` does not reproduce `PMP.COM`'s `MSTOP` before the read. The
music, voice, and effect loads zero the target area and read the fixture into
the address returned by the API; a fixture larger than the area the options
configure is an error. `tondat` is only read when the module has no embedded
program area (`prg_flg == 0`), so a voice-bank case needs a module whose first
part starts at offset 24; `cases/fixtures/VOICE.M` is one.

`loadpcm` stands in for P86DRV's `.P86` reader: it parses the file's 16-byte
header and 256 six-byte start/size entries into the PCM voice table (through
`pmd_get_pcm_adr` / INT 60h AH=0Eh) and copies the sample data into the fake
driver's 64 KiB area, which `get_data_offset` then maps. It records nothing.

`loadadpcm` stands in for whatever external loader fills PMD's ADPCM table and
the YM2608's ADPCM-B memory. The file has a 16-byte header, 256 four-byte
start/stop entries in the 32-byte units the driver writes to registers 02h-05h,
and the sample image. The entries go into the driver at offset 2 of the
`pmd_get_pcm_adr` pointer (after `pcmends`, `PMD.ASM:8357`), and the image fills
the board's 64 KiB ADPCM-B memory. It records nothing. `loadrom` copies a
rhythm ROM image into the board's 64 KiB ADPCM-A memory, which is also where
the YM2608 reads rhythm samples. `rom OFFSET COUNT` records the ADPCM-A bytes
there; neither the original nor the translation exposes them through a driver
call, so the action is how the fixture's contents are compared.

## Trace format

One event per line: `<clock> <event> [args...]`. The clock is a decimal count
of OPN master clocks; ports, registers, and values are lowercase hex.

    0 outb 44 29
    0 ret status 00 01
    64512 b
    73728 a

- `outb PORT VALUE`: every write the driver makes, in order, including the
  install and probe writes at clock 0.
- `a`, `b`, `ab`: the interrupt delivered at this clock. A coincident Timer A
  and B overflow is one delivery, `ab`, never two.
- `ret NAME VALUES`: a scripted action's result, including `ret exit STATUS`
  at the end of `main` or of an errored `change`, `ret change RESULT` for a
  `change` that ended normally, and `ret rom OFFSET BYTES...`, the ADPCM-A
  memory dump the `rom` action records.
- `pps OP ARGS`, `ppz8 OP ARGS`, `p86 OP ARGS`: a call into a fake resident
  driver, named for the interface operation rather than the interrupt register
  contract (`pps keyon NUM DETUNE VOLUME`, `pps pdr NUMBER MODE`, `ppz8
  protect BOOL`, `p86 push`, ...). The PDR line records what the driver
  receives: `PMD.ASM:2483-2484` split the module's `*` operand into AL =
  `n >> 1` and DL = `n & 1`. The PPZ8 entry obtained through 0409h is recorded
  as `ppz8 fmint`, and the voice-table query is not recorded because only its
  contents affect the driver. The P86DRV lines are `p86 version 10`, `p86
  push`, `p86 pop`, and `p86 data OFFSET`, where OFFSET is the 24-bit sample
  offset the driver asks the card to map.

Not recorded:

- `inb` reads. The driver reads the status twice before every FM write, and
  the values belong to the board model rather than the driver. A `; Reads: on`
  header logs them on both hosts for diagnosis; no checked-in case uses it.
- Console text; that is `NAME.OUT`.
- Addresses and segments.

`repro.py` records every non-host `OUT` instruction, so the golden includes
the original's writes to the FM ports and to the OPNA mute/ID ports. The C
host records the same calls through `pmd_hw.outb`.

## Board model

The fake board is deliberately small and must stay identical in both hosts:

- FM status port: BUSY always clear; bits 0/1 are the latched Timer A/B flags.
- FM data port reads: register 0Eh and 0Fh return 00h (the interrupt-number
  bits are 0, i.e. IRQ 3 / INT 0Bh); every other address returns the last byte
  written to that data port, which is how the 0AAh probe reads back.
- The second chip: its status port (base+4) reads FFh while the OPNA is off,
  which is what `check_86b` probes for, and 00h once it is on, so `rdychk`
  does not wait forever. Its data port (base+6) reads FFh. The high register
  bank is the ADPCM-B interface: registers 00h-0Fh are modelled (control,
  memory mode, start address, data) and 10h is the flag control. The model
  keeps a 64 KiB external memory, shifts the start address by five bits in
  the 8-bit and ROM/RAM modes (`ymfm_adpcm.cpp`), consumes the two dummy
  reads before a read transfer, and returns the stored bytes from the data
  port, so `ADRAMCHK.ASM` reads back what it wrote. The status port reports
  BRDY (bit 3) while the bank is in external mode, which is all the check
  polls; EOS is not modelled because the driver never tests it. `loadadpcm`
  fills that memory with a sample image, and `loadrom` fills the separate
  64 KiB ADPCM-A memory the chip reads for rhythm instruments. The trace hosts
  do not decode either memory, so only the driver's register stream and the
  `rom` action observe them.
- The OPNA ID port 0A460h: FFh when no OPNA is declared, 00h otherwise, and
  writes to it track the driver's OPNA on/off sequence. The mute port 0A66Eh
  reads 00h.
- The PCM86 card at 0A466h-0A46Ch. The FIFO register 0A468h reads back its
  control bits, with bit 4 replaced by the interrupt flag. The card raises
  that flag on every timer delivery while the interrupt enable (bit 5) and
  playback start (bit 7) bits are set, and any write that clears bit 4 clears
  it; that is what the driver polls from `opnint`, and it is what drives the
  transfers. The volume register 0A466h, the pan/rate register 0A46Ah, and the
  sample data port 0A46Ch are write-only in the model. The fake P86DRV answers
  INT 65h (version 10h, EMS push/pop, and the sample offset lookup) and owns
  the sample area `loadpcm` fills.
- `Board: VA` uses four adjacent byte ports instead of word pairs: the same
  rules apply at 044h/045h and 046h/047h, and the second chip's status port
  reads 00h until its ADPCM bank is in external mode (the 88VA has no OPNA
  gate).
- An unmodelled read or write fails the case.

Host-owned ports are neither recorded nor modelled beyond what the original's
installer needs: the PIC mask registers read all-masked, so the installer
installs its own handler instead of chaining the previous INT 5h, and the
PIT/PIC writes never reach the trace. Those ports are 00h, 02h, 08h, 0Ah,
71h, and 77h on the PC-98 boards, plus 32h on the 88VA (where `int_init`
clears the OPN interrupt request) and 184h-18Ah, which is where the 88VA's
8259s live.

`repro.py` builds the DOS structures the installer reads directly: the PSP,
the environment block, the MCB size that `memchk_init` compares against, and
the FCB block the virus check walks. Populating that block with the binary's
size and 8.3 name is what makes the check accept the shipped COM.

## Timer model

The clock is integer OPN master clocks. Timer A reloads `(1024 - NA) * 72`
and Timer B `(256 - TB) * 1152`, where NA is the 10-bit value in registers
24h/25h and TB is register 26h.

The model follows the chip:

- Register 27h bits 0/1 load the timers. A timer starts only if it is not
  already running; a load write to a running timer does not move its deadline.
  Bits 2/3 enable the corresponding status flag and interrupt, and bits 4/5
  reset the flags. This matters: the driver writes 27h=3Fh in its interrupt
  handler on every tick, and moving the deadlines there would starve Timer A.
- At a deadline the timer latches its flag if enabled, reschedules itself from
  the register values *as they are at that instant*, and then the host delivers
  one interrupt. A handler that writes 26h therefore retimes the reload after
  the next one, and a tempo change takes effect one tick late. This is the
  chip's behavior; it is reproduced rather than corrected.
- A tick where no enabled timer latches produces no event, and both hosts
  advance past it silently.

The install order explains the first trace: the probe-time initialization
writes 27h=3Fh while 24h/25h/26h are still 0, which starts both timers with
A = 1024*72 = 73728 clocks and B = 256*1152 = 294912. `setint` then writes
26h=200 (0C8h) and 27h=3Fh, but the B timer is already running, so its first
reload keeps the 294912-clock period; only after that first expiry does it use
the 64512-clock period. The first A tick is at 73728, the first B tick at
294912 (`ab`), and later A and B coincide every 8 B ticks (72/63 = 8/7).

`WAIT.INC` measures `loop $` against the PIT to calibrate the register write
wait. That calibration has no meaning under a virtual clock and its result is
never compared: `repro.py` ticks INT 8 from an instruction count while the
calibration's own vector is installed, only so the measurement terminates, and
`/C` or `Board: none` keeps the counts out of the console. The translation
sets both counts to 1, which is host-owned.

## Comparison and normalization

Compare `.LOG` line by line and `.OUT` byte for byte. On a mismatch the
harness prints the first differing trace line with its clock and a few lines
on either side (or the first differing console byte and its line).

Only these host differences are normalized:

- The PIC/PIT ports and the `WAIT.INC` wait counts, as above.
- `mstart`/`mstop`'s deferred/rejected result is recorded as a flag, and `ss`
  as AL only, because the original's no-board entries leave the other bits
  untouched.
- Pointer-valued actions. `musdat`, `tondat`, `efcdat` and `memo` point into
  the module data area, whose layout is the original's, so they are logged as
  offsets from the data-area base (`musdat` is 0 by definition). `pcm` and
  `workadr` point inside the translated `struct pmd`, whose layout is not
  comparable, so they are interned as aliases (`p1`, `p2`, ...) in first-seen
  order; that catches an accidental alias between the two without asserting
  an address. The default and VA1 builds have no PCM driver, so their
  `pmd_get_pcm_adr` reports absence where the original returns the address of
  a table nothing reads; `ret pcm none` is recorded for both.

The VA builds print through a NUL-terminated DOS call while the other builds
use the `$`-terminated one. `mes_warning` has no terminator of its own, so the
VA console prints `WARNING:$` and the board message twice. That is driver
output and the translation reproduces it; it is not normalized.

`.OUT` is UTF-8 in both hosts: `repro.py` decodes the reference console and
re-encodes it, while the translation prints its UTF-8 strings. The decoding
uses the JIS mapping of Shift JIS rather than Windows CP932, because that is
the mapping the original sources were converted to UTF-8 with; CP932 would
turn the usage text's `8160h` wave dash into U+FF5E and report a difference.
Console cases must avoid module-derived text such as the music name.

## Cases

Each variant's directory covers the same shapes: a no-board install, a board
install, the INT 60h probes, a playback, and the module-loaded control trace.

| Directory | Board | Cases |
| --- | --- | --- |
| `pmd` | 26K, 86B, none | `INSTALL_NONE`, `INSTALL_26K`, `INSTALL_86B`, `TIMER_26K`, `API_26K`, `DATA_26K`, `PPS_26K`, `PDR_26K`, `VOICE_26K`, `EFC_26K`, `SSGEFC_26K`, `PLAY_SAMPLE`, `PLAY_SAMPLE2`, `PLAY_SSGEG`, `PLAY_86B`, `PLAY_END`, `PLAY_TIMING`, `PLAY_CONTROL`, `USAGE_NONE`, `OPTIONS_26K`, `CHANGE_NONE`, `CHANGE_RESET_26K` |
| `pmd86` | 86B, none | `INSTALL_NONE`, `INSTALL_86B`, `API_86B`, `PLAY_SAMPLE`, `PLAY_CONTROL`, `PCM86_86B`, `PCM86_S_86B`, `PCM86_HZ_86B`, `CHANGE_NONE`, `CHANGE_RESET` |
| `pmdb2` | 86B, none | `INSTALL_NONE`, `INSTALL_86B`, `ADPCM_86B`, `ADPCM_PLAY_86B`, `RHYTHM_86B`, `API_86B`, `PLAY_SAMPLE`, `PLAY_CONTROL`, `CHANGE_NONE`, `CHANGE_RESET` |
| `pmdppz` | 86B, none | `INSTALL_NONE`, `INSTALL_86B`, `PPZ_86B`, `PPZ_RESET_86B`, `PPZ_PLAY_86B`, `ADPCM_PLAY_86B`, `RHYTHM_86B`, `API_86B`, `PLAY_SAMPLE`, `PLAY_CONTROL`, `CHANGE_NONE`, `CHANGE_RESET` |
| `pmdppze` | 86B, none | `INSTALL_NONE`, `INSTALL_86B`, `PPZ_86B`, `PPZ_PLAY_86B`, `API_86B`, `PLAY_SAMPLE`, `PLAY_CONTROL`, `CHANGE_NONE`, `CHANGE_RESET` |
| `pmdva` | VA, none | `INSTALL_NONE`, `INSTALL_VA`, `ADPCM_PLAY_VA`, `RHYTHM_VA`, `API_VA`, `PLAY_SAMPLE`, `PLAY_CONTROL`, `CHANGE_NONE`, `CHANGE_RESET` |
| `pmdva1` | VA, none | `INSTALL_NONE`, `INSTALL_VA`, `API_VA`, `PLAY_SAMPLE`, `PLAY_CONTROL`, `CHANGE_NONE`, `CHANGE_RESET` |

What the `pmd` cases cover:

| Case | Covers |
| --- | --- |
| `INSTALL_NONE` | no board: probe writes, warning text, wait counts 0, no-board API results |
| `INSTALL_26K` | 26K install with `/C`: probe, board init registers, status |
| `INSTALL_86B` | 86B install with `/C`: OPNA probe/on/off sequence and the YM2608 init |
| `TIMER_26K` | `/C` plus `run b 8`: Timer A/B periods and their interleaving with no module |
| `API_26K` | the INT 60h probes and controls that need no module, with a board |
| `DATA_26K` | `PMDOPT` buffer sizes, the resulting `tondat`/`efcdat` offsets, the pointer aliases, and the memo table before and after `load` |
| `PPS_26K` | a resident PPSDRV: detection, then the `pps keyon`/`keyoff` calls `effon`/`effoff` make |
| `PDR_26K` | the rhythm part's `*` (PDR mode) command from `PDR.M`, with a rhythm hit between switches: one `pps pdr` call for each of the six documented operands, 0-5 |
| `VOICE_26K` | external voice bank: `VOICE.FF` loaded into `tondat`, then a no-program module that selects voice 1 with `@1` |
| `EFC_26K` | FM effect data: `EFFECT.EFC` loaded into `efcdat`, then `fmefon 0` through its `n`/`N` commands |
| `SSGEFC_26K` | the built-in SSG effect table: `effon 0` and its pitch/noise writes each Timer A tick |
| `PLAY_SAMPLE` | official `SAMPLE.M`, `mstart`, 16 ticks, status |
| `PLAY_SAMPLE2` | official `SAMPLE2.M`, same shape |
| `PLAY_SSGEG` | official `SSGEG_S.M`, same shape |
| `PLAY_86B` | `SAMPLE.M` on the 86B board: the FM6/rhythm writes the second chip adds |
| `PLAY_END` | `SHORT.M` played to its end, bounded by `run-until status2 ff max 512` |
| `PLAY_TIMING` | `SAMPLE2.M` over 88 Timer B ticks: the module's tempo rewrite of the Timer B reload, coincident A/B overflows, `fout` to full fade and back, then `mstop`/`mstart` reinitialization |
| `PLAY_CONTROL` | `SAMPLE2.M` with FM and SSG effects running: `ff` forward and rewind with a partial fade held across the skip, `pause`/`resume`, `partmask` on the FM, SSG, PCM, rhythm, and effect-masked parts, and a full fade-in that reaches `volset2rf` on the board2 builds |
| `USAGE_NONE` | no board plus `/H`: the full usage text and exit status 255 |
| `OPTIONS_26K` | `PMDOPT` buffers overridden by the command line, `/K-`, and the `size`/`drvchk`/`ppsdrv` results |
| `CHANGE_NONE` | the second-instance status: `/K`, `/KE`, `/KG`, `/KR`, `/DF`, `/DS`, `/W`, `/G`, `/A`, then `/I`, `/N`, `/F`, `/S`, `/P`, and `/Z` reflected in the `changemes_*` text, an option-less status, the rejected-option message, the usage text, and `/R` |
| `CHANGE_RESET_26K` | `/R` with a board and a module playing: the shutdown's keyoffs, the FM interrupt reset (`27h=30h`), and `ret change 01` |

The board2 variants add their own driver-specific cases:

| Case | Covers |
| --- | --- |
| `ADPCM_86B` | a successful `ADRAMCHK.ASM` transfer (the model reads back what it wrote, so `pcm_gs_flag` stays 0) and the PCM-part masking it changes |
| `ADPCM_PLAY_86B` / `_VA` | `ADPCM.M` through `PCMDRV.ASM` with the `ADPCM.ADP` voice table: `@n` start/stop writes, the `@n,m,o,p` repeat and release addresses, pitch, volume, and both pan directions |
| `RHYTHM_86B` / `_VA` | `loadrom` and the `rom` dump: a rhythm ROM image in the board's ADPCM-A memory, including the zero fill past the image |
| `PPZ_86B` | a resident PPZ8: the 0410h/0409h handshake, 1901h protection, and the `mstart` init/stop sequence |
| `PPZ_RESET_86B` | `/Z`: FIFO stop, one keyoff per channel, and 1900h removal permission |
| `PPZ_PLAY_86B` | `PPZEXT.M` through PPZ8: keyon, volume, tone, loop/rate from the (empty) voice table, pan, and the directly called FM entry on each tick |
| `PCM86_86B` | a resident P86DRV and `PCM86.M` with a PCM86 part: the INT 65h version/push/pop/data calls, the FIFO transfers each tick, the volume register, and both pan directions |
| `PCM86_S_86B` | `/S`: the SPB volume curve and the one-octave-lower playback of notes from 65h up |
| `PCM86_HZ_86B` | `/HZ0`: the downloaded frequency table changes the rate bits each note writes to the PCM86 register |

Every directory's `CHANGE_NONE` runs the same script on its own build, so the
status text it prints covers that variant's `put_status` blocks (the board2
volume lines, PPZ8's line, the PCM86 frequency, the 88VA key-mask rotation).
`CHANGE_RESET` pairs a short playback with `/R`; on the PPZ8 builds it also
asks the fake resident PPZ8 for the 1900h removal release. The status has to be
a `Board: none` case: `fm_check` calibrates the two wait counts by timing
`loop $`, so a board-present one prints host-owned numbers. The VA scripts also
leave out `/M16`, because `pmderr_main` prints through the `$`-terminated DOS
call while those builds' strings end at a NUL: the original runs past the text
into the image.

The variant directories reuse those shapes: the install cases pin each build's
probe and initialization sequence, the API cases pin its `drvchk` board value
and no-module results, and the playback cases pin its timer and register
writes. `PLAY_CONTROL` appears in every directory with the same script and
characterizes the module-loaded control paths on that build: the second chip's
FM2 and rhythm masks, the PCM part where the variant has one, and the effect
parts where PPZ8's work array shortens the part table.
`pmdva`/`pmdva1` additionally exercise the adjacent-port 88VA layout,
`pmdb2` the ADPCM RAM check's transfer, and `pmdb2`/`pmdppz`/`pmdva` the ADPCM
voice table and rhythm ROM.

`cases/fixtures` holds the binary fixtures a case loads, each covering one
shape: `SAMPLE.M`, `SAMPLE2.M`, and `SSGEG_S.M` are ordinary modules (taken
from the original PMD 4.8s distribution), `PPZEXT.M` uses PPZ8 extension
voices, `PDR.M` drives the rhythm part's `*` command, and `PCM86.M` has a PCM86
part. `SHORT.M` is a module short enough to reach the end of the music inside
`PLAY_END`'s tick bound; `SAMPLE.M` and `SAMPLE2.M` run for thousands of ticks,
which would make that golden an order of magnitude larger. `VOICE.FF` is an
external voice bank and `EFFECT.EFC` is EFC effect data. `VOICE.M` is hand-built
because MC always embeds a program area: its first part starts at offset 24,
which is how `play_init` detects `prg_flg == 0` and reads voices from `tondat`.
`PCM86.P86` is hand-built to the `.P86` format `PCMLOAD.INC` documents: a
16-byte header, 256 six-byte start/size entries (voice 0 is a long sine and
voice 1 a short ramp), and 4 KiB of sample data. `loadpcm` uses it to fill the
voice table and the fake P86DRV's area on both hosts. `ADPCM.M` has an ADPCM
part and comes from `test/mc/cases/ADPCM.MML`. `ADPCM.ADP` follows the same
shape as the `.P86` file with four-byte start/stop entries and a sample image:
voice 0 runs from 26h to 66h and voice 1 from 66h to 6Eh in the units the
driver writes to the chip, and the image is 6Eh*32 bytes so the rhythm-style
first offset matches `pcmends`. `RHYTHM.ROM` is a 256-byte ADPCM-A image;
everything past it reads as zero.

## Limitations

PPSDRV (INT 64h), PPZ8 (INT 7Fh), and P86DRV (INT 65h) are modelled as fake
resident drivers on both hosts, and the cases above cover detection, the
install/reset handshakes, PPSDRV effects (including the PDR switch), a PPZ8
playback, the PCM86 FIFO, EMS, and transfer calls, and the ADPCM voice table,
playback registers, and rhythm ROM. Still open:

- The reference `Board: 86` (PC-88), which no shipped binary targets.
- A board-present status: its wait counts are calibrated from a `loop $`
  timing, so only the `Board: none` status is comparable.

To add a case: write `NAME.RUN` (plus any fixture) in the driver's directory,
regenerate the goldens with `repro.py`, and run `zig build test-pmd-traces`.
Do not hand-edit a golden without checking the change against the assembly or
a replay run.
