# Analysis of the C translation of PMD MC 4.8s (src/mc vs original/mc48s)

This document reviews the C translation of the original PMD MC (MML compiler)
assembly sources against `original/mc48s/MC.ASM`, `LC.INC` and `DISKPMD.INC`.
The translation is overall very faithful: all data tables, initial values,
command dispatch, and nearly all command implementations match the original
byte-for-byte in behavior. However, several genuine translation bugs were
found, and a number of smaller divergences are worth documenting.

Test status: `zig build test` passes (DEFONKAI, PAN, SCALE). The bugs below
are therefore outside the coverage of the shipped test cases.

---

## High-impact bugs

### 1. Command-line arguments are concatenated without spaces (mc_main.c)

`mc_main.c` builds the command line by copying each `argv` entry directly
after the previous one:

```c
memcpy(cmdline + cmdline_len, *argv, arglen);
cmdline_len += arglen;
```

The original DOS command line (`MC filename[.MML] voice_filename[.FF]`) is
space-separated, and the parsing in `mc_main` (`get_option`, the `.MML`
filename scan at `:211`, `read_fffile` at `:433`) relies on the space
terminator. As written, `mc foo.mml bar.ff` produces the filename
`foo.mmlbar.ff` and fails with error 3.

Confirmed: `mc t1.mml t2.mml` → "MMLファイルが読み込めません。"
(`t1.mmlt2.mml` cannot be opened); `mc ff1.mml ff1.ff` fails the same way.

Fix: insert a space (or mimic the PSP layout with a leading space) between
arguments.

### 2. `opnhnd` — PMD environment variable search is mistranslated (mc.c `opnhnd`)

Two problems, both confirmed empirically (a `.MML` file located only in the
`PMD` path is reported as unreadable):

a) The separator append condition is wrong:

```c
if (is_sjis || (ohf_write[-1] != '\\' && ohf_write[-1] == ':')) {
```

The original (`DISKPMD.INC:199`, `oh_set_yen`) appends `\` unless the last
character of the path is already `\` or `:`:

```asm
cmp al,"¥"     ; if last char == '\' -> don't add
jz  oh_not_set_yen
cmp al,":"     ; if last char == ':' -> don't add
jz  oh_not_set_yen
oh_set_yen:    ; otherwise add '\'
```

The C condition `last != '\\' && last == ':'` can never be true for a normal
character (a byte cannot be both), so no `\` is inserted for ordinary paths
(e.g. `PMD=/home/user/pmd`), and a `\` *is* inserted when the path ends with
`:` (the opposite of the original). The condition should be
`(ohf_write[-1] != '\\' && ohf_write[-1] != ':')`.

Note: this particular bug is masked when the requested filename itself
contains a path separator, because the C (unlike the original, which strips the
separator) keeps the original `\`/`:` from the filename when building the
combined path. Bare filenames — the common case — are affected.

b) The `;`-separated multi-path search is missing. The original loops over each
`;`-separated path in the `PMD` variable, trying to open
`path\filename` for each in turn (`osp_loop1`). The C copies the entire
variable content (including `;`) into a single path and attempts one open.
Confirmed: `PMD="/nonexistent;/tmp/mctest/pmd2"` fails to find the file, which
the original would locate via the second path.

### 3. `calc_line` — errors in the main file after an `#Include` lose their position (mc.c `calc_line`)

The `case 2` (include-exit marker) handler does:

```c
case 2:
    mc->line = line_stack[--inc_level];
    mml_buf = filename_stack[inc_level];   // WRONG
    goto next_line;
```

`filename_stack[inc_level]` is the *filename of the include being exited*
(stored at `case 1`). Resuming the scan from there re-reads the include's
filename text, and the scan then hits the filename's NUL terminator, which is
handled like EOF (`case 0` → `line = 0, linehead = NULL, si = NULL`). The
original (`cl_inc_dec`) simply continues scanning the outer content from after
the `[02][LF]` marker, i.e. the scan position must *not* be reassigned here.

Confirmed: with

```
#Title test
#Include inc.mml
A cdef
A o9
```

