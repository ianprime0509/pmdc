# PMD port plan

This document is the working plan for translating the PMD resident sound driver
to C. It records the source and reference material, the porting conventions, the
architectural decisions made so far, and a phased order in which the translation
should proceed.

The plan is intentionally incremental: each phase should leave the tree
buildable, and later phases should not require rewriting earlier ones.

## Source and reference

### Original source

The source being translated is:

- `original/pmd48s/PMD.ASM` — the PMD resident driver.
- `original/pmd48s/PMD86.ASM`, `PMDB2.ASM`, `PMDPPZ.ASM`, `PMDPPZE.ASM`,
  `PMDVA.ASM`, `PMDVA1.ASM` — variant entry files that set feature flags and
  `include PMD.ASM`.
- `original/pmd48s/PCMDRV.ASM`, `PCMDRVE.ASM`, `PCMDRV86.ASM` — PCM driver
  code included by `PMD.ASM`.
- `original/pmd48s/PPZDRV.ASM` — PPZ8 driver code.
- `original/pmd48s/EFCDRV.ASM` — PSG drum/effect driver code.
- `original/pmd48s/ADRAMCHK.ASM` — ADPCM RAM check.
- `original/pmd48s/WAIT.INC` — FM register wait calibration.
- `original/pmd48s/VIRUS.INC`, `VIRUSCHK.INC`, `VIRUSEND.INC` — virus
  self-check (not translated; see below).
- `original/pmd48s/EFFECT.INC`, `HZDATA.INC`, `TUNEDATA.INC` — data tables
  and effect data.

Line-number comments in the C port refer to `PMD.ASM` unless another file is
named explicitly, e.g. `// :1234` or `// EFCDRV.ASM:1234`.

### Reference implementation

The MC translation is the style reference for this port:

- `src/mc/mc.h`, `src/mc/mc.c` — the main translation.
- `src/mc/mc_main.c`, `src/mc/mc_stdio.c`, `src/mc/mc_stdio.h` — the host-side
  plumbing.
- `src/mc/strings_jp.h`, `src/mc/strings_en.h` — translated strings.
- `build.zig` — build setup, including the per-variant macros and `lang`.
- `test/mc/harness.zig`, `test/mc/cases/` — test harness style.

The PMD port lives in `src/pmd/`:

- `src/pmd/pmd.h` — global state and public interface.
- `src/pmd/pmd.c` — translation of `PMD.ASM` and its includes.
- `src/pmd/pmd_main.c` — CLI host for development.
- `src/pmd/pmd_stdio.c`, `src/pmd/pmd_stdio.h` — C stdlib implementation of
  `pmd_sys` only. Hardware and external drivers are deliberately not
  implemented here: `pmd_hw` is provided by the YMFM backend (phase 7). The
  external-driver vtables are defined with the public interface (phase 2) and
  remain interface-only; their call sites are translated in phase 8.
- `src/pmd/strings_jp.h`, `src/pmd/strings_en.h` — strings.

The MC translation is a reference, not a template to copy blindly: PMD is a
resident, interrupt-driven driver, while MC is a batch compiler. The vtable
shapes and the general code style carry over; the resident lifecycle and the
hardware model do not.

## Style and porting conventions

These conventions are taken from the MC translation and apply to the PMD port.

### Comments

- Every declaration and definition corresponding to a symbol in the original
  source has a line-number comment immediately above it, e.g. `// :8217` or
  `// EFCDRV.ASM:17`.
- For a group of struct fields that appear on adjacent lines in the original and
  form one logical block, only the first field needs the line-number comment.
  Individual fields still get explanatory comments where the original is
  non-obvious.
- Within function definitions, use line-number comments before complex or
  important sections, and before labels that are kept from the original. The
  comments are there to make it possible to follow the original while reading
  the C; they should be frequent enough to preserve that mapping but not
  mechanical.
- When a block of logic is intentionally omitted or impossible to translate
  literally, add a comment at the omission site explaining why. This is how the
  MC port handles DOS-only behavior, and it should be used consistently here.
- Avoid verbose or obvious comments. Comments aside from those described above
  are generally not needed. An example of an exception is comments on fields in
  the `struct pmd` global state, which can be useful if the field name is not
  obvious.

### Source organization

`src/pmd/pmd.c` holds the data, then one block of file-local declarations, then
the definitions:

- Every static function defined below the declaration block is declared there,
  even when its definition already precedes its call sites.
- Declarations and definitions are each ordered by the position of the original
  label: `PMD.ASM` line order, with the code of an included file (`PCMDRV*.ASM`,
  `PPZDRV.ASM`, `EFCDRV.ASM`, `ADRAMCHK.ASM`, ...) placed where its `include`
  line sits in `PMD.ASM`. The two blocks therefore list the same functions in
  the same order.
- New functions are inserted at that position in both blocks, never appended at
  the end of a block; later phases replace a stub in place.
- `pmd_init` stays at the top of the definitions and the lifecycle entry points
  (`pmd_resident_main`, `pmd_resident_cut`) at the end.
- The ordering requirement above is about declarations. Statements inside a
  function need not appear in source order: what matters is that they are
  *executed* in the original's order. A block may therefore be moved within its
  function (inlining the body of a label into the branch that jumps to it, for
  example) as long as the resulting execution order is unchanged. Line-number
  comments keep that traceable: give the moved block the line number of the
  original statement or label it translates, so a reader can follow the logic
  even when it is written somewhere else.

### Names

- Static functions, fields, constants, and labels should match the names of the
  corresponding labels in the original. Examples: `opnint`, `FM_Timer_main`,
  `TimerA_main`, `TimerB_main`, `mmain`, `mstart`, `mstop`, `data_init`,
  `play_init`, `setint`, `settempo_b`, `fm_check`, `port_check`, `wait_check`,
  `part_data_table`, `mmlbuf`, `tondat`, `efcdat`.
- Public entry points use a `pmd_` prefix plus the original label name:
  `pmd_resident_main`, `pmd_change_main`, `pmd_mstart_f`, `pmd_mstop_f`, etc.
  This matches the `mc_` prefixes in `mc.h`.
- Public constants use a `PMD_` prefix and `ALL_CAPS` name.
- Struct field names should match the assembly variable names where
  possible. If an identifier is not valid C, use a minimal, consistent suffix
  and record the original name in its line-number comment.
- Keep the original's numeric constants and their names (`mdata_def`,
  `voice_def`, `effect_def`, `key_def`, `fmvd_init`, the `com_*` constants,
  the `part`/`ongen` values, the `vector_table` data, etc.).

### Logic and control flow

- Follow the source logic and flow as exactly as possible, including bugs. If
  the original has behavior that is clearly a bug but is observable, preserve
  it and mark it with a `BUG:` comment. Do not silently fix the original.
- Prefer structured C control flow. A `goto` is only justified where the
  original's label is genuinely re-entered from several places (a shared
  epilogue or a chained state machine), so that structured C would have to
  duplicate a block or nest several conditions. In particular, a label that
  sits at the end of the function body after a `return`, or a jump that only
  selects between mutually exclusive blocks, is written as an early return or
  an `if`/`else` instead. Where the original uses jumps and a direct
  translation would require a large restructuring, use a strategic `goto` that
  mirrors the original label rather than inventing a different control-flow
  shape. The MC port does this in several places; follow its example.
- Preserve the original's ordering of side effects. This matters especially for
  register writes, interrupt-visible state, and the `si`/`di`-style pointer
  updates. The requirement is on execution order, not on the order the
  statements are written: a block may be moved within the translation of a
  function (see "Source organization") as long as everything still executes in
  the original's order.
- Keep the original's register-oriented state where it is real driver state or
  effects observable behavior, as MC does with `si`/`di`; `partb` is a PMD
  example. Do not preserve `ds_push`, `dx_push`, `ah_push`, or `al_push` merely
  to imitate INT 60h register saving: the typed public functions use ordinary
  arguments, locals, and returns instead.

### Arithmetic and binary accesses

- Multi-byte values in module data and other original binary formats are
  little-endian. Use explicit read/write helpers rather than unaligned casts.
- Model byte/word stores with `uint8_t`/`uint16_t` and explicit truncation.
  Do not rely on C signed overflow, implementation-defined signed shifts, or
  native struct padding to reproduce 16-bit wraparound and partial-register
  behavior.
- Preserve carry, sign-extension, and 8-bit-versus-16-bit operation widths at
  branches and arithmetic where the original flags affect control flow.

### Feature flags and variants

- As in the original, one `pmd.c` is compiled into multiple binaries with
  preprocessor feature flags. The flags are the original's `ifndef` guards:
  `va`, `board2`, `adpcm`, `ademu`, `pcm`, `ppz`, `sync`, `vsync`.
- `pmd.h` must remain independent of these feature flags. It is OK for fields
  to go unused in `struct pmd` because they are not used in a particular
  build of `pmd.c`. In cases where the structure of a type genuinely differs
  based on the feature flags used, and that type needs to be in `pmd.h`, use
  a union.
- Mirror the original's variant matrix in `build.zig`:
  - `pmd` — default (`va=0`, `board2=0`)
  - `pmd86` — `board2=1`, `pcm=1`
  - `pmdb2` — `board2=1`, `adpcm=1`
  - `pmdppz` — `board2=1`, `adpcm=1`, `ppz=1`
  - `pmdppze` — `board2=1`, `adpcm=1`, `ademu=1`, `ppz=1`
  - `pmdva` — `va=1`, `board2=1`, `adpcm=1`
  - `pmdva1` — `va=1`
- The original also defines `_myname`, `resmes`, and `_optnam` per variant.
  `resmes` and `_optnam` are used for the title/usage/status strings and should
  be selected from the variant flags in the strings files so each binary
  reports its own name. `_myname` belongs to the virus check and is only
  relevant if that check is ever restored.
- Keep the same `lang` mechanism as MC: `build.zig` passes a `lang` numeric
  macro, and `pmd.c` includes `strings_jp.h` or `strings_en.h` based on it.
  New translations can be added later without changing the driver.

### Strings

- All user-visible strings live in `strings_jp.h` and `strings_en.h` as
  `static const char ...[]`, with line-number comments grouping them by their
  original location.
- Those two headers hold only strings that appear in the original source, so
  they stay a faithful translation of it. Anything an auxiliary component needs
  on its own (a host backend, a test host, a build wrapper) is localized inside
  that component instead.
- The Japanese file is the source of truth. The English file can initially be a
  copy or partial translation; the build must keep working either way.
- The strings include the original `ver`, `vers`, `verc`, `date`, `resmes`,
  `_optnam`, the `mes_*` usage messages, the `pmderror_mes*` errors, and the
  `changemes_*` status messages. `_myname`/`myname` belong only to the omitted
  virus check and are not needed.
- Keep the string headers `static const`. `ADRAMCHK.ASM` mutates `mes_ongen1` to
  report a YMF288, so `struct pmd` holds a mutable copy of that string
  initialized from a `default_*` entry, as with `mes_int`.

### Memory and data layout

- `struct pmd` holds all global state, following the original data-area order as
  closely as practical for readability.
- In-memory binary compatibility with the original is not a goal. Translate an
  address that the original treats as an absolute pointer into an actual C
  pointer. For address-like values, keep an integer such as `uint16_t` only
  when the original treats the value as an offset relative to another base,
  such as positions and part start offsets within the module data at `mmlbuf`.
  In particular, `part_data_table` becomes an array of `struct pmd_qq *`
  pointers to the part work areas, not a table of 16-bit offsets.
