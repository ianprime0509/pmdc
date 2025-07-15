#ifndef PMDC_MC_H
#define PMDC_MC_H

#include <stdint.h>
#include <stdnoreturn.h>

// All system interactions are done via the following mc_sys functions, which
// mostly correspond to DOS system functions. An implementation of these
// functions using the C standard library I/O functionality is available in
// mc_stdio.c, but other implementations are possible if desired.

// To increase the flexibility of possible system function implementations, all
// mc_sys functions accept a user_data parameter, which is passed as the
// user_data set in the mc "global" structure.

// Reference for DOS INT 21 functions: https://www.stanislavs.org/helppc/int_21.html
// DOS INT 21,2
void mc_sys_putc(char c, void *user_data);
// DOS INT 21,9
// But mes is expected to be null-terminated, not $-terminated.
void mc_sys_print(const char *mes, void *user_data);
// DOS INT 21,3C
void *mc_sys_create(const char *filename, void *user_data);
// DOS INT 21,3D
void *mc_sys_open(const char *filename, void *user_data);
// DOS INT 21,3E
int mc_sys_close(void *file, void *user_data);
// DOS INT 21,3F
int mc_sys_read(void *file, void *dest, uint16_t n, uint16_t *read, void *user_data);
// DOS INT 21,40
int mc_sys_write(void *file, void *data, uint16_t n, void *user_data);
// DOS INT 21,4C
noreturn void mc_sys_exit(int status, void *user_data);
// Takes a null-terminated environment variable name and returns its
// null-terminated value, or NULL. The returned pointer must be stable across
// multiple calls to this function.
// Replaces kankyo_seg (:8000).
char *mc_sys_getenv(const char *name, void *user_data);

struct mc;

// A command definition within comtbl (:3493).
struct mc_com {
    // The command letter.
    char cmd;
    // The implementation of the command. The second argument (command letter)
    // is used by some command implementations, notably the length setting
    // commands, where the same command implementation function is used across
    // multiple commands, and the letter is used for correct error reporting.
    void (*impl)(struct mc *mc, char cmd);
};

// Indicates the location the RSS command should jump back to.
// This is not actually handled as a jump in this implementation, but determines
// the processing done after the command.
enum mc_rcom_ret {
    // Original jump: ret. Corresponds to olc0 processing (:3331).
    MC_RCOM_NORMAL,
    // Original jump: jmp olc02. Corresponds to olc02 processing (:3333).
    MC_RCOM_OLC02,
    // Original jump: jmp olc03. Corresponds to no additional processing before
    // starting the next command (:3336).
    MC_RCOM_OLC03,
};

// A command definition within rcomtbl (:7261).
struct mc_rcom {
    // The command letter.
    char cmd;
    // The implementation of the command.
    enum mc_rcom_ret (*impl)(struct mc *mc);
};

// A named macro definition.
struct mc_hs3 {
    // Pointer within mml_buf to the start of the macro implementation.
    char *ptr;
    // The name of the macro. If the name is fewer than 30 characters, there
    // will be a NUL indicating the end of the name.
    char name[30];
};

// struct mc contains the global state of the MC process. The original
// assembly implementation simply used global variables, but this translation
// moves all global state into this struct to improve its flexibility of use.
struct mc {
    // :3493
    // A table associating command letters with their implementations.
    // This is kept within this struct because it is actually mutable: the
    // octave reverse setting (§2.12, §4.6) is implemented by switching the
    // command letters associated with the octave change commands.
    struct mc_com comtbl[76];
    // :7261
    // A table associating RSS command letters (§14) with their implementations.
    // Unlike comtbl, this table is never mutated, but it is kept here
    // alongside comtbl for consistency.
    struct mc_rcom rcomtbl[12];

    // :8217
    uint8_t tempo;
    uint8_t timerb;
    uint8_t octave;
    uint8_t leng;
    uint8_t zenlen;
    uint8_t deflng;
    uint8_t deflng_k;
    uint8_t calflg;
    uint8_t hsflag;
    uint8_t lopcnt;
    int8_t volss;
    int8_t volss2;
    int8_t octss;
    uint8_t nowvol;
    // The current line number.
    // Only set during error reporting.
    uint16_t line;
    // Pointer within mml_buf to the start of the current line.
    // Only set during the first pass and during error reporting.
    char *linehead;
    uint8_t length_check1;
    uint8_t length_check2;
    uint8_t allloop_flag;
    uint16_t qcommand;
    // Pointer within m_buf right after a preceding )^ or (^ command (§5.5).
    // Used to remove the accent command when the note right after it is
    // skipped.
    uint8_t *acc_adr;
    // Start position for playback (§2.24).
    uint16_t jump_flag;

    // :8242
    // Per-part, per-note transposition (§4.15).
    // The value of these variables is always -1, 0, or 1.
    int8_t def_a;
    int8_t def_b;
    int8_t def_c;
    int8_t def_d;
    int8_t def_e;
    int8_t def_f;
    int8_t def_g;