an error on the last line prints no filename/line/caret at all ("Error 26:
Part A ..." only), whereas an error *inside* the include prints
`inc.mml(2) : Error 26 ...` correctly. The fix is to not touch `mml_buf` in
`case 2` (optionally `mml_buf++` to skip the LF, as the original's `inc si`
does).

### 4. `search_hs3` — inverted skip loop for exactly-30-character macro names (mc.c `search_hs3`)

The "all 30 name bytes matched" path ends with:

```c
while (!is_graph(*mc->si)) mc->si++;   // WRONG: condition inverted
return 0;
```

The original (`hscom3_loop3`) skips *printable* characters (`cmp al,"!"; jnc
hscom3_loop3`) and stops at the first character `< '!'` (`dec si`). The C skips
non-printable characters instead, so for a macro name of exactly 30 characters
the position after the match is wrong: a space terminator is consumed, and if
the reference is at end of line, the CR/LF are consumed and the *next line* is
parsed as a continuation of the current line.

Confirmed: with a 30-`a` macro defined as `!aaa…a cdef` and referenced at the
end of a part line, the following line's content is parsed into the current
part, producing extra bytes (`fa 00 00` + extra notes) in the `.M` output and
leaving the next part empty. The loop should be `while (is_graph(*mc->si))`.

---

## Lower-impact bugs / divergences

### 5. `psgenvset` — uninitialized variable in the extended (E) command (mc.c `psgenvset`)

```c
uint8_t al;                                  // uninitialized
if (*mc->si == ',') {
    mc->si++;
    al = getnum(mc);
}
*mc->di++ = al;
```

The original (`extend_psgenv`) does `xor dl,dl` *before* the comma check, so
the trailing value is 0 when the second comma is absent. The C writes an
uninitialized byte (undefined behavior; observed values varied). Should be
`uint8_t al = 0;`.

### 6. `wf_set` — errors when the `#w` value is omitted (mc.c `wf_set`)

```c
if (lngset(mc, &wf) != 0) error(mc, '#', 2);
```

The original:

```asm
wf_set:
    call lngset
    cmp al,4          ; overwrites the carry flag from lngset
    mov dx,"#"*256+2
    jnc error
```

The carry from `lngset` (no number present) is deliberately ignored; the value
defaults to 1 and `#w` with no parameter writes `[d9h][01h]`. The C treats the
missing number as error 2. Confirmed: `A #w cdef` errors in the C translation.

### 7. `fb_set` — 8-bit wrap difference for large values (mc.c `fb_set`)

```c
uint8_t val = getnum(mc);
if (val + 7 >= 15) error(mc, 'F', 2);   // int-promoted arithmetic
```

The original does `add dl,+7; cmp dl,15; jnc error` on an 8-bit register, so
for unsigned values 249–255 the sum wraps (e.g. 250+7 = 1) and passes the
check, while the C's int arithmetic (`257 >= 15`) errors. The original accepts
`FB250` (storing `0x80|250`), the C rejects it. Nonsense input in practice,
but a behavioral difference for values 249–255 without a sign.

### 8. `include_set` — wrong error number for read/close failures (mc.c `include_set`)

The C merges the read/close failure and the "too large" case into one:

```c
if (read_status != 0 || close_status != 0 || inc_len == inc_read_len) {
    mc->si = cr_ptr;
    error(mc, '#', 18);
}
```

The original reports error 3 (`"#"*256+3`) for a failed read/close
(`inc_error3`/`inc_error4`) and error 18 only for the "size too large" case.
The C always reports 18.

### 9. Error-line caret display emits an extra `^` (mc.c `error`)

The original writes `mov word ptr [si],"$^"` — a `$` terminator at the error
position and `^` one byte after — so the printed caret line ends at the `$`
and shows a single caret (at `si-1`, which the code converts to `^`). The C
writes:

```c
mc->si[1] = 0;
mc->si[0] = '^';
if (!is_cntrl(mc->si[-1])) *--mc->si = '^';
```

which leaves `^` at *both* `si` and `si-1` (the NUL is at `si+1`), so the
displayed caret line has two carets (`^^`) where the original shows one.
Observed in error output, e.g. `o9` displays `^^` under it.

### 10. Trailing Ctrl-Z (1Ah) in an included file is not stripped (mc.c `include_set`)

The original strips a trailing `1Ah` from the included content before the CRLF
check (`inc_eof_check: cmp byte ptr -1[di],01ah; jnz inc_crlf_set; dec di`).
The C keeps it and merely appends CRLF if the content doesn't end with CRLF.
The stray 1Ah is treated as an ordinary control character (skipped via
`line_skip`), so compilation still succeeds, but the extra CR/LF can shift
error line numbers for content following the include.

### 11. `ge_set_vol` — negative-depth clamp uses signed comparison (mc.c `ge_set_vol`)

The original:

```asm
cmp al,-15
jnc gem_00      ; unsigned comparison: keep only if al >= 0F1h (-15..-1)
mov al,-15
```

keeps the accumulated depth only when it is in the range -15..-1 and clamps
*everything* else (including 0 and positive sums) to -15. The C clamps only
values below -15 (`if (new_depth < -15) new_depth = -15;`) and keeps positive
sums as-is. Given how `ge_depth`/`ge_depth2` are initialized (same sign, both
from the W command parameter), the divergent case appears unreachable in
practice, but it is a mistranslation of the original's unsigned comparison.

### 12. `calc_line` — linehead off-by-one for errors on the first line of an included file

For an error on the *first* line of an included file, the C sets `linehead` to
the LF that follows the `[01]`/filename header (one position before the line
start), so the caret-line display is suppressed (`linehead` is a control
character); the original sets `linehead` to the first content character. The
line number/filename are still correct.

### 13. `strings_en.h` typo

```c
static const char not_pmd_mes[] = "PMD is not resident.n";
```

ends with a literal `n` instead of `\n`.

---

## Faithful reproductions of original bugs (correct, documented in the code)

The following places where the C carries comments such as "BUG: ..." are
faithful translations of genuine quirks in the original assembly and are not
translation errors:

- `adpcm_set` (`#ADPCM Off` also sets `adpcm_flag = 1`; original does
  `mov [adpcm_flag],1` for both cases).
- `check_lopcnt`/`edl00`: `bh` is undefined when the synthetic `]` is written
  at end of part; the C picks the (intended) `bh == 0` behavior.
- `tieset_2`: the duplicated `test bh,bh` check at `:6187` is omitted.
- `lng_skip_ret`: error message uses `'-'` even when invoked from `l=`/`=` /
  digits (`lngrew`).
- `lfoswitch`: `if (lfocmd == mc->di[-2]) mc->si -= 2;` replicates the
  original's `sub si,2`.
- `include_set`: the dead `inc_eof_chk_loop` (which reads `[fhand-1]` due to
  an uninitialized `bx` in the original) is intentionally skipped.
- PMD-resident features (play, PCM loading, memo display, `set_filename_topmd`,
  `pcm_read`, etc.) are not implemented (documented TODOs); this matches the
  documented scope of the project.

---

## Verified-correct areas

- `fnumdat_seg` (384 entries), `oplprg_table` (72 bytes), `fmvol` (17 bytes),
  `psgenvdat` (10×4), `num_data`, `err_table`/error strings, `comtbl`,
  `rcomtbl` — all match the original exactly (checked programmatically).
- `mc_init` values match the data segment at `:8217` (octave=4, zenlen=96,
  deflng=24, adpcm_flag=0xFF, etc.).
- `one_line_compile`/`olc0`/`olc02`/`olc03`, `skip_mml`, `part_not_found`,
  `part_found`, `olc_skip2` — faithful, including the odd "skip printable
  characters after the part letter" behavior.
- Loop format handling (`stloop`/`edloop`/`extloop`/`bunsan_main`): the f9/f8
  address fields are written exactly as in the original (verified against the
  LC.INC reader, which consumes the same format).
- `press`/`restprs`/`prs3`, `futen`, `lngcal`, `lngset`/`lngset2`, `getnum`,
  `hexget8`, `numget` — faithful including default-value and carry semantics.
- `lc`/`part_loop2`/`kcom_loop`/`rcom_loop`/`command_exec`/`loop_start`/
  `loop_end`/`loop_exit` (LC.INC) — faithful.
- `get_option`, `macro_set` and all header commands, `new_neiro_set`/
  `opl_nns`/`slot_get`/`slot_trans`, `hsset`, `hscom`, `search_hs3` (except
  finding 4), `detset` family, `lfoset` family, `psgenvset` (except finding 5),
  `ssgeg_set`, `volup`/`voldown`, `vseta`/`vset`/`vsetm`/`vss` family,
  `neirochg`/`rhyprg`/`psgprg`/`repeat_check`/`set_prg`, `rhycom` and the R
  part commands, `status_write`, `giji_echo_set`, `sousyoku_onp_set` — all
  match.
- Character classification (`is_graph` etc.), `sjis_check`, `space_cut`,
  `print_8/16/32`, `put_part`, `search_env` (via `getenv`), `usage` — match.

## Suggested regression tests

The shipped test cases don't exercise the affected areas. Useful additions:
multi-argument invocation (`MC file.mml voice.ff`), `PMD` env var lookup
(plain path and `;`-separated paths), `#Include` followed by an error in the
main file, an exactly-30-character macro name referenced at end of line, the
extended E command without a fifth value (`E3,1,2,3,`), and `#w` with no
parameter.
