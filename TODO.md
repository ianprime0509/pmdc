# PMD port TODO

Outstanding work from `PLAN.md`. Phases 1-8 and the Phase 9 trace harness are
in place; `zig build` and `zig build test` pass for every variant. The items
below are what remains.

## External drivers and data paths

- [ ] **ADPCM sample data and rhythm ROM fixtures.** The board models serve
  registers, the ADPCM-B external RAM check, and the PCM86 card (including
  sample data through the fake P86DRV), but the ADPCM-B RAM is never filled
  with a sample fixture, and rhythm ROM contents are never compared. Add a
  data-memory model to both hosts plus fixtures, following how `EXT.FF`,
  `EFC_EFCT.EFC`, and `PCM86.P86` supply voice, effect, and PCM86 data.

## Trace coverage

- [ ] **Long-run timing traces.** Coincident Timer A/B overflows, tempo and
  timer reprogramming, fade, fast-forward/skip, pause, part masks, and
  effect/music masking. These are the cases where golden comparison beats
  single-tick assertions, and the ones that need the most normalization. The
  `ff`, `fout`, `pause`, and `partmask` actions currently appear only in API
  cases with no module loaded, so they never reach the sequencer.

- [ ] **`pmd_change_main` in the trace harness.** There is no `change` action
  on either host, so the status text and the `/R` shutdown handshake are
  covered only by hand-written expectations in `test/pmd/test_pmd.c`
  (`test_pmd.c:1451`, `:1465`, `:4037`). Add the action to `repro.py` (call the
  resident COM through the change path) and to `trace.c`, then compare the
  console text and the shutdown writes against the original.

- [ ] **Reference-driven option/probe matrix.** The checked-in cases exercise
  only `/C`, `/Z`, `/K-`, `/H`, `/M16`, and a `PMDOPT` of `/M /V /E`
  (`test/pmd/cases/pmd/DATA_26K.RUN`, `OPTIONS_26K.RUN`). Add cases for `/M`,
  `/V`, `/E`, `/P`, `/Z`, `/S`, `/K` (including `/KG`, `/KR`, `/KE`), `/R`,
  `/O`, `/W`, `/G`, `/A`, `/DF`-`/DR`, `/I`, `/N`, `/F`, `/HZ`, and their
  `PMDOPT` and command-line precedence forms, for each variant.

- [ ] **Remaining API actions.** Add the `musname` action planned in
  `PLAN.md` (music-name bytes recorded in the `.LOG`) and exercise the
  `setfv` verb, which exists in `trace.c` but appears in no case. Add an action
  for `pmd_set_ppsdrv_flag` so the setter is compared against the original too.

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