- Inside `struct pmd_qq`, `address` and `partloop` are `uint8_t *` pointers into
  `mmlbuf` (or another buffer in the same `dataarea` image, such as `efcdat`);
  a NULL pointer is the original's null/inactive/no-loop sentinel. Do not bias
  offsets or add integer conversion helpers.
- Store the part work areas in one contiguous `struct pmd_qq` array in the
  original's physical order. Several assembly loops advance from `part1`
  through adjacent work areas, so separate C struct members cannot be used as
  an array. Use named indices/accessors for `part1`, `part2`, etc.; the
  `part_data_table` pointers refer into this array.
- Preserve `qqq`'s aliases into `struct pmd_qq` with anonymous unions or
  accessor functions. For example, `pat` must share storage with `eenv_ar`, not
  become an independent field. The same requirement applies to the other
  old/extended envelope aliases.
- `struct pmd` is pinned because it stores pointers into itself: `mmlbuf` points
  into `dataarea`, `part_data_table` points at `parts`/`part_e`, and the
  `struct pmd_qq` address fields point into `dataarea`. The host must not move
  or copy the `struct pmd` after `pmd_init` (allocate it in place, as the CLI
  and test hosts do).
- The resident data area starts at the original's `dataarea` label. It has a
  one-byte version/system prefix (the `db 0` at `dataarea`) followed by the
  module data (`mmlbuf`). The version byte is not part of `mmldat_lng`: it is
  taken out of the module buffer, so that buffer holds one byte less than its
  configured size. See the architecture decisions below.
- Use `uint8_t` for binary data and `char` for text, as in MC. Where the
  original aliases the same register for both, use an anonymous union or the
  helper accessors, following `mc.h`'s `si`/`sib` and `di`/`dic` pattern.

### Error handling

- Use `pmd_sys.exit` for unrecoverable errors, matching `error_exit` and
  `msdos_exit`. A `longjmp`-based implementation is acceptable and mirrors the
  MC approach; the vtable function is marked `PMDC_NORETURN`.
- Do not add a `resident` flag or an "is this initialized?" state to
  `struct pmd`. The caller controls the lifecycle and is responsible for calling
  `pmd_init`, `pmd_resident_main`, `pmd_change_main`, and the INT 60h functions
  in the correct order.

## Architecture decisions

These are the decisions that are not obvious from reading `pmd.h` and that
should be preserved during the rest of the port.

### Caller-owned lifecycle

PMD is a resident driver, but the C port does not reproduce DOS residency.
Instead, the host owns a `struct pmd` and controls the lifecycle explicitly:

1. The host sets `sys`, `hw`, the external-driver vtables, and `user_data`,
   then calls `pmd_init` to establish the default global state.
2. The host calls `pmd_resident_main` to parse options, probe the hardware (if
   applicable), set up the buffers and the FM timer handler, and return. The
   successful return is the C equivalent of the original's `INT 21h/AH=31h`
   residency transition.
3. The host calls `pmd_change_main` to apply runtime option changes and print
   the status, and calls the INT 60h functions such as `pmd_mstart_f` as needed.
4. The host stops the driver with `pmd_resident_cut`, either directly or via
   the `/R` option to `pmd_change_main`. This synchronously removes the timer
   handler and performs the driver's shutdown sequence. `pmd_change_main`
   returns `PMD_CHANGE_REMOVED` for `/R` and `PMD_CHANGE_OK` otherwise; after
   `PMD_CHANGE_REMOVED`, the host may discard the `struct pmd`. The backend
   must not retain a callback into freed driver state.

Consequences:

- There is no `get_resident` system function. The caller has the `struct pmd`.
- There is no `resident_exit` system function. `pmd_resident_main` returns on
  success. Errors still use `pmd_sys.exit`.
- There is no `resident` flag in `struct pmd`; ordering is the caller's
  responsibility.
- `pmd_change_main` returns `PMD_CHANGE_OK` after printing/applying the
  status, or `PMD_CHANGE_REMOVED` after `/R`. It does not call `exit` on either
  successful path; the caller decides when to terminate or discard the state.
- `pmd_resident_cut` does not free the `struct pmd`. After removal, the host
  must call `pmd_init` and `pmd_resident_main` again before further use.
- `pmd_resident_main` and `pmd_change_main` may only be called after
  `pmd_init`; this is documented in the header, not enforced at runtime.

### Address translation

The original is a 16-bit real-mode program, so many of its variables hold
offsets or segment:offset addresses. Translate each one according to how the
original uses it:

- If the original treats an address as an absolute pointer to an in-memory
  object, make it a C pointer. This covers `struct pmd_qq.address` and
  `partloop`, `part_data_table`, `mmlbuf`, `tondat`, `efcdat`, `prgdat_adr`,
  `radtbl`, `rhyadr`, `ssgefcdat`, and the other in-memory references.
- If the original treats the value as an offset relative to some base, keep it
  as an integer, usually `uint16_t`. Examples are note positions and part start
  offsets within `mmlbuf`, offsets within the PCM data areas, and the ADPCM
  voice table's start/stop offsets.
- Scalar fields of an external data format keep the format's natural width;
  that is independent of address translation.
- A null absolute address becomes a NULL pointer. Do not bias integer offsets
  to reserve a sentinel, and do not add pointer/offset conversion helpers
  unless an external data format or vtable genuinely requires one.

This rule applies to new work as well: before choosing an integer type for an
address-like field, check whether the original uses the value as an absolute
pointer or as an offset from some base.

### Resident memory

The original's resident data is the program's own COM segment, not a separately
allocated block. `dataarea` is the start of the resident data; `mmlbuf` is
`dataarea + 1`; `tondat` and `efcdat` follow the music and tone buffers. The
`resident_size` calculation is a DOS bookkeeping detail and is not reproduced.

In the port:

- `struct pmd` contains the `dataarea` array and the `mmlbuf`/`tondat`/`efcdat`
  pointers. The host must keep the `struct pmd` alive for as long as the driver
  is in use.
- `dataarea[0]` is the version/system byte (the original `db 0`, read as
  `-1[mmlbuf]` by `play_init`). `mmlbuf = dataarea + 1`.
- The initial data area is the empty module image:

  ```text
  dataarea[0]     = 0x00
  dataarea[1..24] = 0x18 0x00 repeated 12 times  (empty part offsets)
  dataarea[25]    = 0x80                         (end of empty part data)
  ```

  `pmd_init` should install this, set `mmlbuf = dataarea + 1`, and set the
  default `mmldat_lng`/`voicedat_lng`/`effecdat_lng` (16/8/4 KiB).
  `pmd_resident_main` updates `tondat`/`efcdat` after parsing `/M`, `/V`, `/E`.
- `tondat` and `efcdat` are computed from `dataarea`, not from `mmlbuf`
  (`PMD.ASM:8642-8658`): `tondat = dataarea + mmldat_lng * 1024` and
  `efcdat = tondat + voicedat_lng * 1024`. The version byte therefore comes out
  of the module buffer, which holds `mmldat_lng * 1024 - 1` bytes, while the
  voice and effect buffers hold their full configured sizes.
- `dataarea` is `40 * 1024` bytes: the maximum sum of
  `mmldat_lng + voicedat_lng + effecdat_lng` is 40 KiB (`PMD.ASM:8582` rejects
  a larger sum), and that sum covers the whole resident data area including the
  version byte. The largest allowed configuration ends exactly at the end of
  `dataarea`.
- The original's `resident_size` bookkeeping stays a DOS detail, but its
  *purpose* is still needed: `memchk_init` (`PMD.ASM:8604`) must reject a
  configuration that does not fit in `dataarea`. The original compares the
  resident size against the largest MCB block; the port compares
  `mmldat_lng + voicedat_lng + effecdat_lng` in paragraphs against
  `sizeof(dataarea) / 16` and reports `pmderror_mes7` on overflow. This is not
  optional. `PMD.ASM:8582` only checks the sum when the command line is
  non-empty (`jz resmes_set` skips it), while `PMDOPT` can ask for buffers that
  do not fit; without the `dataarea` bound, `tondat`/`efcdat` would point past
  the array and `resident_exit_end` would clear memory outside the struct. The
  host still owns the allocation; this check is a bounds check, not a
  `resident_size` reproduction.

### Hardware abstraction

`pmd_hw` exposes only:

- `inb`/`outb` for port I/O.
- `install_fm_timer_handler` for the host's FM interrupt/timer source.
- `void (*remove_fm_timer_handler)(void *user_data)`. This synchronously stops
  future callbacks and waits for any active callback to finish, making it safe
  for `pmd_resident_cut` to proceed. It is harmless when no handler is
  installed.
- `uint8_t (*read_key_state)(void *user_data)`, an optional normalized keyboard
  snapshot for the ESC/GRPH logic. A NULL hook means no keys are pressed. The
  result uses `PMD_KEY_SHIFT = 01h`, `PMD_KEY_CAPS = 02h`,
  `PMD_KEY_KANA = 04h`, `PMD_KEY_GRPH = 08h`, `PMD_KEY_CTRL = 10h`, and
  `PMD_KEY_ESC = 20h`; the driver translates these bits to the original PC-98
  or VA representations rather than exposing BIOS addresses or VA ports to the
  host.

The complete `pmd_sys` and `pmd_hw` interfaces are fixed before driver
implementation begins. Later phases implement these contracts rather than
adding new system or hardware hooks.

There are deliberately no `fm_*` helpers. `opnset44`, `opnset46`, `sel44`,
`sel46`, and `get07` are all `rdychk` + `outb` + `_wait` + `inb` on the board's
ports; they remain driver functions. `port_check` and `fm_check` are driver
functions built on `inb`/`outb`; they are not part of the vtable.

The reference `pmd_hw` implementation is the YMFM backend
(`src/pmd/pmd_ymfm.*`, phase 7). `pmd_stdio` implements only `pmd_sys`; it
provides neither `pmd_hw` nor any external-driver implementation.

`rdychk` is a wrapper around `inb` that spins while bit 7 (BUSY) of the status
port is set. It is a chip handshake, not a delay, and should be implemented
directly on `inb`.

`_wait`, `_waitP`, and `_rwait` are calibrated CPU delay loops. They are not
hardware operations and are not implemented. The stubs in `pmd.c` remain,
with comments explaining that `WAIT.INC` calibration is intentionally omitted.
`wait_check` sets both `wait_clock` and `wait1_clock` to 1 rather than 0:
`ADRAMCHK.ASM` has raw `loop $` uses of `wait_clock`, and zero would mean
65,536 iterations under 8086 `LOOP` semantics. No delay or calibration is
performed. `wait_check` is only called from `fm_check`'s board-present path, as
in the original, so the status output reports these placeholder values only
after a board was found; the no-board path reports whatever the fields held
before. A future real-hardware delay implementation would be a separate scope
change, not another hook added to the completed vtables.

### Initialization

`pmd_init` initializes exactly what the original initializes, and nothing else.
Fields the original leaves indeterminate stay indeterminate in the port: it has
no blanket clear, no zeroing of fields the original never stores to, and no
defensive defaults for values that are only meaningful after a later stage ran
(`wait_clock`/`wait1_clock` until `fm_check`, `board` until `pmd_resident_main`,
`int_level` until `int_init`, and so on). Do not add initialization logic the
original does not have, even when it looks helpful: it would hide lateness bugs
and make the port's observable state diverge from the original.

