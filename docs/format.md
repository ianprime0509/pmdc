# PMD format documentation

Credit to [@OPNA2608's libopenpmd
documentation](https://github.com/OPNA2608/libopenpmd/tree/master/docs) as prior
art for this documentation, which was very helpful as confirmation to make sure
my interpretations were correct.

Hexadecimal numbers in this documentation are written with the suffix `h`: for
example, the number `FFh` is equivalent to decimal 255. Binary numbers are
likewise written with the suffix `b`.

This document uses the following terms for describing data sizes:

- Byte: 8 bits
- Word: 2 bytes
- Dword (double word): 4 bytes

## Module

This documentation is written for PMD version 4.8s. Information relevant for
older versions (which can be gleaned from the PMD driver source code) may be
added later. Note that modules compiled for the FM Towns (option `/t`) are
compiled in compatibility mode for PMD version 4.6.

Data in modules is stored in little-endian byte order. The term "address" is
used to describe a word denoting the offset between the second byte of the file
and the start of another location in the file. That is, the first byte (the
module type byte) is address `-1`, followed by the second byte at address `0`,
and so on.

### High-level structure

The data in a module is laid out as follows:

- Module type byte
- Part data header (addresses of part, rhythm pattern, and instrument data)
- Part data (parts A-K are laid out sequentially)
- Rhythm pattern data header (addresses of individual rhythm pattern data)
- Rhythm pattern data (rhythm parts are laid out sequentially)
- Song length data
- Address of memo data header
- Version byte (`48h` for PMD version 4.8)
- Hard-coded byte `FEh`
- Instrument data
- Memo data
- Memo data header
- Hard-coded word `0000h`

### Module type byte

The first byte of a module indicates the system it is intended to run on:

- `0`: PC-88, PC-98, FM Towns
- `1`: X68000
- `2`: IBM PC

### Part data header

The module data begins with addresses to the primary parts of the module:

- Address `0000h`: address of part A data
- Address `0002h`: address of part B data
- Address `0004h`: address of part C data
- Address `0006h`: address of part D data
- Address `0008h`: address of part E data
- Address `000Ah`: address of part F data
- Address `000Ch`: address of part G data
- Address `000Eh`: address of part H data
- Address `0010h`: address of part I data
- Address `0012h`: address of part J data
- Address `0014h`: address of part K data
- Address `0016h`: address of rhythm pattern data
- Address `0018h`: address of instrument data (only if the `/v` option was used)

If the address of the part A data is `001Ah`, then instrument data is included
in the module. Otherwise, it is not (and the address of the part A data will be
`0018h`).

The addresses of extended parts (FM3Extend, PPZExtend) are encoded using special
commands at the start of part A (for FM3Extend) and part J (for PPZExtend),
documented in the command reference below.

### Part data

Each part is stored as a sequence of commands, where each command consists of a
single byte (the command byte) followed by any arguments applicable to the
command.

In the list below, arguments are given the suffix `b` to denote bytes, `w` to
denote data words, and `a` to denote addresses.

Note that several commands dealing with relative changes to values may be
interpreted as having signed arguments due to how two's-complement arithmetic
works.

- `00h...7Fh LENb` (not part K): play note or rest for `LEN` ticks
  - The command byte determines the note or rest: it is structured as
    `0ooonnnnb`, where `ooo` is the octave number (minus 1) and `nnnn` is the
    note number:
    - `0h`: c
    - `1h`: c+/d-
    - `2h`: d
    - `3h`: d+/e-
    - `4h`: e
    - `5h`: f
    - `6h`: f+/g-
    - `7h`: g
    - `8h`: g+/a-
    - `9h`: a
    - `Ah`: a+/b-
    - `Bh`: b
    - `Ch`: x (repeat previous note) (octave number is always `0`)
    - `Fh`: r (rest) (octave number is always `0`)
  - For example, the command byte `32h` indicates the note D at octave 4.
- `00h...7Fh` (part K): play rhythm pattern (number given by the command byte)
- `80h`: end part
- `B1h CLOCK2b`: set upper bound for `q` quantization (sound cut) to `CLOCK2` ticks
- `B3h CLOCK3b`: set minimum note length for `q` quantization (sound cut) to `CLOCK3` ticks
- `C4h LENb`: set `Q` quantization to `LEN` ticks
- `DFh LENb`: set zenlen to `LEN` ticks
- `FCh TIMERBb`: set TimerB value to `TIMERB`
- `FCh FEh DELTAb`: increase TimerB value by `DELTA`
- `FCh FDh DELTAb`: increase tempo by `DELTA`
- `FCh FFh TEMPOb`: set tempo to `TEMPO`
- `FEh CLOCKb`: set `q` quantization (sound cut) to `CLOCK` ticks

#### Index of commands by letter

- `c`, `d`, `e`, `f`, `g`, `a`, `b`, `r`, `x`: `00h...7Fh LENb`
- `l`, `l=`, `l-`, `l+`, `l^`: modifies previous note (may add `FBh` ties between notes if needed to extend beyond 255 ticks)
- `o`, `o+`, `o-`, `>`, `<`: modifies internal octave setting for part
- `C`: `DFh LENb`
- `t`: `FCh FFh TEMPOb`
- `t+`, `t-`: `FCh FDh DELTAb`
- `T`: `FCh TIMERBb`
- `T+`, `T-`: `FCh FEh DELTAb`
- `q`: `FEh CLOCKb` [`B1h CLOCK2b`] [`B3h CLOCK3b`]
- `Q`: `C4h LENb`
