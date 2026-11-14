#include "pmd.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            failures++;                                                      \
        }                                                                    \
    } while (0)

#define CHECK_EQ(actual, expected)                                                \
    do {                                                                          \
        unsigned long long actual_ = (unsigned long long)(uintptr_t)(actual);       \
        unsigned long long expected_ = (unsigned long long)(uintptr_t)(expected);   \
        if (actual_ != expected_) {                                                \
            fprintf(stderr, "%s:%d: %s: expected %#llx, got %#llx\n", __FILE__,    \
                    __LINE__, #actual, expected_, actual_);                        \
            failures++;                                                           \
        }                                                                         \
    } while (0)

static void test_sys_putc(char c, void *user_data) {
    (void)c;
    (void)user_data;
}

static void test_sys_print(const char *mes, void *user_data) {
    (void)mes;
    (void)user_data;
}

PMDC_NORETURN static void test_sys_exit(int status, void *user_data) {
    (void)status;
    (void)user_data;
    abort();
}

static char *test_sys_getenv(const char *name, void *user_data) {
    (void)name;
    (void)user_data;
    return NULL;
}

static const struct pmd_sys test_sys = {
    .putc = test_sys_putc,
    .print = test_sys_print,
    .exit = test_sys_exit,
    .getenv = test_sys_getenv,
};

static const struct pmd_hw test_hw = {0};
static const struct pmd_drv_pps test_pps = {0};
static const struct pmd_drv_ppz8 test_ppz8 = {0};
static const struct pmd_drv_p86 test_p86 = {0};

// Allocates a struct pmd filled with garbage, sets the host-owned pointers, and
// runs pmd_init.
static struct pmd *new_pmd(void) {
    struct pmd *pmd = malloc(sizeof(*pmd));
    if (pmd == NULL) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    memset(pmd, 0xa5, sizeof(*pmd));
    pmd->sys = &test_sys;
    pmd->hw = &test_hw;
    pmd->pps = &test_pps;
    pmd->ppz8 = &test_ppz8;
    pmd->p86 = &test_p86;
    pmd->user_data = pmd;
    pmd_init(pmd);
    return pmd;
}

static void free_pmd(struct pmd *pmd) { free(pmd); }

// :8378
static void test_empty_dataarea(struct pmd *pmd) {
    CHECK_EQ(sizeof(pmd->dataarea), 40 * 1024);
    CHECK(pmd->mmlbuf == pmd->dataarea + 1);
    // dataarea[0] is the version/system byte read as -1[mmlbuf].
    CHECK_EQ(pmd->mmlbuf[-1], 0x00);
    // Twelve empty part offsets of 0x18, followed by the end marker.
    for (int i = 0; i < 12; i++) {
        CHECK_EQ(pmd->mmlbuf[2 * i], 0x18);
        CHECK_EQ(pmd->mmlbuf[2 * i + 1], 0x00);
    }
    CHECK_EQ(pmd->mmlbuf[24], 0x80);
    // The first part offset word equals 2 * (max_part2 + 1) = 24, which
    // play_init uses to detect an empty module.
    CHECK_EQ(pmd->mmlbuf[0] | (pmd->mmlbuf[1] << 8), 24);
}

// :8642
static void test_buffer_layout(struct pmd *pmd) {
    CHECK_EQ(pmd->mmldat_lng, 16);
    CHECK_EQ(pmd->voicedat_lng, 8);
    CHECK_EQ(pmd->effecdat_lng, 4);
    // tondat and efcdat are offsets from dataarea, not from mmlbuf, so the
    // version byte comes out of the module buffer: it holds 16 * 1024 - 1
    // bytes while the voice and effect buffers hold their full sizes.
    CHECK_EQ(pmd->tondat, pmd->dataarea + 16 * 1024);
    CHECK_EQ(pmd->tondat - pmd->mmlbuf, 16 * 1024 - 1);
    CHECK_EQ(pmd->efcdat, pmd->dataarea + (16 + 8) * 1024);
    // The buffers are exactly as large as configured, and the largest allowed
    // configuration (40 KiB summed) ends at the end of dataarea.
    CHECK_EQ(pmd->efcdat + 4 * 1024, pmd->dataarea + (16 + 8 + 4) * 1024);
    CHECK_EQ(sizeof(pmd->dataarea), 40 * 1024);
    CHECK(pmd->mmlbuf > pmd->dataarea);
    CHECK(pmd->efcdat > pmd->tondat);
    CHECK(pmd->tondat > pmd->mmlbuf);
}

// :8283
static void test_part_data_table(struct pmd *pmd) {
    const size_t table_len = sizeof(pmd->part_data_table) / sizeof(pmd->part_data_table[0]);
    // The part work areas are contiguous in the original order, so the entries
    // are the array elements in order, with part_e last. Those max_part1 + 1
    // entries are the only ones initialized, since indices come from the part
    // numbers in `part_table`; struct pmd has a fixed layout, so the trailing
    // entries of the table are unused.
    CHECK_EQ(table_len, PMD_MAX_PARTS + 1);
    CHECK_EQ(sizeof(pmd->parts) / sizeof(pmd->parts[0]), PMD_MAX_PARTS);

#if ppz
    const size_t count = 14 + 8 + 1;
#elif board2
    const size_t count = 14 + 1;
#else
    const size_t count = 11 + 1;
#endif
    for (size_t i = 0; i + 1 < count; i++) {
        CHECK(pmd->part_data_table[i] == &pmd->parts[i]);
    }
    CHECK(pmd->part_data_table[count - 1] == &pmd->part_e);

    // :8501 The original clears the first max_part1 part work areas.
    CHECK(pmd->parts[0].address == NULL);
    CHECK_EQ(pmd->parts[0].leng, 0);
    CHECK(pmd->parts[count - 2].address == NULL);
    CHECK_EQ(pmd->parts[count - 2].leng, 0);

    // The table points into the same struct pmd, not into freed or moved
    // storage.
    for (size_t i = 0; i < count; i++) {
        ptrdiff_t off = (char *)pmd->part_data_table[i] - (char *)pmd;
        CHECK(off >= 0 && (size_t)off < sizeof(*pmd));
    }
    // parts[] is a single array, so its entries are adjacent.
    for (size_t i = 1; i + 1 < count; i++) {
        CHECK(pmd->part_data_table[i] == pmd->part_data_table[i - 1] + 1);
    }
    // part_e is a separate member. It follows the last part directly only in
    // the largest variant: smaller ones leave the tail of parts[] unused, so
    // nothing may walk from the last part into part_e, which is why the
    // original compares against `offset part_e` instead.
#if ppz
    CHECK(pmd->part_data_table[count - 1] == pmd->part_data_table[count - 2] + 1);
#else
    CHECK(pmd->part_data_table[count - 1] != pmd->part_data_table[count - 2] + 1);
#endif
}

// :8248
static void test_qq_aliases(struct pmd *pmd) {
    struct pmd_qq *part = &pmd->parts[0];
    CHECK_EQ(offsetof(struct pmd_qq, pat), offsetof(struct pmd_qq, eenv_ar));
    CHECK_EQ(offsetof(struct pmd_qq, pv2), offsetof(struct pmd_qq, eenv_dr));
    CHECK_EQ(offsetof(struct pmd_qq, pr1), offsetof(struct pmd_qq, eenv_sr));
    CHECK_EQ(offsetof(struct pmd_qq, pr2), offsetof(struct pmd_qq, eenv_rr));
    CHECK_EQ(offsetof(struct pmd_qq, patb), offsetof(struct pmd_qq, eenv_arc));
    CHECK_EQ(offsetof(struct pmd_qq, pr1b), offsetof(struct pmd_qq, eenv_src));
    CHECK_EQ(offsetof(struct pmd_qq, pr2b), offsetof(struct pmd_qq, eenv_rrc));
    CHECK_EQ(offsetof(struct pmd_qq, penv), offsetof(struct pmd_qq, eenv_volume));

    part->pat = 3;
    CHECK_EQ(part->eenv_ar, 3);
    part->eenv_volume = 9;
    CHECK_EQ(part->penv, 9);
    part->pr2b = 0x5a;
    CHECK_EQ(part->eenv_rrc, 0x5a);
}

// :8156
static void test_qq_address(struct pmd *pmd) {
    struct pmd_qq *part = &pmd->parts[0];
    part->address = pmd->mmlbuf;
    part->partloop = pmd->efcdat;
    CHECK(part->address == pmd->mmlbuf);
    CHECK(part->partloop == pmd->efcdat);
    CHECK(pmd->part_data_table[0]->address == pmd->mmlbuf);
    part->address = NULL;
    part->partloop = NULL;
    CHECK(part->address == NULL);
    CHECK(part->partloop == NULL);
}

// :8487
static void test_defaults(struct pmd *pmd) {
    CHECK_EQ(pmd->key_check, 1);
#if va + board2
    CHECK_EQ(pmd->fm_voldown, 0);
    CHECK_EQ(pmd->_fm_voldown, 0);
#else
    CHECK_EQ(pmd->fm_voldown, 16);
    CHECK_EQ(pmd->_fm_voldown, 16);
#endif
    CHECK_EQ(pmd->ssg_voldown, 0);
    CHECK_EQ(pmd->_ssg_voldown, 0);
    CHECK_EQ(pmd->pcm_voldown, 0);
    CHECK_EQ(pmd->_pcm_voldown, 0);
    CHECK_EQ(pmd->rhythm_voldown, 0);
    CHECK_EQ(pmd->_rhythm_voldown, 0);
    CHECK_EQ(pmd->ppz_voldown, 0);
    CHECK_EQ(pmd->_ppz_voldown, 0);
    CHECK_EQ(pmd->kp_rhythm_flag, 0xff);
    CHECK_EQ(pmd->fade_stop_flag, 1);
    CHECK_EQ(pmd->ppsdrv_flag, 0xff);
    CHECK_EQ(pmd->ff_tempo, 250);
    CHECK_EQ(pmd->music_flag, 0);
    CHECK_EQ(pmd->message_flag, 1);
#if !va
    CHECK_EQ(pmd->port_sel, -1);
#endif
#if va
    CHECK_EQ(pmd->grph_sp_key, 0x80);
    CHECK_EQ(pmd->rew_sp_key, 0x40);
    CHECK_EQ(pmd->esc_sp_key, 0x80);
#else
    CHECK_EQ(pmd->grph_sp_key, 0x10);
    CHECK_EQ(pmd->rew_sp_key, 0x01);
    CHECK_EQ(pmd->esc_sp_key, 0x10);
#endif

    // :8641
    CHECK_EQ(pmd->psgefcnum, 0xff);
    CHECK_EQ(pmd->fm_effec_num, 0xff);
    CHECK_EQ(pmd->pcm_effec_num, 0xff);
    CHECK_EQ(pmd->effon, 0);
    CHECK_EQ(pmd->fm_effec_flag, 0);
    CHECK_EQ(pmd->pcmflag, 0);
    CHECK_EQ(pmd->intfook_flag, 0);
    CHECK_EQ(pmd->skip_flag, 0);
    CHECK(pmd->fmint.fn == NULL);
    CHECK(pmd->efcint.fn == NULL);

    // :8080
    CHECK_EQ(pmd->rhyvol, 0x3c);
    // :8082
    CHECK(pmd->ssgefcdat != NULL);
    CHECK_EQ(pmd->ssgefclen, 941);
    // The first effect entry has priority 1 and its data starts right after
    // the 40 entry address table.
    CHECK_EQ(pmd->ssgefcdat[0], 1);
    CHECK_EQ(pmd->ssgefcdat[1] | (pmd->ssgefcdat[2] << 8), 40 * 3);
    // :8145
    CHECK_EQ(pmd->p86_freq, 8);
#if board2 && pcm
    CHECK(pmd->p86_freqtable == pmd->pcm_tune_data);
    // The default table starts with the o1 C entry.
    CHECK_EQ(pmd->pcm_tune_data[0], 0x00);
    CHECK_EQ(pmd->pcm_tune_data[1] | (pmd->pcm_tune_data[2] << 8), 0x2ab7);
#else
    CHECK(pmd->p86_freqtable == NULL);
#endif
#if board2 && adpcm && !ademu
    CHECK_EQ(pmd->pcm_table.adpcm_data.pcmends, 0x26);
    CHECK_EQ(pmd->pcm_table.adpcm_data.pcmadrs[0].start, 0);
    CHECK_EQ(pmd->pcm_table.adpcm_data.pcmadrs[0].stop, 0);
    CHECK_EQ(pmd->pcm_table.adpcm_data.filename[0], 0);
#endif
#if board2 && pcm
    CHECK(pmd->pcm_table.pcm86.base == NULL);
    CHECK_EQ(pmd->pcm_table.pcm86.pcmadrs[0].offset, 0);
    CHECK_EQ(pmd->pcm_table.pcm86.pcmadrs[0].size, 0);
#endif
    CHECK_EQ(pmd->adpcm_emulate, 0);

    // :8088
    CHECK_EQ(pmd->TimerBflag, 0);
    CHECK_EQ(pmd->TimerAflag, 0);
    CHECK_EQ(pmd->int60flag, 0);
    // :8103
    CHECK_EQ(pmd->pcm_access, 0);
    // :8110
    CHECK_EQ(pmd->int5_flag, 0);
    CHECK_EQ(pmd->port22h, 0);
    // :8122
    CHECK_EQ(pmd->mstart_flag, 0);
    CHECK_EQ(pmd->mus_filename[0], 0);

    // :8497
    CHECK_EQ(pmd->rshot_bd, 0);
    CHECK_EQ(pmd->rdump_rim, 0);
    // :8505
    CHECK_EQ(pmd->disint, 0);
    CHECK_EQ(pmd->rescut_cant, 0);
    CHECK_EQ(pmd->adpcm_wait, 0);
    CHECK_EQ(pmd->pcm86_vol, 0);
    CHECK_EQ(pmd->_pcm86_vol, 0);
#if board2 && pcm
    // PCMDRV86.ASM:1387
    CHECK_EQ(pmd->play86_flag, 0);
    CHECK_EQ(pmd->trans_flag, 0);
    CHECK_EQ(pmd->start_ofs, 0);
    CHECK_EQ(pmd->repeat_flag, 0);
    CHECK_EQ(pmd->pcm86_pan_dat, 0);
#endif
}

// pmd_init must not overwrite the host-supplied vtables.
static void test_host_pointers_preserved(struct pmd *pmd) {
    CHECK(pmd->sys == &test_sys);
    CHECK(pmd->hw == &test_hw);
    CHECK(pmd->pps == &test_pps);
    CHECK(pmd->ppz8 == &test_ppz8);
    CHECK(pmd->p86 == &test_p86);
    CHECK(pmd->user_data == pmd);
}

static void test_message_buffer(struct pmd *pmd) {
    size_t len = strlen(pmd->mes_int);
    CHECK(len + 1 <= sizeof(pmd->mes_int));
    CHECK_EQ(pmd->mes_int[0], 'I');
    // The original's mes_int run ends with "(" before the port number is
    // printed before mes_int2.
    CHECK_EQ(pmd->mes_int[len - 1], '(');
}

int main(void) {
    struct pmd *pmd = new_pmd();

    test_empty_dataarea(pmd);
    test_buffer_layout(pmd);
    test_part_data_table(pmd);
    test_qq_aliases(pmd);
    test_qq_address(pmd);
    test_defaults(pmd);
    test_host_pointers_preserved(pmd);
    test_message_buffer(pmd);

    free_pmd(pmd);

    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