The consequence for hosts is that a freshly initialized `struct pmd` is only
partly defined. Hosts that need deterministic status output, and tests that
compare against transcribed golden text, must seed the fields the original
never writes rather than relying on `pmd_init` to define them; a `malloc`-ed
`struct pmd` holds indeterminate bytes in those fields.

### Interrupt/timer handling

The host owns the FM interrupt/timer source. `pmd_resident_main` installs the
driver's handler through `install_fm_timer_handler`; the host calls it when the
FM IRQ fires.

The driver-side handler is the original `opnint` core:

- `int5_flag` nesting counter. Increment/decrement it as in the original; do
  not invent an early-return reentrancy guard. The host must still avoid
  reentrant calls because the original private-stack mechanism is omitted.
- PCM86 FIFO check and PPZ8 dispatch (omitted initially; see the phase plan).
- FM status check and `FM_Timer_main`.
- `FM_Timer_main` reads/acknowledges the timer flags and dispatches
  `TimerA_main` and/or `TimerB_main`.
- The `set_fm_int`/`set_efc_int` user callbacks remain in the driver
  (`intfook_flag`, `fmint_ofs`/`efcint_ofs` become C function pointers plus
  user data).

The following parts of `opnint` are intentionally omitted, with comments at the
omission sites:

- 8259 PIC mask/unmask and EOI (`mask_adr`, `mask_data`, `mask_data2`,
  `eoi_adr`, `eoi_data`, `eoi_send`).
- `intset`/`intpop` and `maskpush`/old-handler chaining (`int5ofs`).
- The private stack switch (`_stack`, `ss_push`/`sp_push`).
- DOS IRQ-vector installation and the PIC-specific parts of `int_init`
  (`vector` and the vector offsets in `vector_table`). The host's
  `install_fm_timer_handler` owns the actual vector/source. Retain the portable
  `int_init` work: the register `0Eh` IRQ-level read, `int_num`/`int_level`
  metadata, chip/status messages, and PPSDRV/PPZ8 setup. `int_level` is also
  needed by the PPZ8 `0410h` handshake, and `int_num` is user-visible.
- The `cli`/`sti` pair around `FM_Timer_main`; `disint` remains as a field for
  the `/I` option, but it does not control host interrupts. The host is
  responsible for not calling the handler reentrantly.

`TimerA_main` and `TimerB_main` must keep the original's `music_flag`
start/stop handshake and the `TimerAflag`/`TimerBflag` guards. These are driver
semantics, not interrupt-controller semantics.

A `struct pmd` is not internally thread-safe. The host must serialize timer
callbacks with resident/change/INT 60h calls and provide the exclusion that the
original obtained through `cli` and PIC masking, notably around `ff_music`.
This does not prohibit a user callback from issuing an INT 60h command that the
original's `reint_chk` table explicitly permits from that timer context.

### Omitted functionality

The following are intentionally not translated, with comments marking them:

- **Virus check** (`VIRUS.INC`, `VIRUSCHK.INC`, `VIRUSEND.INC`). It depends on
  DOS internal structures (List of Lists, InFCB records, DOS version) and on the
  on-disk image layout. Omit both original inclusion sites (`virus.inc` at
  `PMD.ASM:8419` and `viruschk.inc` at `PMD.ASM:10848`) with comments. No
  `_myname`/`myname` symbol is needed.
- **`_wait` calibration** (`WAIT.INC`). Keep the no-delay stubs, set both wait
  fields to 1 as explained above, and omit PIT/timer calibration with comments.
- **PIC/EOI and DOS vector installation**, as described above. Do not omit the
  portable `int_init` chip-metadata and external-driver setup along with them.
- **EPSON ROM/BIOS identification reads** at `0FD80:0002/0004` in
  `port_check`. A non-PC-98 host reports `epson_flag = 0`; leave a comment at
  each omitted absolute-memory read. A future real-PC-98 backend can provide
  this through a narrowly scoped hook if needed.
- **`debug`/`debug2`/`debug_pcm` VRAM writes**. These are never invoked in the
  original build; do not translate the macros.
- **`resident_size`/MCB/PSP/environment-block manipulation**. The host owns
  memory; only the `PMDOPT` environment variable is still needed, via
  `pmd_sys.getenv`.
- **The INT 60h register dispatcher**. Translate its functionality into the
  typed public functions, not a DOS-like `int60_main` interface. The original
  tables guide coverage and reentrancy behavior; `int60_result` is replaced by
  typed error returns.
- **The `open_work` marker and the resident check that reads it**. The check
  (`PMD.ASM:8437-8452`) compares the resident's `board` against `open_work` to
  decide between `change_main` and a fresh `resident_main`; the caller-owned
  lifecycle decides that directly, so the marker has no consumer.
- **`ss_push`/`sp_push` stack switching**. The host callback stack is used.

### `pmd_change_main`

The original `change_main` runs in a second process, writes to the resident
segment through `ES`, reads the resident's `board` flag through the
`int 60h`/`part_data_table - 2` trick, prints the status, and exits.

In the port,
`enum pmd_change_result pmd_change_main(struct pmd *pmd, char *cmdline)`:

- operates on the same `struct pmd` as `pmd_resident_main`;
- parses `status_option` (the runtime-changeable subset of options);
- applies the changes to the `struct pmd` fields;
- for `/R`, calls `pmd_resident_cut` and returns `PMD_CHANGE_REMOVED` without
  printing the normal status;
- otherwise prints the status using `put8`, `print_16`, and the
  `changemes_*` strings and returns `PMD_CHANGE_OK`;
- does not call `pmd_sys.exit` on either successful path.

There is no need to emulate `int 60h` for this path.

### `pmd_resident_cut`

`pmd_resident_cut(struct pmd *pmd)` translates `resident_cut_main`/`rescut_main`
without freeing memory or restoring DOS vectors:

- Reject removal with `cantcut_mes` and `pmd_sys.exit(1)` when `rescut_cant`
  is nonzero.
- Run with host-side exclusion and, when `hw` is non-NULL, call
  `remove_fm_timer_handler` and rely on its synchronous return before touching
  state that a callback could access. The NULL-hardware/no-board path needs no
  vtable dereference.
- Stop active FM/PSG effects and music, reset the FM timer register as in
  `vector_ret`, and release PPZ8 removal protection with command `1900h` when
  applicable. Omit only PIC/vector restoration and DOS memory release.
- Print `mes_cut` and return. The host still owns the unchanged allocation and
  receives removal explicitly through its direct call or `PMD_CHANGE_REMOVED`;
  no `resident` flag is added.

### Other driver interfaces

The external resident drivers are represented by separate vtables, not by one
combined `pmd_drv`. Define the complete vtable contracts in phase 2, before
the resident setup and sequencer phases need to call them:

- `pmd_drv_pps` — PPSDRV (INT 64h), used by `EFCDRV.ASM` and `PMD.ASM`.
- `pmd_drv_ppz8` — PPZ8 (INT 7Fh), used by `PMD.ASM`, `PPZDRV.ASM`, and
  `PCMDRVE.ASM`.
- `pmd_drv_p86` — P86DRV (INT 65h), used by `PCMDRV86.ASM`.

As with `pmd_sys`, `pmd_hw`, and the public INT 60h functions, expose separate,
named C functions for each underlying operation with typed arguments and
returns. Do not add a generic interrupt/register dispatcher or a catch-all
`call(uint16_t ax, ...)` interface. For example, PPZ8 IRQ-level negotiation,
its optional direct FM callback, removal protection, keyon/keyoff/stop, pan,
volume, and ADPCM emulation are distinct operations; P86DRV version query,
EMS push/pop, and data-offset lookup are likewise distinct. Document the
original interrupt number and register contract above each function, using
named structs for multi-value returns.

`struct pmd` holds `const struct pmd_drv_pps *pps`,
`const struct pmd_drv_ppz8 *ppz8`, and `const struct pmd_drv_p86 *p86`. A NULL
pointer means that driver is unavailable. The vtable fields are the interface
that a future translation project must implement; this project does not provide
implementations. Availability checks use these pointers, not host-provided
"check" functions. An optional PPZ8 direct-FM-callback field being NULL models
the original fallback to ordinary PPZ8 calls without disabling those calls.

The original's `ppz_call_seg` (`PMD.ASM:8144`) is a segment number that doubles
as PPZ8's availability state, and its `_ppz` macro tests `ppz_call_seg < 2`
before calling the directly obtained FM entry. `struct pmd` keeps that state as
`ppz_call_seg`, because the vtable alone cannot express the three cases the
driver and the status output distinguish: 0 = unavailable, 1 = available
through the ordinary INT 7Fh calls only, 2 = the direct entry from `0409h` was
obtained and is usable through `pmd_drv_ppz8.fm_int`. Only the segment number
itself is dropped; the value is a state sentinel, not an address.

These are interfaces to other resident programs, not hardware. They remain
separate from `pmd_hw`.

## Phased plan

Each phase should end with a buildable tree. Build with `zig build` for all
variants and `zig build -Dlang=en` to keep the translation unit honest.

### Phase 1: Scaffold, build, and strings

**Goal:** A buildable skeleton for all PMD binaries, with the feature flags and
strings in place.

Work items:

- Add `addPmd` to `build.zig`, following `addMc`:
  - C11 with `-Wall -Wextra -pedantic`.
  - `link_libc`.
  - `lang` macro.
  - One target per variant (`pmd`, `pmd86`, `pmdb2`, `pmdppz`, `pmdppze`,
    `pmdva`, `pmdva1`) with the appropriate `addCMacro` flags.
  - Sources initially: `src/pmd/pmd.c`, `src/pmd/pmd_main.c`,
    `src/pmd/pmd_stdio.c`.
- Finalize the `pmd_sys` and `pmd_hw` declarations in `pmd.h` before writing
  driver logic. This includes `remove_fm_timer_handler` and `read_key_state`
  with the contracts above; neither vtable may gain fields in a later phase.
- Add `src/pmd/strings_jp.h` and `src/pmd/strings_en.h` with the original
  `ver`/`vers`/`verc`/`date`/`resmes`/`_optnam`, the usage/error/status
  messages, and the `lang` selection in `pmd.c`. The English file may start as
  a copy of the Japanese file.
- Define the feature-flag defaults at the top of `pmd.c` using the original
  `ifndef` pattern.
- Add `src/pmd/pmd_stdio.c`/`.h` as a `pmd_sys`-only implementation:
  `putc`/`print`/`exit`/`getenv`. Do not add `inb`/`outb` or external-driver
  stubs here; those are not standard-library functionality.
- Add `src/pmd/pmd_main.c` as a minimal CLI host. It sets
  `sys = &pmd_sys_stdio`, `hw = NULL`, `pps = NULL`, `ppz8 = NULL`,
  `p86 = NULL`, calls `pmd_init` and `pmd_resident_main`, and is limited to the
  option/usage and no-board paths until the YMFM phase. The driver must treat a
  NULL `hw` as "no board" and NULL external-driver pointers as unavailable.
- Keep `pmd_init`, `pmd_resident_main`, and `pmd_change_main` as empty stubs
  that do nothing. `pmd_init` cannot be filled in yet because the global fields
  in `struct pmd` are not defined until phase 2.

Acceptance criteria:

- `zig build` builds all variants.
- `zig build -Dlang=en` builds all variants.
- `pmd_sys` and `pmd_hw` contain no placeholder fields and cover every host
  interaction used by the later phases, including teardown and keyboard input.