    // :8250
    uint16_t master_detune;
    uint16_t detune;
    uint16_t alldet;
    uint8_t bend;
    int16_t pitch;

    // :8256
    uint8_t bend1;
    uint8_t bend2;
    uint8_t bend3;

    // :8260
    // Global transposition setting (§2.27).
    uint8_t transpose;

    // :8262
    uint8_t fm_voldown;
    uint8_t ssg_voldown;
    uint8_t pcm_voldown;
    uint8_t rhythm_voldown;
    uint8_t ppz_voldown;

    // :8268
    uint8_t fm_voldown_flag;
    uint8_t ssg_voldown_flag;
    uint8_t pcm_voldown_flag;
    uint8_t rhythm_voldown_flag;
    uint8_t ppz_voldown_flag;

    // :8294
    uint8_t pcm_vol_ext;

    // :8326
    // The current part being processed, starting at 1 for part A.
    uint8_t part;
    // The sound source associated with the current part being processed. See
    // the constants at :8310.
    uint8_t ongen;
    // The current pass being performed over the input.
    //
    // 0: first pass (p1cloop, :533): handles headers and instrument definitions
    // 1: second pass (cmloop, :610): handles all parts except R
    // 2: rhythm pass (rt, :1007): handles R part (rhythm patterns)
    //
    // This variable is not actually read in this implementation. In the
    // original, it is used to control the return location after processing
    // macro definitions.
    uint8_t pass;

    // :8330
    // Keeps track of the highest instrument number (plus one) used in the
    // current part.
    uint8_t maxprg;
    // Set to maxprg before processing rhythm pattern definitions, to keep track
    // of how many more patterns need to be processed. Rhythm patterns defined
    // beyond the maximum one actually used will not be included in the output.
    uint8_t kpart_maxprg;
    // Stores the current SSG rhythm "instrument" number (see MML manual
    // §6.1.3). The high bit (8000h) is always set when this is populated by
    // the @ command.
    uint16_t lastprg;

    // :8334
    // Flags describing the previous content processed, mainly for deciding when
    // to make decisions about compressing tied notes and rests (see the press
    // function, :5348). For example, the sequence c2&c2 can be compressed into
    // c1 to save space in the output.
    //
    // Format: R000PTKL
    // L: previous content was a note length
    // K: previous content was an adjustment of a previous note length
    // T: previous content was a tie command (FBh)
    // P: previous content was a portamento
    // R: previous content was a RSS shot/dump command
    uint8_t prsok;

    // :8341
    // Flags describing how to store instrument data.
    //
    // Bit 0: if set, includes instrument data in the M file (/V option)
    // Bit 1: if set, saves instrument data to an FF file (/VW option)
    uint8_t prg_flg;
    // Whether an FF file has been read (#FFFile header, §2.4).
    uint8_t ff_flg;
    // Whether the module is being compiled for the X68000 system (/M option).
    uint8_t x68_flg;
    // Whether the module is being compiled for the FM-TOWNS system (/T option).
    uint8_t towns_flg;
    // Whether the DT2 parameter is included in instrument definitions
    // (#DT2Flag header, §2.14).
    uint8_t dt2_flg;
    // Whether the module is being compiled for the IBM PC system (/L option).
    uint8_t opl_flg;
    // Whether to play the module after compilation (/P or /S option).
    uint8_t play_flg;
    // Whether to save the module data after compilation (disabled using /S).
    uint8_t save_flg;
    // Whether PMD is resident.
    uint8_t pmd_flg;
    // Whether the SSG detune/LFO is set to extended (#Detune Extend header,
    // §2.16).
    uint8_t ext_detune;
    // Whether the LFO speed is set to extended (#LFOSpeed Extend header,
    // §2.17).
    uint8_t ext_lfo;
    // Whether the SSG/PCM envelope speed is set to extended (#EnvelopeSpeed
    // Extend header, §2.18).
    uint8_t ext_env;
    // Whether to print the module memo data when playing (disabled using /O).
    uint8_t memo_flg;
    // Whether PCM data should be loaded when playing (disabled using /A).
    uint8_t pcm_flg;
    // Whether to display part length information after compilation (/C option).
    uint8_t lc_flag;
    // Default loop count (#LoopDefault header, §2.13).
    uint8_t loop_def;

    // :8358
    uint8_t adpcm_flag;

    // :8362
    uint8_t ss_speed;
    int8_t ss_depth;
    uint8_t ss_length;
    uint8_t ss_tie;

    // :8367
    uint8_t ge_delay;
    int8_t ge_depth;
    int8_t ge_depth2;
    uint8_t ge_tie;
    uint8_t ge_flag1;
    uint8_t ge_flag2;
    uint8_t ge_dep_flag;

    // :8375
    uint8_t skip_flag;
    uint8_t tie_flag;
    uint8_t porta_flag;

