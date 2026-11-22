# pmdc

This is a C translation of the [Professional Music Driver
(PMD)](http://www5.airnet.ne.jp/kajapon/tool.html) version 4.8s source code,
originally written by [Masahiro Kajihara (KAJA)](https://x.com/kajaponn).
MC, MCH, EFC, and PMD have been translated. It is highly likely that some bugs
remain from the translation process; the trace tests described below compare
PMD's timer-driven register stream with the shipped binaries.

This project has several goals:

- Make it possible to compile and use PMD on modern systems without relying on
  emulation such as DOSBox or similar tools. In this respect, it is similar to
  [PMDDotNet](https://github.com/kuma4649/PMDDotNET), but not limited to
  Windows.
  - This has a performance benefit as well: running MC using DOSBox as a wrapper
    has a noticeable startup delay, but running this translation natively
    completes almost instantly.
- Serve as a cross-reference for the original source code and make it easier to
  document the original's behavior and formats.
- Have fun and improve my knowledge of C and assembly.

The translation attempts to be as direct as possible, but some changes are
unavoidable due to differences between C and assembly, and some changes just
make the code easier to understand without sacrificing the original behavior
(such as replacing gotos with structured control flow wherever possible).
Cross-references to lines of the original source code are included throughout
this translation to make verification and debugging simpler.

## Building

This project uses [Zig](https://ziglang.org/) to make (cross-)compilation easy.
Currently, it requires Zig 0.17. Running `zig build` will compile all available
programs into `zig-out/bin`.

By default, the output will contain the original Japanese messages for usage
information, errors, etc. To compile with English messages, run
`zig build -Dlang=en` instead.

## Usage

`zig build` installs MC, MCH, EFC, every PMD variant, and a player per variant
into `zig-out/bin`:

```
mc SONG.MML              # MML compiler
mch SONG.MML             # MML compiler with evaluation listings
efc SONG.MML             # EFC effect compiler
pmd /M16                 # resident PMD, one-shot: prints the install report
pmd-play SONG.M           # play a module on an FM chip
pmd86-play SONG.M         # the PCM86 variant, and so on for pmdb2, pmdppz,
                          # pmdppze, pmdva, and pmdva1
```

`pmd` and the other PMD programs accept the same options as the originals
(`/M`, `/V`, `/E`, `/K`, `/C`, ...); run one with no arguments to install with
its defaults. Unlike the original, `pmd` cannot stay resident: the process
prints its report and exits. `pmd-play` is the host-side equivalent of
PMP, and `pmd-play --help` lists its chip, board, and output options.

This translation is intended to behave in exactly the same way as the original
programs, including bugs and quirks, with a few exceptions:

- Program output printed to the console is encoded in UTF-8 rather than Shift
  JIS so that it will display correctly on modern systems.
- Functionality specific to DOS can't be implemented as-is, notably, any
  functionality requiring PMD to be "resident" in memory. The translation
  replaces residency with a host interface: the FM timer, port I/O, and the
  PPSDRV/PPZ8/P86DRV resident drivers are supplied through `struct pmd_hw` and
  the driver vtables in `pmd.h`. The `pmd-play` utility is one such host (YMFM
  audio output); `test/pmd/trace.c` is another (a recording fake board). In
  the long term, I'd like to find a solution to residency which is as
  cross-platform as possible.

Any other difference in behavior is considered a bug in this translation.

One notable difference in behavior which is _not_ a bug is the handling of
environment variables, which is present in the original programs as well, but
typically not observable when using DOSBox or another emulator as a simple
wrapper. For example, if you don't provide a composer or arranger in MML, it is
likely that your current system username will be used as a default via the
`USER` environment variable.

## Tests

`zig build test` runs the MC compiler goldens, the PMD unit tests, and the PMD
trace tests. The trace tests compare PMD's console output and its ordered port
write/timer interrupt stream with the shipped 4.8s binaries; see
`test/pmd/README.md` for the case format and for how to regenerate a golden
with `repro.py` (which needs Python and Unicorn, unlike a normal test run).

## License

The copyright on the original PMD source code (from which this is translated)
belongs to KAJA.

The English translated strings are taken from the
[translation](https://drive.google.com/drive/folders/1fSH39Vr97_29tvjni6H7WXJlxH_HmZx0)
by Mana, Pigu and VasteelXolotl.

Anything else in this repository which is produced by me, such as additional
support code and commentary, is dedicated to the public domain.