- Each binary can be invoked without crashing; the usage/status path can be
  reached once phase 3 lands.

### Phase 2: Complete `pmd.h`

**Goal:** The full global state and the full public interface, matching the
original data area and the INT 60h function table.

Work items:

- Fill `struct pmd` with the original global fields, in the original data-area
  order, with line-number comments. Include at least:
  - Global fields: `mmlbuf`, `tondat`, `efcdat`, `fm1_port1/2`,
    `fm2_port1/2`, `fm_port1/2`, `fmint_*`, `efcint_*`, `prgdat_adr`,
    `radtbl`, `rhyadr`, `rhythmmask`, `board`, `key_check`, the volume/down
    fields, `status`, `status2`, `tempo_d`, `fadeout_*`, `syousetu*`,
    `TimerAflag`, `TimerBflag`, `int60flag`, `partb`, `ongen`, the contiguous
    part-work array, `part_data_table`, and `dataarea`.
  - The `mmldat_lng`, `voicedat_lng`, `effecdat_lng` and the other option
    fields.
  - `dataarea[40 * 1024]`, `mmlbuf`, `tondat`, `efcdat`.
  - The `pmd_sys`/`pmd_hw`/`pmd_drv_pps`/`pmd_drv_ppz8`/`pmd_drv_p86`
    pointers and `user_data` (`sys`, `hw`, `pps`, `ppz8`, `p86`).
  - `int8_t port_sel` (the original uses `-1` for auto), `opn_0eh` (the
    `0Eh` interrupt-number bits `int_init` reads and `opninit_loop` compares
    against), and the `ppz_call_seg` availability state described under "Other
    driver interfaces".
  - The FM/SSG/PCM/RHYTHM/PPZ volume and mask fields used by `pmd_change_main`.
- Define the complete `pmd_drv_pps`, `pmd_drv_ppz8`, and `pmd_drv_p86`
  vtables now, including `user_data` arguments and named, typed operations for
  every command used by `EFCDRV.ASM`, `PMD.ASM`, `PPZDRV.ASM`, `PCMDRVE.ASM`,
  and `PCMDRV86.ASM`. This includes PPZ8 `0410h`/`0409h`/`1901h`, keyon/
  keyoff/stop and ADPCM emulation, and P86DRV version/EMS/data-offset calls.
  Follow the named-function rule above rather than adding interrupt-style
  dispatchers. Later phases must not redefine these interfaces.
- Translate `EFFECT.INC` as the static `efftbl`/`efftblend` data now, so
  `pmd_init` can initialize `ssgefcdat`/`ssgefclen` without a later rewrite.
  Also translate `HZDATA.INC` and `TUNEDATA.INC`; keep the default
  `pcm_tune_data` as per-instance mutable storage and point `p86_freqtable` at
  it for PCM86 builds so `/HZ` needs no later redesign. The code that interprets
  these tables remains in phases 3 and 8.
- Define `struct pmd_qq` using the original field names and natural C types.
  `address` and `partloop` are `uint8_t *` pointers into `mmlbuf`; other
  in-memory references become pointers too. Preserve the `qqq` envelope
  aliases, and store the part work areas in the required contiguous array.
- Change `part_data_table` to an array of `struct pmd_qq *` pointers, with one
  entry per original table entry (including the `board2` and `ppz` conditional
  entries). Initialize the pointers in `pmd_init`, after the `struct pmd` is
  allocated. The INT 60h `GET_WORKADR` function returns this pointer array.
- Define `enum pmd_change_result` with `PMD_CHANGE_OK` and
  `PMD_CHANGE_REMOVED`, change `pmd_change_main` to return it, and declare
  `void pmd_resident_cut(struct pmd *pmd)` with the lifecycle contract above.
- Define a typed `pmd_pcm_table` for each variant's logical `pcm_table` fields,
  with documented array counts and C pointers instead of segment/offset pairs.
  A variant has at most one of the two tables and `struct pmd` has a fixed
  layout, so define `pmd_pcm_table` as a union of one struct per table (the
  YM2608 ADPCM table and the PCM86 table) and keep only the matching member
  meaningful. `pmd_get_pcm_adr` returns `union pmd_pcm_table *` (or NULL in a
  variant with no PCM table). Do not expose an undocumented byte pointer or
  rely on the original packed layout; provide explicit little-endian
  serialization helpers if a consumer needs the original external format.
- Declare all INT 60h functions with signatures derived from their input/output
  registers. Multi-value register returns are represented as named structs
  rather than packed integers; addresses are represented as pointers. Define
  the packed-return structs in `pmd.h`:

  ```c
  struct pmd_status {
      uint8_t status;       // AH
      uint8_t status2;      // AL
  };
  struct pmd_drv_info {
      uint8_t board;        // AL; 255 when no board is present
      uint8_t version;      // AH and DH (`vers`)
      uint8_t version_char; // DL (`verc`)
  };
  struct pmd_psgefcnum {
      uint8_t psgefcnum;    // AH
      uint8_t effon;        // AL
  };
  struct pmd_joy {
      uint8_t joystick1;    // AL
      uint8_t joystick2;    // AH
  };
  struct pmd_size {
      uint8_t music;        // AL
      uint8_t voice;        // AH
      uint8_t effect;       // DL
  };
  struct pmd_int_handler {
      void (*fn)(void *);
      void *user_data;
  };
  ```

  Function examples:

  - `bool pmd_mstart_f(struct pmd *pmd);` (`false` when the original sets
    `ah_push` to FFh because a timer is running, `true` otherwise)
  - `bool pmd_mstop_f(struct pmd *pmd);` (the same busy/success contract as
    `pmd_mstart_f`)
  - `void pmd_fout(struct pmd *pmd, uint8_t speed);` (AL)
  - `void pmd_eff_on(struct pmd *pmd, uint8_t efcnum);` (AL)
  - `void pmd_effoff(struct pmd *pmd);`
  - `uint16_t pmd_get_ss(struct pmd *pmd);` (AX; a single 16-bit value)
  - `uint8_t *pmd_get_musdat_adr(struct pmd *pmd);` (returns `dataarea`,
    including the version byte at `mmlbuf[-1]`, not `mmlbuf` itself)
  - `uint8_t *pmd_get_tondat_adr(struct pmd *pmd);`
  - `uint8_t *pmd_get_efcdat_adr(struct pmd *pmd);`
  - `struct pmd_qq **pmd_get_workadr(struct pmd *pmd);`
  - `uint8_t pmd_get_fv(struct pmd *pmd);` (AL)
  - `struct pmd_drv_info pmd_drv_chk(struct pmd *pmd);` (AL, AH, and DX)
  - `struct pmd_status pmd_get_status(struct pmd *pmd);`
  - `void pmd_fm_effect_on(struct pmd *pmd, uint8_t efcnum);` (AL)
  - `void pmd_fm_effect_off(struct pmd *pmd);`
  - `union pmd_pcm_table *pmd_get_pcm_adr(struct pmd *pmd);`
  - `void pmd_pcm_effect(struct pmd *pmd, uint8_t efcnum, uint16_t deltan,
    uint8_t pan, uint8_t vol);` (AL = efcnum, DX = DeltaN, CH = pan, CL = volume)
  - `uint8_t pmd_get_fmefc_num(struct pmd *pmd);` (AL)
  - `uint8_t pmd_get_pcmefc_num(struct pmd *pmd);` (AL)
  - `void pmd_set_fm_int(struct pmd *pmd, struct pmd_int_handler handler);`
  - `void pmd_set_efc_int(struct pmd *pmd, struct pmd_int_handler handler);`
  - `struct pmd_psgefcnum pmd_get_psgefcnum(struct pmd *pmd);`
  - `struct pmd_joy pmd_get_joy(struct pmd *pmd);`
  - `uint8_t pmd_get_ppsdrv_flag(struct pmd *pmd);` (AL)
  - `void pmd_set_ppsdrv_flag(struct pmd *pmd, uint8_t flag);` (AL)
  - `void pmd_set_fv(struct pmd *pmd, uint8_t vol);` (AL)
  - `void pmd_pause_on(struct pmd *pmd);`
  - `void pmd_pause_off(struct pmd *pmd);`
  - `uint8_t pmd_ff_music(struct pmd *pmd, uint16_t bar);` (DX input; AL is
    0 = success, 1 = music ended before the requested bar, 2 = music stopped)
  - `uint8_t *pmd_get_memo(struct pmd *pmd, uint8_t n);` (AL = memo number)
  - `void pmd_part_mask(struct pmd *pmd, uint8_t part);` (AL)
  - `struct pmd_int_handler pmd_get_fm_int(struct pmd *pmd);` and
    `pmd_get_efc_int` (return the current callback and user data, or a
    zeroed struct)
  - `char *pmd_get_mus_name(struct pmd *pmd);`
  - `struct pmd_size pmd_get_size(struct pmd *pmd);`
  - `void pmd_opnint_sub(struct pmd *pmd);` (`sync` builds only; the typed
    equivalent of the special AH=FFh entry)

  There is deliberately no `int60_main`/`int60_jumptable` dispatcher or
  register-based public API. Each function above implements one table entry,
  using shared helpers where useful for `int60flag` and `reint_chk` semantics.
  Functions that the original dispatcher could reject return `bool` or a named
  result enum to expose that failure instead of relying on `int60_result`.
  Each declaration documents its board-present and no-board behavior; where
  the original leaves an otherwise meaningless input register unchanged, the
  typed API defines a useful sentinel or `void` result rather than exposing
  arbitrary register contents.

  The exact return types should be refined as each function is translated; the
  header should document the original register contract above each declaration.
- Add the internal declarations needed by the later phases only when they are
  implemented; keep `pmd.h` a public header and put internal helpers in
  `pmd.c` or an internal header if the set grows.
- Implement `pmd_init` now that the global fields exist:
  - Initialize exactly the fields the original explicitly initializes: its
    statically initialized data image and the `resident_main` defaults.
    Everything else stays untouched, as in the original, so the caller-supplied
    `sys`, `hw`, `pps`, `ppz8`, `p86`, and `user_data` (set before `pmd_init`
    in the lifecycle above) are preserved without a blanket clear. Do not add
    initialization the original does not have; see "Initialization" above.
  - Install the empty `dataarea` image (version byte, 12 empty part offsets,
    `0x80`).
  - Set `mmlbuf = dataarea + 1`.
  - Set default `mmldat_lng`/`voicedat_lng`/`effecdat_lng` and compute the
    default `tondat`/`efcdat`.
  - Initialize `part_data_table` with pointers to the part work structures in
    the same `struct pmd`.
  - Set the original default volumes, flags, key settings, `port_sel = -1`,
    `ppsdrv_flag = -1`, `kp_rhythm_flag = -1`, `message_flag = 1`, etc.
    (`PMD.ASM:8471-8567`).
  - `pmd_resident_main` and `pmd_change_main` remain stubs until phase 3.
- Add the `test/pmd` harness and `test-pmd` build step now. Later phases extend
  this harness rather than referring to a test host that does not exist yet.
  Include header-only compilation for each variant's flag set and unit tests
  for initialization, offsets, aliases, and pointer lifetimes.

Acceptance criteria:

- The header compiles on its own for every variant's flag set.
- `zig build test-pmd` runs the initialization and layout tests.
- `pmd_init` initializes the global state and the empty data area, and the
  `part_data_table` pointers refer to the part work structures in the same
  `struct pmd`.
