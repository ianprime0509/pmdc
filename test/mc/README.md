# MC harness-based tests

Runs the compiled MC executable against a directory of test cases. Each test
case is an MML file to be compiled, and the file must begin with comment lines:

- `; Output: TEST.M` - defines the output file name to compare against
- `; Console: TEST.OUT` - defines the console output to compare against

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