    // :8379
    // The part letters of FM3Extend parts (§2.20), or 0 if not set.
    char fm3_partchr[3];
    // The address where the FM3Extend part offsets will be written (following
    // the C6h command).
    uint8_t *fm3_ofsadr;
    // The part letters of PPZExtend parts (§2.25), or 0 if not set.
    char pcm_partchr[8];
    // The address where the PPZExtend part offsets will be written (following
    // the B4h command).
    uint8_t *pcm_ofsadr;

    // :8422
    // Points to the end of the MML content in mml_buf.
    // Used when processing #Include headers (§2.21).
    char *mml_endadr;

    // :8424
    uint16_t loptbl[32];
    uint8_t *lextbl[32];

    // :8429
    uint8_t *bunsan_start;
    uint8_t bunsan_count;
    uint8_t bunsan_work[16];
    uint8_t bunsan_length;
    uint8_t bunsan_1cnt;
    uint8_t bunsan_tieflag;
    uint8_t bunsan_1loop;
    uint8_t bunsan_gate;
    uint8_t bunsan_vol;

    // :8441
    uint8_t newprg_num;
    uint8_t alg_fb;
    uint8_t slot_1[6];
    uint8_t slot_2[6];
    uint8_t slot_3[6];
    uint8_t slot_4[6];
    char prg_name[8];

    // :8449
    uint8_t oplbuf[16];

    // :8451
    uint8_t prg_num[256];

    // :8453
    char mml_filename[128];
    char mml_filename2[128];
    char *ppzfile_adr;
    char *ppsfile_adr;
    char *pcmfile_adr;
    char *title_adr;
    char *composer_adr;
    char *arranger_adr;
    char *memo_adr[128];
    // composer_seg and arranger_seg are not needed in this version.

    // :8465
    // The source MML file.
    // When #Include (§2.21) is processed, the contents of the included MML file
    // are added within this buffer at the correct location, along with a header
    // and trailer used by error handling to report the included file name.
    //
    // Included file structure:
    // [01h] [null-terminated filename] [LF]
    // [included file contents, followed by CRLF if necessary]
    // [02h] [LF]
    char mml_buf[61 * 1024];

    // :8472
    // Pointers within mml_buf to the start of numbered macro definitions.
    char *hsbuf2[256];
    // Named macro definitions.
    struct mc_hs3 hsbuf3[256];

    // :8480
    // The output filename. null-terminated.
    char m_filename[128];
    // Pointer within m_filename to the first character of the file extension
    // (after the "."). Used for format 2 of the #Filename header (§2.1).
    char *file_ext_adr;
    // The output M file.
    uint8_t m_buf[63 * 1024];

    // :8495
    // Name of the external instrument file (#FFFile header, §2.4).
    // null-terminated.
    char v_filename[128];
    // Instrument data, stored in the format of an FF instrument bank file.
    uint8_t voice_buf[8192];

    // LC.INC:583
    // Message printed to display the part length when the /C option is used.
    // This is mutable: the part letter is written to this string when
    // processing the part so that the entire message can be printed at once.
    char part_mes[17];

    // LC.INC:590
    // Whether part length printing is enabled.
    uint8_t print_flag;
    // The total length of the current part.
    uint32_t all_length;
    // The total length of the current part at the time the L command (§10.2) is
    // used.
    // This means this is actually the length of the part _before_ the loop, not
    // the loop length printed when using the /C option.
    uint32_t loop_length;
    // The total length of the song (maximum length among all parts).
    uint32_t max_all;
    // The total length of the global loop section of the song.
    // Unlike loop_length, this is actually the length of the loop itself, not
    // the part before it.
    uint32_t max_loop;
    // The offsets of FM3Extend (§2.20) parts.
    uint16_t fm3_adr[3];
    // The offsets of PPZExtend (§2.25) parts.
    uint16_t pcm_adr[8];
    // Whether there is an infinite local loop (§10.1) in the current part.
    uint8_t loop_flag;
    // The underscore-prefixed versions of fm3_partchr and pcm_partchr are not
    // needed in this translation, since we don't overwrite the values of the
    // original fields to 0 during compilation. See the note on cmloop in mc.c.

    // The si and di x86 registers are used heavily for input and output logic
    // in the original assembly implementation, and they are replicated here as
    // part of the global state of the program. Not all uses of si and di in the
    // original use these fields in this translation, but most do, because it
    // provides a very convenient way to track the current location in the MML
    // source file (si) and M output file (di).
    //
    // This implementation uses char consistently for printable text, such as
    // the MML source file, and uint8_t for binary data, such as the M output
    // file. There are contexts where si and di need to point to a data type
    // they are not normally associated with: C11's anonymous unions make this
    // simple, by allowing sib and dic to alias the si and di fields for their
    // secondary data types.
    union {
        char *si;
        uint8_t *sib;
    };
    union {
        uint8_t *di;
        char *dic;
    };

    void *user_data;
};

// Initializes the global state of the program.
void mc_init(struct mc *mc);
// Runs the program.
void mc_main(struct mc *mc, char *cmdline);

#endif