- Do not assert the original DOS/16-bit binary layout. Do assert the
  C-internal part-work contiguity and `qqq` aliasing required by the translated
  algorithms; the public C API remains pointer-based.

### Phase 3: `pmd_resident_main` and `pmd_change_main`

**Goal:** The non-timer control flow, enough to parse options, probe the
hardware, print the status, and return.

Work items:

- Implement `pmd_resident_main`:
  - The command-line and `PMDOPT` parsing (`set_option`/`_set_option`, the
    `resident_option` table, `usage`, `error_exit`).
  - `fm_check`/`port_check` using `inb`/`outb`; the `wait_check` stub sets both
    `wait_clock` and `wait1_clock` to 1, with a comment explaining the omitted
    `WAIT.INC` calibration and why zero is avoided. The `_wait` stubs remain
    no-delay points.
    A NULL `hw` must return "no board" before the first vtable dereference.
  - Translate the low-level `rdychk`, `opnset44`, `opnset46`, `sel44`, `sel46`,
    and `get07` helpers here, since probing and timer code both need them.
    Translate `ADRAMCHK.ASM` for the applicable variants, including the
    YM2608/YMF288 name mutation.
  - After parsing `/M`, `/V`, and `/E`, validate the capacities and update
    `tondat`/`efcdat`. Reproduce `resident_exit_end`'s buffer initialization:
    clear the voice area and initialize the effect area to 128 little-endian
    `0100h` entries followed by `80h`, respecting zero-sized buffers.
  - Translate the portable parts of `int_init`: IRQ metadata and messages plus
    PPSDRV/PPZ8 setup. Availability checks use `pmd->pps != NULL` /
    `pmd->ppz8 != NULL`; PPZ8 negotiation still uses the phase 2 vtable and the
    original `0410h`/`0409h` handshake. No external implementation is required.
  - The `resident_size`/MCB check is replaced by the `dataarea` bounds check
    described under "Resident memory": `memchk_init` must reject a
    configuration that does not fit, because `PMDOPT` alone can request one.
    The host still owns the allocation.
  - Leave `opnint_start` and timer-handler installation for phases 4/5; its
    `data_init`, `opn_init`, `mstop`, and `setint` calls are playback-dependent.
    Omit only DOS vector installation/PIC/EOI, with comments at those sites.
  - Return on success.
- Implement `pmd_change_main`:
  - `status_option` parsing and `set_option`.
  - `put_status` with `put8`/`print_16` and the `changemes_*` strings.
  - Return `PMD_CHANGE_OK` on ordinary success. Route `/R` to
    `pmd_resident_cut` and return `PMD_CHANGE_REMOVED` without printing the
    status, as the `pmd_change_main` contract above specifies. Until phase 6
    fills in `pmd_resident_cut`, that function is a no-op, so
    `PMD_CHANGE_REMOVED` is a forward declaration rather than a report that the
    driver is really down: mark the call site with a phase 6 TODO so no host is
    built against the result before the shutdown sequence exists.
- Implement the shared option machinery (`set_option`, `_set_option`,
  `get_comline_number`, and a `pmd_sys.getenv`-based replacement for
  `search_env`) in `pmd.c`.
- Implement `pmd_sys.getenv` usage for `PMDOPT`; no PSP/environment-segment
  code.
- Extend `test/pmd` with scripted `pmd_sys`/recording `pmd_hw` implementations
  and cases for option precedence/errors, capacity limits, buffer
  initialization, NULL-hardware behavior, and status output. Cover the
  board-present probe branches as well as the no-board path, including the
  PPSDRV/PPZ8 setup in `int_init` (with mock driver vtables) and the re-probe
  `/O` triggers.

Acceptance criteria:

- `zig build test-pmd` covers the option, buffer, probe, and status paths,
  including `int_init`'s PPSDRV/PPZ8 setup and `/O`'s re-probe.
- `pmd -H` prints the original usage text (with the variant-specific
  `resmes`/`_optnam`).
- `pmd` with an invalid option exits with the original error status.
- `pmd_change_main` prints the same status text as the original for a given
  `struct pmd` state.
- No hardware is required; the CLI host's NULL `hw` (or a local null backend)
  causes `fm_check` to report no board, and the code takes the expected
  no-board path. `pmd_stdio` itself provides only `pmd_sys`.

### Phase 4: FM timer core

**Goal:** The core YM2203/YM2608 interrupt and timer logic, without PCM/PPZ8 or
other external drivers.

Work items:

- Implement the `opnint` core:
  - Increment/decrement the `int5_flag` nesting counter without adding an
    early-return guard.
  - FM status check and `FM_Timer_main`.
  - Clear the timer flags and dispatch TimerA/TimerB exactly as the original.
  - The PCM86 FIFO and PPZ8 checks are omitted with TODO comments at their
    original sites (`:7540-7565`); leave the `#if pcm`/`#if ppz` structure in
    place so they can be filled in later.
  - Preserve the `vsync` port-2 stop/restore behavior behind `#if vsync`; it
    is separate from the omitted PIC path. Omit the PIC/EOI,
    `intset`/`intpop`, `int5ofs` chaining, and stack switch with comments.
- Implement `FM_Timer_main`, including the `rdychk`/`27h` read/reset sequence
  and the `ch3mode` write.
- Implement `TimerA_main` and `TimerB_main`:
  - Keep the `TimerAflag`/`TimerBflag` guards and the `music_flag` start/stop
    handshake.
  - `TimerB_main` handles `music_flag` through `mstart`/`mstop`, calls `mmain`,
    `settempo_b`, `syousetu_count`, and updates `lastTimerAtime`; leave explicit
    phase 5 stubs for those five routines. Preserve the `sync` early return and
    the `opnint_sub` entry point.
  - `TimerA_main` calls `fadeout`, `rew`, `effplay`, `fm_efcplay`, and the
    keyboard check; use stubs/TODO comments for the parts that belong to later
    phases.
  - Call the `set_fm_int`/`set_efc_int` user callbacks at the original points
    (`intfook_flag` bits 0/1).
- Implement the timer state (`TimerAtime`, `lastTimerAtime`, `tempo_d`,
  `TimerB_speed`, `music_flag`, `disint`) as needed by the above.
- Add the `install_fm_timer_handler` call in `pmd_resident_main` when a board is
  present. The phase 2 test host's `pmd_hw` stores the handler and can invoke
  it manually; the real YMFM-backed implementation is phase 7. `pmd_stdio`
  does not participate.
- Extend `test/pmd` with TimerA, TimerB, simultaneous-timer, callback,
  reentrancy-state, `sync`, and `vsync` cases using scripted status reads and
  recorded writes.
- Add `BUG:` comments if any original timer quirk is preserved.

Acceptance criteria:

- `zig build test-pmd` runs the timer and callback cases.
- A test host can call the installed handler manually and observe the expected
  timer state changes (e.g. `TimerAtime` increments, `TimerBflag` resets).
- The `disint`/`sti` behavior is documented as host-owned.
- No PIC port writes are required for the handler to run with the test host's
  `pmd_hw` backend.

### Phase 5: Sequencer and playback core

**Goal:** The music engine that the timer and the INT 60h functions drive:
`mstart`, `mstop`, `mmain`, `commands`, the part work, `data_init`,
`play_init`, `setint`, `settempo_b`, `syousetu_count`, `fadeout`, `rew`, and the
core FM/SSG output routines, including `opn_init` and `mstop`. The lower-level
`opnset*`/`sel*` helpers were introduced in phase 3.

Work items:

- Translate the part work layout and `part_data_table` (from phase 2) and the
  `mmain`/`commands`/`data_init`/`play_init` logic.
- Translate the remaining core FM/SSG output routines, including `opn_init`.
- Translate `mstart`/`mstop` and the `play_flag`/`music_flag` transitions.
- Translate `setint`, `settempo_b`, `syousetu_count`, and the tempo fields.
- Complete `opnint_start` with `data_init`, `opn_init`, `mstop`, and `setint`,
  replacing the phase 3/4 stubs while continuing to omit only PIC/vector work.
- Translate `fadeout`/`rew` and the keyboard check (`key_check`, `esc_sp_key`,
  `grph_sp_key`, `rew_sp_key`); the keyboard state source is a host concern and
  should be a small helper (see below).
- Keep the original `BUG:` behavior, and keep the original's `goto`/label
  structure only where the structured form would have to duplicate or nest
  heavily (see "Logic and control flow").
- Extend `test/pmd` with small checked-in module buffers (reusing suitable
  `test/mc/cases/*.M` fixtures where practical), explicit part-state
  expectations, and recorded OPN writes for start/stop, notes, commands, loops,
  tempo, fade, and rewind. Leave PCM/PPZ8-specific call-site stubs marked for
  phase 8, but test the shared command paths now.

Keyboard state: replace the PC-98 BIOS reads at `0000:052Ah`/`0538h` and VA
ports 8/9 with a driver-side helper around the phase 1 `read_key_state` hook.
Translate the normalized bits to the original variant-specific masks, including
ESC separately. A NULL hook returns zero; tests use scripted snapshots to cover
ESC, GRPH, modifier combinations, and VA mapping without changing `pmd_hw`.

Acceptance criteria:

- The driver can start/stop playback with a test music buffer and a null/test
  `pmd_hw` backend without crashing.
- `zig build test-pmd` passes, and the part-work state and tempo/timer
  interactions match the assembly-derived expectations for the tested
  sequences.
- `pmd_mstart_f`/`pmd_mstop_f` work through the core before the remaining
  typed INT 60h functions are completed in phase 6.

### Phase 6: INT 60h resident functions

**Goal:** The full external command surface.

Work items:

- Implement every public `pmd_*` function from phase 2 directly. Do not
  translate `int60_main`, `int60_start`, or either jump table into a dispatcher.
  Use `int60_jumptable`, `n_int60_jumptable`, and `reint_chk` as a checklist for
  functionality, board/no-board behavior, and per-function reentrancy rules.
  Small shared enter/exit helpers are acceptable, but there is no AH/register
  interface. Implement `pmd_opnint_sub` for `sync` builds.
- Return `bool` or a named result where the original could reject a call via
  `int60_result`; typed returns otherwise match the declarations. Each function
  implements and documents its own meaningful no-board result rather than
  relying on unchanged input registers.
- Implement `set_fm_int`/`set_efc_int` with proper C callbacks and preserve
  `intfook_flag`/`rescut_cant` semantics.
- Implement the `pause`, `ff_music`, `part_mask`, `get_memo`, `get_mus_name`,
  and `get_size` paths. The original interrupt register save/restore and `sti`
  behavior are not needed for direct C calls; retain the host-exclusion rules.
- Implement `pmd_resident_cut` and the `/R` branch of `pmd_change_main` using
  synchronous `remove_fm_timer_handler`, the shutdown sequence above, and
  `PMD_CHANGE_REMOVED`. The PSG-effect shutdown call remains an explicit phase
  8 stub until `effoff` is available; all other removal behavior is testable in
  this phase.
- Extend `test/pmd` to call every public function directly in board-present and
  no-board states, covering busy/error returns, callback get/set/clear behavior,
  multi-value returns, and the `sync` entry. Test `/R` result reporting,
  `rescut_cant` rejection, timer removal, and the absence of callbacks after
  removal.

Acceptance criteria:

