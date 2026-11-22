# PMD trace tests

The original PMD's observable output is not a file: over each timer tick it
writes FM/PSG registers and (with the optional resident drivers) calls into
them. These tests capture that stream from the original binary and compare it
with the same stream produced by the translation.

Each case is a script (`NAME.RUN`) plus its fixtures. Two hosts execute it:

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

The original binaries live in `original/pmd48s`. Run one case at a time:

    cd test/pmd
    uv run repro.py ../../original/pmd48s cases INSTALL_26K

That reads `cases/INSTALL_26K.RUN`, runs `PMD.COM`, and rewrites
`cases/INSTALL_26K.OUT` and `cases/INSTALL_26K.LOG`. Add `--replay` to take the
timer schedule from the existing golden instead of from the board's timer
registers; see "Timer model" below for why that is useful.

The trace tests are `zig build test-pmd-traces`, which runs the harness for
every variant (a case names the driver it belongs to, so the others skip it).
`zig build test-pmd-traces -Dreplay=true` forwards `--replay` to every host.
The harness accepts `--verbose` for per-case results and `--replay` itself.

## Script format

A `;` starts a comment. Comment lines at the top of the file are headers:

    ; Driver: pmd
    ; Board: 26K
    ; Chip: YM2203
    ; PMDOPT: /M10

- `Driver` is the reference binary: `pmd` for `PMD.COM`, `pmd86` for
  `PMD86.COM`, and so on. It also selects the translated variant; a host skips
  a case whose driver is not the one it was built for.
- `Board` selects the fake board: `26K` (PC-98, chip at 088h), `86B` (PC-98
  86B, chip at 188h, OPNA present), or `none`, which answers every probe as
  absent. `86` (PC-88) and `VA` (chip at 44h) are not modelled yet.
- `Chip` is `YM2203` or `YM2608`. The timer constants are the same for both,
  so this only documents the case.
- `PMDOPT` sets the environment variable the resident options are read from.
  Optional.
- `Reads: on` records `inb` events on both hosts for diagnosis. No checked-in
  case uses it; reads are otherwise not part of a golden.

Every other line is one action, in order.

| Action | Register contract | Recorded result |
| --- | --- | --- |
| `main OPTS` | run the COM until its TSR exit; the translation calls `pmd_resident_main` | console, `outb`s, `ret exit STATUS` |
| `load NAME` | INT 60h AH=06h buffer; the file is read into it | none |
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

`ss` records AL only: with no board the original leaves AH as the caller set
it, and the translation returns an explicit no-board value there.

There is no `assert` verb: every action that returns something records a
`ret` event, so the golden is the assertion.

Note that `load` does not reproduce `PMP.COM`'s `MSTOP` before the read. The
two hosts zero the music area and read the fixture into the address returned
by the API; a fixture larger than the configured music buffer is an error.

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
  at the end of `main`.

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
- The OPNA ID port 0A460h: FFh when no OPNA is declared, 00h otherwise. Writes
  to it track the driver's OPNA on/off sequence, which matters twice: while the
  OPNA is off the second chip's status port (base+4) reads FFh, which is the
  value `check_86b` probes for, and while it is on it reads 00h so `rdychk`
  does not wait forever. The second chip's data port (base+6) reads FFh. That
  is enough for the YM2608 probe sequences, and no supported case uses the
  ADPCM ports yet. The mute ports 0A66Eh and 0A468h read 00h.
- An unmodelled read or write fails the case.

Host-owned PC-98 ports are neither recorded nor modelled beyond what the
original's installer needs: the PIC mask register 02h reads all-masked, so the
installer installs its own handler instead of chaining the previous INT 5h,
and the PIT/PIC writes (00h, 02h, 08h, 0Ah, 71h, 77h) never reach the trace.

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
- No pointer-valued action is scripted yet. When they are added, pointers into
  the module data area are logged as offsets from the data area base, and
  other pointers are interned as aliases in first-seen order.

`.OUT` is UTF-8 in both hosts: `repro.py` decodes the reference console and
re-encodes it, while the translation prints its UTF-8 strings. The decoding
uses the JIS mapping of Shift JIS rather than Windows CP932, because that is
the mapping the original sources were converted to UTF-8 with; CP932 would
turn the usage text's `8160h` wave dash into U+FF5E and report a difference.
Console cases must avoid module-derived text such as the music name.

## Cases

| Case | Covers |
| --- | --- |
| `INSTALL_NONE` | no board: probe writes, warning text, wait counts 0, no-board API results |
| `INSTALL_26K` | 26K install with `/C`: probe, board init registers, status |
| `INSTALL_86B` | 86B install with `/C`: OPNA probe/on/off sequence and the YM2608 init |
| `TIMER_26K` | `/C` plus `run b 8`: Timer A/B periods and their interleaving with no module |
| `API_26K` | the INT 60h probes and controls that need no module, with a board |
| `PLAY_SAMPLE` | official `SAMPLE.M`, `mstart`, 16 ticks, status |
| `PLAY_SAMPLE2` | official `SAMPLE2.M`, same shape |
| `PLAY_SSGEG` | official `SSGEG_S.M`, same shape |
| `PLAY_END` | `DEFONKAI.M` (from `test/mc/cases`) played to its end, bounded by `run-until status2 ff max 512` |
| `USAGE_NONE` | no board plus `/H`: the full usage text and exit status 255 |
| `OPTIONS_26K` | `PMDOPT` buffers overridden by the command line, `/K-`, and the `size`/`drvchk`/`ppsdrv` results |

`SAMPLE.M`, `SAMPLE2.M`, and `SSGEG_S.M` come from the official 4.8s
distribution; `DEFONKAI.M` is an MC output copied from `test/mc/cases`.

## Limitations

The variant matrix, external drivers, and the data-memory paths are follow-ups
tracked in `PLAN.md`: only `pmd` has cases so far, `Board: VA` and `Board: 86`
are not modelled, and the PPZ8/P86DRV/ADPCM ports need their own fixtures. The
harness, script format, and board/timer models are in place for those cases to
land without changing the design. The 86B install case proves the OPNA probe
path; playback on a YM2608 board would additionally need the ADPCM/rhythm
registers, which the current board model deliberately does not answer.

To add a case: write `NAME.RUN` (plus any fixture), regenerate the goldens with
`repro.py`, and run `zig build test-pmd-traces`. Do not hand-edit a golden
without checking the change against the assembly or a replay run.
