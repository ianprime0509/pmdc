# MC harness-based tests

Runs the compiled MC executable against a directory of test cases. Each test
case is an MML file to be compiled, and the file must begin with comment lines:

- `; Output: TEST.M, TEST.FF` - defines the output files to compare against
- `; Console: TEST.OUT` - defines the console output to compare against
- `; Extra input: ext/TEST.FF` - additional input files for the compiler
- `; Options: /VW TEST.MML TEST` - command line passed to MC, defaulting to just
  the MML file name

Every file named by `; Output:` must be produced in the run directory and must
match the file of the same name in the cases directory. Files listed by
`; Extra input:` are read from the cases directory and copied into the run
directory under their file name, so seeds for tests can be kept in a
subdirectory (`ext/` holds the external voice files) and cannot be mistaken for
goldens.

Use `repro.py` to take an input MML file and produce the true expected output
by running the original MC.EXE under Unicorn:

```
uv run repro.py ../../original/mc48s cases cases/INPUT.MML
```

The script implements only the DOS services MC uses (console output, file I/O,
and process termination), so it does not need DOSBox-X.

MC requires CRLF line endings in the input MML. `.gitattributes` declares
`*.MML` as `eol=crlf`; make sure the working tree matches that before
regenerating goldens.

Case file names follow the DOS 8.3 convention, so a case directory can be
copied onto a real machine and used as-is.