- `zig build test-pmd` covers every public function and return contract without
  an INT 60h dispatcher.
- Every entry in both original jump tables has a corresponding typed function
  or a documented no-op, and the functions return the documented values for
  representative states.
- The `set_fm_int`/`set_efc_int` callbacks are invoked from the timer path at
  the original points.
- `/R` returns `PMD_CHANGE_REMOVED`, synchronously prevents further timer
  callbacks, and leaves memory ownership with the host.

### Phase 7: YMFM hardware backend

**Goal:** A real `pmd_hw` implementation on top of
[YMFM](https://github.com/aaronsgiles/ymfm) (also available locally at
`~/src/ymfm`), so the driver can be exercised without a physical PC-98 or a
DOS-era sound board. This phase is separate from `pmd_stdio`; `pmd_stdio`
remains `pmd_sys`-only.

YMFM provides YM2203/YM2608 (OPN/OPNA) and YMF288 emulation, including FM,
SSG, and (for the YM2608) ADPCM hardware. It is C++, so the integration needs
a small C ABI wrapper; the driver and `pmd_hw` remain C.

Work items:

- Add the YMFM backend as a separate source file, e.g.
  `src/pmd/pmd_ymfm.c`/`.h` with a small `pmd_ymfm.cpp` wrapper (or a single
  `.cpp` translation unit with an `extern "C"` interface). Do not put any of
  this in `pmd_stdio`.
- Pin YMFM through `build.zig.zon`: `zig fetch --save=ymfm
  https://github.com/aaronsgiles/ymfm#main`. `~/src/ymfm` is a local reference,
  not a build-time dependency. Link the C++ runtime as needed. The backend is
  selected by the CLI/test host; the core `pmd.c` translation does not depend on
  YMFM, and `pmd_stdio` stays independent of it.
- Implement `pmd_hw.inb`/`outb` by mapping the board's `fm1_port1/2` and
  `fm2_port1/2` to the configured YMFM YM2203/YM2608/YMF288 device, including
  the `va` layout.
  Give the backend explicit chip-type and base-port configuration: `ongen` is
  a probe result, and `port_sel` only restricts the driver's port scan; neither
  lets the backend skip probing nor determines the chip on its own.
- Emulate the board-probe reads/writes required by `port_check` and `int_init`
  (`0AAh`, OPNA ID, register `0Eh` IRQ selection, and related board-control
  ports). The configured chip/port/IRQ metadata must agree with what the probe
  reports. Non-chip control-port writes may otherwise be recorded or ignored.
- Implement the YMFM interface's timer, busy, and IRQ hooks, not just register
  I/O. Advance the chip with `generate()` at its documented input clock/sample
  rate, service `ymfm_set_timer` expirations, and call the installed PMD handler
  from `ymfm_update_irq` while IRQ is asserted. Merely storing the handler is
  insufficient because YMFM's default timer/IRQ hooks are no-ops. The driver's
  `opnint` still reads status and dispatches TimerA/TimerB.
- Implement `remove_fm_timer_handler` synchronously and verify that no callback
  can occur after it returns. Implement `read_key_state` where host input is
  available; the simple CLI/headless test backend may leave it NULL.
- Preserve `vsync` port-2 accesses in the recording/backend model. PIC/EOI
  ports are unused by the omitted interrupt-controller path and may be no-ops.
- Back YM2608 ADPCM-B external memory through `ymfm_external_read`/
  `ymfm_external_write`, so `ADRAMCHK.ASM` and ADPCM playback have real RAM to
  access. The YM2608 register map alone is not sufficient. The separate
  PCM86/P86DRV board is not covered by YMFM and has no audio backend in this
  phase (see phase 8).
- Provide ADPCM-A external-data backing for rhythm audio. While initializing
  the YMFM backend, `pmd_main` attempts to open exactly
  `ym2608_adpcm_rom.bin` in the current directory. If present, load it into the
  backend's ADPCM-A store; if absent, print a warning once through `pmd_sys`
  using a message localized in the backend (see Strings), and continue with
  deterministic zero reads/no rhythm sample data. Do not
  search other paths or add file operations to `pmd_sys`. Test both present and
  missing files using a controlled working directory.
- Switch `pmd_main.c` to use an initialized, host-owned YMFM backend once it
  exists: `sys = &pmd_sys_stdio`, `hw = &pmd_hw_ymfm`, and the external-driver
  vtables remain NULL. Keep the original PMD CLI's option/status role; module
  loading, timed sample generation, and audio output belong to a test or
  playback host, not implicitly to `pmd_resident_main`.
- `pmd_stdio` must still contain only the `pmd_sys` implementation after this
  phase.

Acceptance criteria:

- The driver can initialize the emulated YM2203/YM2608/YMF288 and run the
  applicable `opnset44`/`opnset46`/`sel44`/`sel46` paths against it.
- Advancing the YMFM clock reaches `FM_Timer_main` and the TimerA/TimerB
  dispatch through the installed IRQ handler.
- A test host can load a simple module into the driver's buffer, start
  playback, advance the emulated clock, and observe the expected chip state and
  audio/timer activity.
- `zig build test-pmd` includes YMFM probe, ADPCM-RAM, ROM-present/missing,
  timer installation/removal, keyboard, and playback integration cases; phase 9
  expands rather than introduces this coverage.

### Phase 8: Effect, PCM, and PPZ8 driver paths

**Goal:** Translate the remaining included effect/PCM/PPZ8 code and replace
the corresponding stubs from phases 4–6. The separate PPSDRV, PPZ8, and P86DRV
resident programs themselves are not implemented in this project; their
complete interface-only vtables were defined in phase 2.

Work items:

- Translate `EFCDRV.ASM`, using the `EFFECT.INC` data from phase 2 and
  including `eff_on`/`effplay`/`effoff`, SSG drum behavior, and every PPSDRV
  call through `pmd_drv_pps`.
  PPSDRV is used by the default and VA1 builds too; it is not a separate `pps`
  build flag.
- Translate `PPZDRV.ASM`, the PPZ8 portions of `mmain`/commands/part masking,
  and the PPZ8 dispatch in `opnint`. Replace each original `ppz8_call`/INT 7Fh
  site with the appropriate named `pmd_drv_ppz8` operation for `0410h`,
  `0409h`, `1901h`, keyon/keyoff/stop, or ADPCM emulation; do not reproduce the
  generic interrupt call in the vtable.
- Translate `PCMDRV.ASM`, `PCMDRVE.ASM`, and `PCMDRV86.ASM`, including their
  sequencers, effects, PCM table handling, and PPZ8 ADPCM-emulation calls, using
  the phase 2 frequency tables. `ADRAMCHK.ASM` and the low-level OPN helpers
  were already translated in phase 3. Exercise YM2608 and PCM86 register/FIFO
  I/O through `pmd_hw`; do not model hardware as external resident-driver
  calls.
- Replace the PCM86 FIFO stub in `opnint` with `fifo_main`. Translate transfer,
  pan, repeat/release, and `/HZ` behavior. Represent EMS sample locations with
  C pointers or explicit wide offsets rather than emulating 16-bit segment
  wrapping. Route P86DRV version, EMS push/pop, and data-offset operations
  through the named phase 2 `pmd_drv_p86` functions. A real PCM86 audio backend
  remains out of scope; recording `pmd_hw` and mock P86DRV implementations are
  sufficient for verification.
- `struct pmd` holds `const struct pmd_drv_pps *pps`,
  `const struct pmd_drv_ppz8 *ppz8`, and `const struct pmd_drv_p86 *p86`.
  NULL means "not available"; there are no separate host-provided availability
  functions. All call sites must compile and take the original unavailable
  paths with NULL vtables, and tests must also use mock vtables to verify the
  available paths.
- Complete the PSG-effect portion of `pmd_resident_cut` now that `effoff` is
  available, and test the full `/R` shutdown path.
- Add recording/mock-vtable tests for SSG effects, ADPCM, PCM86 FIFO/transfers,
  PPZ8, NULL-driver fallbacks, and the interrupt-dispatch paths translated in
  this phase.

Acceptance criteria:

- No translation stubs remain in the C paths corresponding to `EFCDRV.ASM`,
  `PPZDRV.ASM`, `PCMDRV.ASM`, `PCMDRVE.ASM`, or `PCMDRV86.ASM`; any deliberately
  omitted DOS/host operation has a line-numbered explanation.
- The phase 2 `pmd_drv_pps`/`pmd_drv_ppz8`/`pmd_drv_p86` contracts cover every
  command used as named operations, with the original register contracts
  documented but no interrupt-style dispatcher.
- All variants compile and take the "not available" paths with NULL vtables;
  mock-driver tests verify the enabled paths.
- No implementation of any external resident driver is added to this project.

### Phase 9: Verification, tests, and documentation

Work items:

- Complete the `test/pmd` coverage started in phase 2. Review every golden
  state/trace against the assembly or an independently captured original run;
  do not bless output merely because the new implementation produced it. Wire
  `test-pmd` into the aggregate `zig build test` step.
- Add reference-based golden tests. The original PMD's observable output is not
  a single file or a console transcript: it is the ordered stream of hardware
  and external-driver operations emitted over timer ticks. Express each test as
  a `.RUN` script, capture the stream from the original with `repro.py`, and
  compare it with the same stream recorded through `pmd_hw`, `pmd_drv_pps`,
  `pmd_drv_ppz8`, and `pmd_drv_p86`; see "Trace-driven test scripts".
- Cover install/probe and option behaviour separately from playback. Run each
  original variant with a matrix of `/M`, `/V`, `/E`, `/P`, `/Z`, `/S`, `/K`,
  `/R`, and `PMDOPT` values, and compare the console text and the port
  probe/initialization writes. This replaces hand-transcribed expectations in
  `test_pmd.c` with output from the original. Use `Board: none` or `/C` for
  the cases whose console text must match exactly; see the normalization
  rules.
- Capture the INT 60h API surface: call every function, including the no-board
  path, and record the return values and any returned memory (status, size,
  memo, music name, work addresses, callback/effect numbers, PPSDRV/PPZ8
  flags) as script actions. Report addresses as data-area offsets or pointer
  aliases, not as segment values.
- Use the `.M` fixtures produced by `test/mc/cases` as a ready-made playback
  corpus. Add focused synthetic modules where needed so every command byte,
  envelope, LFO, FM3 mode, rhythm, PCM/ADPCM, and PPZ8 path is reached, and
  include the official `SAMPLE.M`, `SAMPLE2.M`, and `SSGEG_S.M` fixtures.
- Run `zig build test-pmd` for every variant flag set, plus the YMFM integration
  tests and sanitizer-enabled native tests where supported.
- Verify all variants with `zig build` and `zig build -Dlang=en`.
- Update `README.md`: it currently says only MC is translated. Add PMD status
  and usage notes.
- Review the port for any remaining `TODO`/omission comments and make sure each
  one points at the original line and explains the reason.
- Remove the `PMDC_UNUSED` macro from `pmd.h`, as all items marked as unused
  from prior work-in-progress phases should now be used.

#### Reference binaries

The official `pmd48s.zip` from KAJA's site
(`http://www5.airnet.ne.jp/kajapon/dat/pmd48s.zip`) contains the exact reference
binaries for this source release: `PMD.COM`, `PMD86.COM`, `PMDB2.COM`,
`PMDPPZ.COM`, `PMDPPZE.COM`, `PMDVA.COM`, and `PMDVA1.COM`, plus `PMP.COM` and
the sample modules. `PMD.COM` reports `Version 4.8s`, `Jan.22nd 2020`, which
matches `strings_jp.h`, so these are preferable to a locally rebuilt
reference. Store them alongside the existing `original/mc48s/MC.EXE`, or have
`repro.py` accept their directory as an argument, the same way the MC script
accepts the original MC directory.

