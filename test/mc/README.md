# MC harness-based tests

Runs the compiled MC, MCH and EFC executables against a directory of test
cases. Each test case is a source file to be compiled (`.MML`, or `.EML` for
EFC), and the file must begin with comment lines:

- `; Output: TEST.M, TEST.FF` - defines the output files to compare against
- `; Console: TEST.OUT` - defines the console output to compare against
- `; Extra input: ext/TEST.FF` - additional input files for the compiler
- `; Options: /VW TEST.MML TEST` - command line passed to the compiler,
  defaulting to just the MML file name
- `; Command: efc` - the command the case is for, for the cases that do not
  test `mc` itself
- `; Exit status: 1` - the status the compiler is expected to exit with,
  defaulting to 0 and usually omitted

Every file named by `; Output:` must be produced in the run directory and must
match the file of the same name in the cases directory. A case that expects a
nonzero exit status may omit `; Output:` if the compiler fails before writing
any file. Files listed by
`; Extra input:` are read from the case's directory and copied into the run
directory under their file name, so seeds for tests can be kept in a
subdirectory (`ext/` holds the external voice files) and cannot be mistaken for
goldens. A case in a command subdirectory can name a seed shared with the mc
cases as `../ext/NAME`.

`mc` is the primary command and the default one, so the cases in `cases/`
itself test it and must not name a command. The other commands have their own
subdirectory, named after them: `cases/mch` and `cases/efc`. Every case there
must name its command in a `; Command:` line, and the case must use the input
extension that command takes. A header or extension that does not match the
directory fails the case rather than skipping it.

`mch` only compiles and plays, so it cannot save a file: its cases have no
`; Output:` and compare the console output alone. Adding a command means
extending `Command` in `harness.zig` and passing its path from `build.zig`.

Use `repro.py` to take an input source file and produce the true expected
output by running the original binary under Unicorn. The `; Command:` header selects
`MC.EXE`, `MCH.EXE` or `EFC.EXE` from the directory named on the command line:

```
uv run repro.py ../../original/mc48s cases cases/INPUT.MML
uv run repro.py ../../original/mc48s cases/efc cases/efc/INPUT.EML
```

The script implements only the DOS services these commands use (console
output, file I/O, and process termination), so it does not need DOSBox-X.

MC requires CRLF line endings in the input source, and the harness splits the
header comments on them. `.gitattributes` declares `*.MML` and `*.EML` as
`eol=crlf`; make sure the working tree matches that before regenerating
goldens.

Case file names follow the DOS 8.3 convention, so a case directory can be
copied onto a real machine and used as-is.

## EFC cases

EFC compiles an effect MML (`.EML`) into an `.EFC` file of FM/SSG effect data.
Its MML differs from MC's: parts are numbered from 0 rather than lettered, and
effect commands (`n`, `N`) are attached to them. It refuses to compile without
tone data, so either `/V` or a voice file has to be named on the command line;
a `#Option /V` inside the MML is too late, because EFC checks before it parses
the MML.

## MCH cases

MCH is MC's evaluation build: it compiles and then plays instead of writing the
compiled file, so a case can only check what it prints. Without a resident PMD
there is nothing to play, and it reports that after compiling.
