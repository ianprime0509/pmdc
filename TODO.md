# PMD port TODO

Outstanding work from `PLAN.md`. Phases 1-8 and the Phase 9 trace harness are
in place; `zig build` and `zig build test` pass for every variant. The items
below are what remains.

## Trace coverage

- [ ] **`Board: 86` reference board (PC-88).** No shipped binary targets it;
  lowest priority. Add the model only if a case needs it.

## Tooling

- [ ] **Sanitizer runs.** Run the unit and trace tests under ASan/UBSan. The
  build enables no C sanitizer today (`build.zig` only disables it for
  miniaudio at `build.zig:296`).

- [ ] **Coverage build.** Add a coverage configuration (clang/LLVM or the Zig
  equivalent) and use it to find command bytes, envelopes, LFO modes, and
  driver branches that no test reaches.

- [ ] **Differential fuzzing.** Generate structurally valid `.M` streams from
  the command length tables and run a sampled corpus against the reference
  binaries. Keep the target bounded (tick limits, no unbounded loops) and
  triage mismatches into ordinary golden cases.

## Maintenance

- [ ] Re-review the port for `TODO`/omission comments and confirm each one
  names its original line and the reason (`src/pmd/pmd.c:642`, `:9308`,
  `:9410-9458`, `:10318-10396`, `src/pmd/pmd.h:483`).

- [ ] Review the port and ensure no references remain to the plan or other
  intermediate `pmd` branch states.