#### Unicorn capture design

Follow `test/mc/repro.py`: a Python script (`test/pmd/repro.py`, run with
`uv run`) loads the shipped COM under Unicorn in 16-bit real mode and
implements only the DOS and hardware behaviour PMD needs. This removes the
emulator setup from the reproduction path; the only dependency is `unicorn`,
already declared in `test/pyproject.toml`.

- Reuse MC's `DosMachine` shape: fixed-segment load, PSP with the command
  tail, empty environment, `INT 21h` console output and handle-based file I/O,
  and `INT 20h`/`AH=4Ch` exit. PMD additionally needs the services its
  installer uses (vector get/set `AH=25h`/`35h`, memory allocation
  `AH=48h`/`49h`/`4Ah`, the DOS version `AH=30h`, and the TSR exit
  `AH=31h`, which ends the emulation but leaves the resident image in guest
  memory for the rest of the script). Also answer `INT 2Fh` as "not Windows",
  so the calibration skips its blanking wait. Fail loudly on unimplemented
  calls, as the MC script does, instead of silently stubbing them, and grow
  the list from the first run's `unimplemented DOS call` error.
- The reference binaries are `.COM` files, so the loader is simpler than MC's
  MZ loader: copy the image to a fixed load segment, build the command tail
  from the option string, and start at `CS:0100`.
- Model the board, not the machine. Hook `IN`/`OUT` instructions
  (`UC_HOOK_INSN` with `UC_X86_INS_IN`/`UC_X86_INS_OUT`) to serve the
  YM2203/YM2608 register file, status/busy/ID reads, and the ADPCM/PCM86
  ports, and record each write at the current virtual clock. Drop the
  host-owned PIC and PIT ports (`00h`, `02h`, `08h`, `0Ah`, `71h`, `77h`, and
  on the 88VA also `32h` and `184h`-`18Ah`) from the recorded trace; the
  translated driver does not touch them. Raise on
  unimplemented ports or registers so a probe path cannot pass by accident.
- Derive the clock from the timer programming, as the translation's trace host
  does: on writes to registers `24h`/`25h`/`26h`/`27h`, recompute the Timer
  A/B deadlines in integer OPN master clocks (3,993,600 Hz for the YM2203),
  using the same interval and reload rules stated under "Trace-driven test
  scripts". Run `emu_start` in slices bounded by the next deadline and inject
  the corresponding IRQ at the boundary through the installed IVT entry, so
  timer phase and coincident A/B overflows are exact and reproducible. Time
  does not advance for instruction execution, so the measured `WAIT.INC`
  calibration is meaningless here and must not be recorded; see the
  normalization rules.
- Capture flow, driven by the `.RUN` script:
  1. `main`: run the COM with the script's option string, record the console
     output (`AH=09h`/`02h`) and the initialization port writes, and derive
     the resident segment from the INT 60h vector left in the emulated IVT.
  2. `load`: read the fixture into the buffer returned by INT 60h `AH=00h`.
  3. API actions: set `AH` and the other registers, synthesize the interrupt
     frame, and jump to the vector target; record the return registers and
     `mem_read` any returned buffer (status, size, memo, music name, work
     addresses, callback/effect numbers, PPSDRV/PPZ8 flags). Convert pointers
     into data-area offsets or aliases as the trace rules say, not into
     segment values.
  4. `run`: let the virtual timers run, recording each `OUT` and each
     external-driver call, until the script's tick or `status2` bound.
  5. Write the trace and console goldens into `test/pmd/cases/`, converting
     CP932 console bytes to UTF-8 exactly as `test/mc/repro.py` does.
- `repro.py` writes the goldens for the case it is given, the way the MC
  script does, and nothing consumes them at build time.
- Two installer behaviours need explicit fake-DOS support rather than a real
  emulator: the virus check reads and hashes interrupt vectors, and the
  memory sizing walks the MCB chain and the list-of-lists. Build those
  structures directly in the fake memory map and record the values the
  installer reads, so a trace explains any install difference.

#### Trace-driven test scripts

A test case is a script, not a recording: a `.RUN` file that names the actions
to perform, plus its fixtures. `test/pmd/repro.py` executes the script against
the shipped COM under Unicorn; `test/pmd/trace.c` executes the same script
against the translated driver. Each side writes the same two files and the
harness compares them:

- `NAME.OUT`: console output, converted to UTF-8 and compared as in
  `test/mc`.
- `NAME.LOG`: the ordered event trace, compared line by line.

##### Script format

`;` starts a comment. Comment lines at the top of the file are headers:

```
; Driver: pmd
; Board: 26K
; Chip: YM2203
; PMDOPT: /M10
```

- `Driver` selects the reference binary (`pmd` for `PMD.COM`, `pmdva` for
  `PMDVA.COM`, ...) and the translated build the case runs against. Cases are
  grouped under `test/pmd/cases/<driver>/`, and a header that does not match
  its directory is a failure.
- `Board` selects the fake board: `26K` (PC-98, chip at 088h), `86B` (PC-98
  86B, chip at 188h), `VA` (PC-88VA, chips at 044h and 046h), or `none`, which
  answers every probe as absent. `86` (PC-88) is not modelled: no shipped
  binary targets it.
- `Chip` selects the YM2203 (3,993,600 Hz) or the YM2608 (7,987,200 Hz) and
  so the clock unit; it must agree with `Board` and `Driver`. On `Board: VA`
  it also selects whether the OPNA register test succeeds, which is how
  `pmdva` (YM2608) and `pmdva1` (YM2203) differ.
- `PMDOPT` sets the environment variable the installer reads. Optional.

Every other line is one action, in order. The initial set is deliberately
small; add an action only when a test needs it.

```
main /M16
load TEST.M
mstart
status
run b 16
run-until status2 2 max 512
mstop
```

- `main OPTS`: run the resident main with the option string. The reference
  runs the COM to its INT 21h/AH=31h TSR exit; the translation calls
  `pmd_resident_main`. Later actions run against the resident driver.
- `load FILE`: read `FILE` into the address returned by
  `pmd_get_musdat_adr` (in the reference, INT 60h AH=00h). This replaces
  `PMP.COM`; PMP stays a manual smoke test because its output is the
  player's, not the driver's.
- `mstart`, `mstop`: `pmd_mstart_f`, `pmd_mstop_f`.
- `status`: `pmd_get_status`, recorded as `ret status ...`.
- `run b N`: advance virtual time and stop after the N-th Timer B interrupt
  has been handled. The handler's own writes are included.
- `run-until status2 V max N`: advance until an interrupt leaves `status2`
  at `V`, failing if `N` Timer B interrupts pass first. `status2` counts loop
  passes and is 0FFh once the music ends or stops, so this is how a pass, or
  the end of a module, is bounded. Every unbounded wait must be bounded this
  way so a hang is a failure and not a timeout.
- `pause`, `resume`: `pmd_pause_on`, `pmd_pause_off`.

`ff`, `partmask`, `fv`, `fout`, `effon`/`effoff`, and `musname` follow when
their tests land; each is about ten lines per host. `env` should not be
needed while `PMDOPT` is a header.

There is no `assert` verb. Every action that returns something records it as
a `ret` event, so the golden is the assertion; that also covers the
`pmd_get_status`-style checks this design would otherwise need. Add a verb
only when a comparison cannot be an exact value, and prefer normalizing the
value (a data-area offset, a pointer alias) to adding one.

The two hosts deliver actions differently by design: the translation host
calls the typed function, and `repro.py` sets `AH`/`AL`/`DX` and synthesizes
the INT 60h frame. What must match is what the driver does with the action,
not how it arrives, so each action documents the register contract it stands
for, as `pmd.h` already does for the functions.

##### Trace events

One event per line: `<clock> <event> [args...]`. The clock is a decimal
master-clock count; ports, register numbers, and values are hex.

```
0 outb 44 29
0 outb 45 83
0 ret status 00 01
64512 b
73728 a
```

- `outb PORT VALUE`: every `pmd_hw.outb` call, in order, unfiltered. Install
  and probe writes are ordinary `outb` events at clock 0; they need no
  separate event kind.
- `a`, `b`, `ab`: the interrupt the host delivered at this clock: Timer A,
  Timer B, or both flags latched together. `ab` is the tie case and is one
  delivery, never two.
- `ret NAME VALUES`: the result of a scripted action. Numeric results are
  hex values, strings are raw bytes, and pointers appear only as the
  data-area offsets or aliases described under normalization.
- `pps`, `ppz8`, `p86`: external driver calls (named operation plus hex
  arguments), added with those drivers.
- `status1`/`status2` changes are not separate events; `status` and the
  `run-until status2` bound are what read them.

Do not record:

- `inb` reads. `opnset44` reads the status twice before every FM write, so
  logging reads triples the trace, and the values are a function of the
  host's chip model rather than of the driver. Keep a `; Reads: on` debug
  switch that logs them for diagnosis, never for comparison.
- Console text; that is `.OUT`.
- Addresses and segments; see the comparison rules.

