# pmdc

This is a C translation of the [Professional Music Driver
(PMD)](http://www5.airnet.ne.jp/kajapon/tool.html) version 4.8s source code,
originally written by [Masahiro Kajihara (KAJA)](https://x.com/kajaponn).
Currently, only MC.EXE has been translated, and it is highly likely that some
bugs remain from the translation process.

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
Currently, it requires Zig 0.14. Running `zig build` will compile all available
programs into `zig-out/bin`.

By default, the output will contain the original Japanese messages for usage
information, errors, etc. To compile with English messages, run
`zig build -Dlang=en` instead.

## Usage

This translation is intended to behave in exactly the same way as the original
programs, including bugs and quirks, with a few exceptions:

- Program output printed to the console is encoded in UTF-8 rather than Shift
  JIS so that it will display correctly on modern systems.
- Functionality specific to DOS can't be implemented as-is, notably, any
  functionality requiring PMD to be "resident" in memory. In the long term, I'd
  like to find a solution to this which is as cross-platform as possible
  (although it's a moot point currently when the only available program is
  MC.EXE).

Any other difference in behavior is considered a bug in this translation.

One notable difference in behavior which is _not_ a bug is the handling of
environment variables, which is present in the original programs as well, but
typically not observable when using DOSBox or another emulator as a simple
wrapper. For example, if you don't provide a composer or arranger in MML, it is
likely that your current system username will be used as a default via the
`USER` environment variable.

## License

The copyright on the original PMD source code (from which this is translated)
belongs to KAJA.

The English translated strings are taken from the
[translation](https://drive.google.com/drive/folders/1fSH39Vr97_29tvjni6H7WXJlxH_HmZx0)
by Mana, Pigu and VasteelXolotl.

Anything else in this repository which is produced by me, such as additional
support code and commentary, is dedicated to the public domain.
