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
// But mes is expected to be NUL-terminated, not $-terminated.
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
// Takes a NUL-terminated environment variable name and returns its
// NUL-terminated value, or NULL. The returned pointer must be stable across
// multiple calls to this function.
// Replaces kankyo_seg (:8000).
char *mc_sys_getenv(const char *name, void *user_data);

struct mc;

struct mc_com {
    char cmd;
    void (*impl)(struct mc *mc, char cmd);
};

enum mc_rcom_ret {
    MC_RCOM_NORMAL,
    MC_RCOM_OLC02,
    MC_RCOM_OLC03,
};

struct mc_rcom {
    char cmd;
    enum mc_rcom_ret (*impl)(struct mc *mc);
};

struct mc_hs3 {
    char *ptr;
    char name[30];
};

// struct mc contains the global state of the MC process. The original
// assembly implementation simply used global variables, but this translation
// moves all global state into this struct to improve its flexibility of use.
struct mc {
    // :3493
    struct mc_com comtbl[76];
    // :7261
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
    uint16_t line;
    char *linehead;
    uint8_t length_check1;
    uint8_t length_check2;
    uint8_t allloop_flag;
    uint16_t qcommand;
    uint8_t *acc_adr;
    uint16_t jump_flag;

    // :8242
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
    uint8_t part;
    uint8_t ongen;
    uint8_t pass;

    // :8330
    uint8_t maxprg;
    uint8_t kpart_maxprg;
    uint16_t lastprg;

    // :8334
    uint8_t prsok;

    // :8341
    uint8_t prg_flg;
    uint8_t ff_flg;
    uint8_t x68_flg;
    uint8_t towns_flg;
    uint8_t dt2_flg;
    uint8_t opl_flg;
    uint8_t play_flg;
    uint8_t save_flg;
    uint8_t pmd_flg;
    uint8_t ext_detune;
    uint8_t ext_lfo;
    uint8_t ext_env;
    uint8_t memo_flg;
    uint8_t pcm_flg;
    uint8_t lc_flag;
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
    char fm3_partchr[3];
    uint8_t *fm3_ofsadr;
    char pcm_partchr[8];
    uint8_t *pcm_ofsadr;

    // :8422
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
    char mml_buf[61 * 1024];

    // :8472
    char *hsbuf2[256];
    struct mc_hs3 hsbuf3[256];

    // :8480
    char m_filename[128];
    char *file_ext_adr;
    uint8_t m_buf[63 * 1024];

    // :8495
    char v_filename[128];
    uint8_t voice_buf[8192];

    // LC.INC:583
    char part_mes[17];

    // LC.INC:590
    uint8_t print_flag;
    uint32_t all_length;
    uint32_t loop_length;
    uint32_t max_all;
    uint32_t max_loop;
    uint16_t fm3_adr[3];
    uint16_t pcm_adr[8];
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