The board model that answers those reads must be identical in the two hosts
and small: BUSY always clear (the driver's `rdychk` loops on it), the two
latched timer flags, the `0AAh` probe read-back, the OPNA ID and mute ports,
register `0Eh` bits 6-7, and the PCM86 FIFO status. It fails on a read of a
port or register whose value is not modeled, so a probe path cannot pass by
accident.

##### Timing model

- The clock is integer OPN master clocks: 3,993,600 Hz for the PC-98 and
  PC-88 YM2203, 7,987,200 Hz for the YM2608, as the header declares.
- Timer A counts 72 master clocks per step (12 operator slots times the /6
  prescaler) and Timer B counts 16 times that, 1,152. A Timer A interval is
  `(1024 - NA) * 72` clocks and a Timer B interval is `(256 - TB) * 1152`.
  Cross-check: `calc_tb_tempo`'s `112Ch / (256 - TB)` at 48 Timer B ticks per
  beat gives 78.5 BPM where these numbers give 77.4, which is the rounding of
  that constant. Do not take the `9216μs` comment on the Timer A setup as the
  constant; it assumes an 8 MHz chip and is 18,461us at 3,993,600 Hz. Pin
  these numbers with the timer case below rather than trusting this
  paragraph.
- Time advances only at timer deadlines. Instruction execution, DOS calls,
  and scripted actions take zero virtual time, so every event of one handler
  invocation shares a clock and line order is the only ordering within a
  tick. There is no wall clock, no cycle count, and no emulator setting to
  pin.
- Both timers run from the install. The probe-time initialization
  (`opninit_loop`) writes `27h` = 3Fh while `24h`/`25h`/`26h` are still 0, so
  the first periods are 1024 * 72 = 73,728 for A and 256 * 1,152 = 294,912 for
  B. `setint` then writes `26h` = 200 (`tempo_d`) and `27h` = 3Fh, but a load
  write to a running timer does not move its deadline, so B keeps its first
  294,912-clock period and only the reloads after that expiry use 64,512.
  The first events after `main` are therefore an `a` at 73,728 and a `b` at
  294,912, which is also the first `ab`. After the first B expiry, A and B
  coincide every 8 B ticks at the default tempo (72/63 = 8/7). Ties are part
  of the first traces, not an exotic case.
- At a deadline, latch that timer's status flag, reschedule it to
  `now + period` from the register values at that instant, and then deliver
  one interrupt. A handler that writes `26h` therefore retimes the reload
  after next, and a tempo change takes effect one tick late. That is the
  chip's behaviour; reproduce it instead of correcting it, and write this
  rule down in the trace README because it is the first explanation for an
  off-by-one-tick diff.
- Clock 0 is the start of the script, so untimed install and API events are
  all at 0. Use 64-bit clocks.

Master clocks rather than Timer B ticks, for three reasons: Timer A always
runs (`TimerA_main` and the effect drivers), tempo changes retime B mid-run,
and a tick index cannot say whether a write happened before or after the
reload. Master clocks are the chip's own integer unit, so two hosts cannot
disagree about a deadline, and `a`/`b` still give the tick index. Rejected:
microseconds or periods accumulated in floating point (drifts, breaks ties),
and the ymfm sample clock (`pmd_ymfm` advances in whole sample periods and is
deliberately rounded, so it is the audio backend and not the trace backend).

##### Comparison and normalization

Compare `.LOG` line by line and `.OUT` byte for byte. Report the first
difference with the action and event index, the expected and actual line, and
a few lines either side; unlike `test/mc`, do not hexdump a whole file.

The harness derives the timer schedule from the register writes; it does not
replay the golden's `a`/`b` lines. The host owns the timer, so the schedule is
part of what is under test: a wrong `26h` value, or a write at the wrong
clock, must fail. Keep a `--replay` debug mode that pivots on the golden's
timer lines so a first mismatch does not cascade; that run still fails.

Normalize only these documented host differences:

- PIC and PIT ports. The translation leaves interrupt masking, EOI, and the
  `WAIT.INC` calibration to the host; the original writes PIC ports
  `00h`/`02h`, PIT ports `71h`/`77h`, and on the 88VA the relocated PIC ports
  `184h`-`18Ah` and the OPN interrupt flag at `32h`, and reads them back.
  `repro.py` drops those ports, and the INT 2Fh Windows probe, from the
  trace. `vsync` is not a shipped variant and its port 2 writes are real
  driver writes, so it stays unit-tested in `test_pmd.c` instead of being
  reference-traced.
- The install status line's two wait counts. `fm_check` calibrates
  `wait_clock`/`wait1_clock` by timing a `loop $` against the PIT, which has
  no meaning under a virtual clock, and the translation intentionally keeps
  both at 1. Console cases therefore use `Board: none`, where the calibration
  never runs and both counts stay 0, or `/C`, which suppresses the status
  block. Do not add a text filter for these numbers; the counts are
  host-owned, like the PIC writes.
- Pointers. The translated `struct pmd` is deliberately not the original's
  byte layout, so offsets into the driver are not comparable. Values that
  point into the module data area (`musdat`, `tondat`, `efcdat`, the loaded
  module) are logged as offsets from the data-area base, because that layout
  is the original's and both sides share it. Every other pointer is interned
  as an alias (`p1`, `p2`, ...) in first-seen order and compared only as an
  alias pattern, which catches `workadr[i]` aliasing `part_data_table[i]`
  without asserting an address.
- Text. `.LOG` strings are raw bytes, so CP932 module text needs no
  conversion. `.OUT` stays UTF-8 as in `test/mc`: `repro.py` converts the
  reference console and the translation prints its UTF-8 `strings_*.h`. A
  console case must therefore not print module-derived text such as the
  music name; use `musname`, which records the bytes in the `.LOG`.

Keep traces short: tens to a few hundred Timer B ticks, which at the default
tempo is about 62 ticks per second.

##### Layout and build

```
test/pmd/cases/<driver>/NAME.RUN   script
test/pmd/cases/fixtures/NAME.M     shared module fixture (reuse test/mc/cases)
test/pmd/cases/<driver>/NAME.LOG   expected trace
test/pmd/cases/<driver>/NAME.OUT   expected console output
test/pmd/trace.c                   action dispatch, fake board, timer model
test/pmd/harness.zig               runs trace-<variant> on each case, compares files
test/pmd/repro.py                  runs the reference COM under Unicorn, writes goldens
```

`test/pmd/trace.c` is built once per variant (`pmd-trace-<variant>`), as
`test_pmd.c` is. It is a separate host from `test_pmd.c`: `test_pmd.c`
scripts every read and checks internals, which a generated golden cannot do,
while the trace host models the board and records only the public surface.
`test-pmd-traces` runs the goldens and joins `zig build test`; a normal run
needs no Python or Unicorn.

##### Validation order

1. `Board: none` install cases: deterministic console text and probe writes,
   no timer model needed.
2. A timer case: `main /C`, then `run b 8` with no module loaded. Its golden
   is the `a`/`b` interleaving and the exact periods, so it pins the constants
   above. It is also the only case where the two timer models can disagree
   with no driver bug to blame.
3. Board-present `/C` install cases with `run b 1` to `run b N`: install and
   init writes plus the first ticks.
4. `load` + `mstart` + `run b N`: real sequencer output.
5. INT 60h API actions and their `ret` values.
6. Effects, external drivers, and the variant matrix as their actions appear.

#### Implementation status

The Phase 9 trace landed with `test/pmd/repro.py` (fake DOS, board, and
virtual timer under Unicorn), `test/pmd/trace.c` (the same models against the
translated driver), `test/pmd/harness.zig`, and the `test-pmd-traces` step in
`zig build test`. Cases are grouped by driver; every shipped variant has a
no-board install, a board install, an INT 60h API case, and a short playback.
The `pmd` directory additionally covers the 26K and 86B installs, the
timer-only interleaving, three official modules, an 86B playback, a module
played to its end, the usage text, and `PMDOPT` overrides. The board model
covers `26K`, `86B`, `VA` (both chips), and `none`. `--replay` is available on
both hosts and in the build (`-Dreplay=true`). The script grammar, event
format, board and timer models, normalization list, and reproduction steps are
documented in `test/pmd/README.md`. Writing these cases exposed and fixed two
translation mismatches: the VA initial register writes use a leaked PIC port
word rather than the intended timer values, and `silence` skipped too little
when ADPCM was unavailable.

The follow-up coverage then landed on the same harness:

- Pointer-valued actions: `musdat`/`tondat`/`efcdat`/`memo` as data-area
  offsets, `pcm`/`workadr` as first-seen aliases, plus `DATA_26K` for the
  buffer layout and the memo table.
- External drivers: `repro.py` installs fake PPSDRV (INT 64h) and PPZ8
  (INT 7Fh) resident drivers with the original's signature checks, and
  `trace.c` passes the matching mock vtables. `PPS_26K` pins detection and
  the effect keyon/keyoff calls; `PPZ_86B`, `PPZ_RESET_86B`, and
  `PPZ_PLAY_86B` (pmdppz and pmdppze) pin the 0410h/0409h handshake, the
  removal-protection, FIFO and keyoff sequences, and PPZ8 playback through
  `PPZEXT.M`, including the directly called FM entry. This also exposed a
  third translation mismatch: `command00` was missing the `_ppz` call at
  `PMD.ASM:1734`, so PPZ8's FM entry was not reached from command dispatch.
- A successful `ADRAMCHK.ASM` transfer: both hosts model the ADPCM-B high
  register bank and its external memory (address shift, two dummy reads,
  BRDY), so `pmdb2`/`pmdppz`/`pmdva` now report ADPCM RAM present.
  `ADPCM_86B` pins the transfer and the part masking it changes.

Still open from the follow-up list: data-memory fixtures (legacy `tondat`
modules, EFC/ADPCM/PCM data, rhythm ROM), the PDR switch, P86DRV, long
fade/FF/pause traces, `Board: 86`, and sanitizer runs.

#### References

- Official PMD 4.8s distribution and reference binaries:
  `http://www5.airnet.ne.jp/kajapon/dat/pmd48s.zip`.
- Unicorn engine: `https://github.com/unicorn-engine/unicorn`, already used by
  `test/mc/repro.py`. The PMD harness needs 16-bit real mode, `UC_HOOK_INTR`
  for DOS calls and INT 60h, and `UC_HOOK_INSN` with `UC_X86_INS_IN`/
  `UC_X86_INS_OUT` for the fake board.
- `https://github.com/d2lmirrors/pmd`: mirrored PMD sources including the
  player (`PMP.ASM`) and IBM variants, useful for understanding how the
  original is loaded and driven.

#### Follow-up items to consider

The script harness, the action set, the install/console cases, the INT 60h
API cases, and the variant/board matrix have landed. The following later
items are valuable once the core harness is in place.

4. **External drivers, PCM/data paths, and long-run timer cases.**
   - PPSDRV and PPZ8 are traced: fake resident drivers answer the original's
     residency checks on both hosts, and the cases cover detection, the
     install/reset handshakes, effect keyon/keyoff, a PPZ8 playback, and the
     unavailable-driver (NULL vtable / absent vector) paths. Still open: the
     PDR switch, P86DRV (which needs a modelled PCM86 card), and the EMS/FIFO
     calls that only a PCM86 board reaches.
   - Add traces with voice banks, EFC data, ADPCM/PCM86 data reads, loop
     offsets, and rhythm ROM contents. These need fixtures and a data-memory
     model in the trace host, so keep them separate from the basic OPN trace.
     A voice-bank case additionally needs a module without an embedded
     program area, because MC modules always carry one and so never read
     `tondat`.
   - Add long-run timing traces for coincident Timer A/B overflows, tempo and
     timer reprogramming, fade, fast-forward/skip, pause, part masks, and
     effect/music masking. These are the cases where trace comparison is most
     valuable over single-tick assertions, but they also need the most
     normalization.

5. **Sanitizers, coverage, and fuzzing.**
   - Run the trace and unit tests under ASan/UBSan, and add a coverage build
     (clang/LLVM or Zig equivalent) to find command paths the current tests do
     not reach.
   - Generate structurally valid `.M` streams from the command length tables
     and run a sampled differential corpus against the reference. Keep the
     fuzz target bounded (tick limits, no unbounded loops) and treat found
     mismatches as ordinary golden cases once triaged.

#### Acceptance criteria

- The shipped 4.8s binaries for every variant can be driven by the
  reproduction script, and the script regenerates the checked-in console,
  API, and trace goldens.
- `zig build test` runs the trace harness against the checked-in files without
  requiring Python or Unicorn, and reports the first differing event with its
  clock, action, and the surrounding lines.
- The goldens include a `Board: none` install case, a board-present install
  case, a timer-only case that pins the Timer A/B periods, and a
  `load`/`mstart` playback trace plus its `status` results for every shipped
  variant. The follow-up items above are tracked separately if not yet
  complete.
- The script grammar, the trace format, the timer rules, the normalization
  list, and the reproduction procedure are documented in `test/pmd/README.md`
  and `PLAN.md`.

## Working rules

- Keep the tree buildable after every phase, and run `zig build test-pmd` once
  that harness is introduced in phase 2.
- Do not translate more than one phase at a time; the PMD driver is large and
  the timer/sequencer interactions are subtle.
- When in doubt, follow the original's control flow and register order rather
  than restructuring for C style.
- When a literal translation is impossible (DOS, PIC, timer calibration, virus
  check), stop at the boundary, leave the surrounding structure intact, and
  mark the omission.
