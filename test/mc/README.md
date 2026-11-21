# MC harness-based tests

Runs the compiled MC executable against a directory of test cases. Each test
case is an MML file to be compiled, and the file must begin with comment lines:

- `; Output: TEST.M` - defines the output file name to compare against
- `; Console: TEST.OUT` - defines the console output to compare against

With [DOSBox-X](https://dosbox-x.com/) installed, use `repro.sh` to take an
input MML file and produce the true expected output by running the original
MC.EXE on it.
