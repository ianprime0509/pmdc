#include "pmd.h"

#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if lang == 256 * 'e' + 'n'
#include "strings_en.h"
#else
#include "strings_jp.h"
#endif

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

enum {
    test_out_size = 8192,
    test_write_max = 1024,
    // Guards against a driver hang (a BUSY poll that never finishes).
    test_read_max = 20000,
};

struct test_io {
    uint16_t port;
    uint8_t value;
};

struct test_ctx {
    // Recorded pmd_sys output.
    char out[test_out_size];
    size_t out_len;

    // pmd_sys.exit unwinds here.
    jmp_buf jmp;
    bool exited;
    int exit_status;

    // Scripted pmd_hw reads and recorded writes.
    const struct test_io *script;
    size_t script_len;
    size_t script_pos;
    struct test_io writes[test_write_max];
    size_t write_count;
    unsigned reads;
    char hw_error[128];

    // Reads that the script does not cover return pad_read. The default 0xff
    // models a host with no board.
    uint8_t pad_read;
    // ADPCM data reads (the FM2 data port) take successive bytes from data_in
    // instead, so the transfer contents do not have to be scripted bit by bit.
    uint16_t data_port;
    const uint8_t *data_in;
    size_t data_in_len;
    size_t data_in_pos;

    const char *pmdopt;

    // Mock PPZ8 driver calls.
    unsigned ppz_fifo_stops;
    unsigned ppz_keyoffs[8];
    unsigned ppz_removal_calls;
    bool ppz_removal_protect;
    uint8_t ppz_irq;
};

static struct test_ctx ctx;

static void test_out_char(struct test_ctx *ctx, char c) {
    if (ctx->out_len + 1 < sizeof(ctx->out)) {
        ctx->out[ctx->out_len++] = c;
        ctx->out[ctx->out_len] = '\0';
    }
}

static void test_sys_putc(char c, void *user_data) {
    test_out_char(user_data, c);
}

static void test_sys_print(const char *mes, void *user_data) {
    struct test_ctx *ctx = user_data;
    for (const char *p = mes; *p != '\0'; p++) test_out_char(ctx, *p);
}

PMDC_NORETURN static void test_sys_exit(int status, void *user_data) {
    struct test_ctx *ctx = user_data;
    ctx->exited = true;
    ctx->exit_status = status;
    longjmp(ctx->jmp, 1);
}

static char *test_sys_getenv(const char *name, void *user_data) {
    struct test_ctx *ctx = user_data;
    if (strcmp(name, "PMDOPT") == 0) return (char *)ctx->pmdopt;
    return NULL;
}

static const struct pmd_sys test_sys = {
    .putc = test_sys_putc,
    .print = test_sys_print,
    .exit = test_sys_exit,
    .getenv = test_sys_getenv,
};

PMDC_NORETURN static void test_hw_fail(struct test_ctx *ctx, uint16_t port,
                                       const char *what, uint16_t expected) {
    snprintf(ctx->hw_error, sizeof(ctx->hw_error), "%s(%#x): expected %#x",
             what, port, expected);
    longjmp(ctx->jmp, 2);
}

static uint8_t test_hw_inb(uint16_t port, void *user_data) {
    struct test_ctx *ctx = user_data;
    if (ctx->reads++ >= test_read_max) {
        snprintf(ctx->hw_error, sizeof(ctx->hw_error), "inb(%#x): read limit",
                 port);
        longjmp(ctx->jmp, 2);
    }
    if (ctx->script_pos < ctx->script_len) {
        const struct test_io *entry = &ctx->script[ctx->script_pos];
        if (entry->port != port) test_hw_fail(ctx, port, "inb", entry->port);
        ctx->script_pos++;
        return entry->value;
    }
    if (port == ctx->data_port && ctx->data_in_pos < ctx->data_in_len) {
        return ctx->data_in[ctx->data_in_pos++];
    }
    return ctx->pad_read;
}

static void test_hw_outb(uint16_t port, uint8_t value, void *user_data) {
    struct test_ctx *ctx = user_data;
    if (ctx->write_count >= test_write_max) {
        snprintf(ctx->hw_error, sizeof(ctx->hw_error), "outb(%#x): write limit",
                 port);
        longjmp(ctx->jmp, 2);
    }
    ctx->writes[ctx->write_count].port = port;
    ctx->writes[ctx->write_count].value = value;
    ctx->write_count++;
}

static const struct pmd_hw test_hw = {
    .inb = test_hw_inb,
    .outb = test_hw_outb,
};

#if ppz
static void mock_ppz8_fifo_stop(void *user_data) {
    struct test_ctx *ctx = user_data;
    ctx->ppz_fifo_stops++;
}

static void mock_ppz8_keyoff(uint8_t channel, void *user_data) {
    struct test_ctx *ctx = user_data;
    if (channel < 8) ctx->ppz_keyoffs[channel]++;
}

static void mock_ppz8_removal(bool protect, void *user_data) {
    struct test_ctx *ctx = user_data;
    ctx->ppz_removal_calls++;
    ctx->ppz_removal_protect = protect;
}

static uint8_t mock_ppz8_irq(void *user_data) {
    struct test_ctx *ctx = user_data;
    return ctx->ppz_irq;
}

static void mock_ppz8_fm_int(void *user_data) { (void)user_data; }

static const struct pmd_drv_ppz8 mock_ppz8 = {
    .fifo_interrupt_stop = mock_ppz8_fifo_stop,
    .keyoff = mock_ppz8_keyoff,
    .set_removal_protection = mock_ppz8_removal,
    .get_irq_level = mock_ppz8_irq,
    .fm_int = mock_ppz8_fm_int,
};
#endif

static void ctx_reset(void) {
    memset(&ctx, 0, sizeof(ctx));
    ctx.exit_status = -1;
    ctx.pad_read = 0xff;
    ctx.data_port = 0xffff;
}

// Allocates a struct pmd filled with garbage and sets the host-owned pointers,
// without running pmd_init.
static struct pmd *alloc_pmd(void) {
    struct pmd *pmd = malloc(sizeof(*pmd));
    if (pmd == NULL) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    memset(pmd, 0xa5, sizeof(*pmd));
    pmd->sys = &test_sys;
    pmd->hw = &test_hw;
    pmd->pps = NULL;
    pmd->ppz8 = NULL;
    pmd->p86 = NULL;
    pmd->user_data = &ctx;
    return pmd;
}

static struct pmd *new_pmd(void) {
    struct pmd *pmd = alloc_pmd();
    pmd_init(pmd);
    return pmd;
}

static void free_pmd(struct pmd *pmd) { free(pmd); }

static void report_unwind(const char *who) {
    if (ctx.hw_error[0] != '\0') {
        fprintf(stderr, "%s: hardware script: %s\n", who, ctx.hw_error);
        failures++;
    } else if (!ctx.exited) {
        fprintf(stderr, "%s: unexpected unwind\n", who);
        failures++;
    }
}

// Runs pmd_resident_main. Returns true when it returned normally; an exit or a
// hardware-script failure unwinds through ctx.jmp.
static bool run_resident(struct pmd *pmd, const char *cmdline) {
    ctx.reads = 0;
    ctx.write_count = 0;
    if (setjmp(ctx.jmp) == 0) {
        pmd_resident_main(pmd, (char *)cmdline);
        return true;
    }
    report_unwind("pmd_resident_main");
    return false;
}

// Runs pmd_change_main. Returns its result unless it unwound, in which case
// ctx.exited records the exit.
static enum pmd_change_result run_change(struct pmd *pmd, const char *cmdline) {
    ctx.reads = 0;
    ctx.write_count = 0;
    if (setjmp(ctx.jmp) == 0) {
        return pmd_change_main(pmd, (char *)cmdline);
    }
    report_unwind("pmd_change_main");
    return PMD_CHANGE_OK;
}

static void append(char *buf, size_t size, const char *text) {
    size_t len = strlen(buf);
    if (len + 1 < size) strncat(buf, text, size - len - 1);
}

static void append_number(char *buf, size_t size, unsigned n) {
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%u", n);
    append(buf, size, tmp);
}

static void check_output_equals(const char *expected, int line) {
    if (strcmp(ctx.out, expected) != 0) {
        fprintf(stderr,
                "%s:%d: output mismatch\n--- expected ---\n%s\n--- actual ---\n%s\n"
                "----------------\n",
                __FILE__, line, expected, ctx.out);
        failures++;
    }
}

static void check_output_contains(const char *needle, int line) {
    if (strstr(ctx.out, needle) == NULL) {
        fprintf(stderr, "%s:%d: output does not contain \"%s\":\n%s\n",
                __FILE__, line, needle, ctx.out);
        failures++;
    }
}

#define CHECK_OUTPUT_EQUALS(expected) check_output_equals((expected), __LINE__)
#define CHECK_OUTPUT_CONTAINS(needle) check_output_contains((needle), __LINE__)

// The status text put_status(:9511) must produce, transcribed from the
// assembly rather than from the implementation.
static void expected_status(char *buf, size_t size, const struct pmd *pmd) {
    append(buf, size, changemes_01);
    append_number(buf, size, pmd->mmldat_lng);
    append(buf, size, changemes_02);
    append_number(buf, size, pmd->voicedat_lng);
    append(buf, size, changemes_03);
    append_number(buf, size, pmd->effecdat_lng);
    append(buf, size, changemes_1);
    append_number(buf, size, pmd->_fm_voldown);
    append(buf, size, crlf);
    append(buf, size, changemes_2);
    append_number(buf, size, pmd->_ssg_voldown);
    append(buf, size, crlf);
#if board2
    append(buf, size, changemes_3);
    append_number(buf, size, pmd->_pcm_voldown);
    append(buf, size, crlf);
#if ppz
    append(buf, size, changemes_3z);
    append_number(buf, size, pmd->_ppz_voldown);
    append(buf, size, crlf);
#endif
    append(buf, size, changemes_4);
    append_number(buf, size, pmd->_rhythm_voldown);
    append(buf, size, crlf);
    append(buf, size, changemes_5);
    append(buf, size, pmd->kp_rhythm_flag != 0 ? changemes_5a : changemes_5b);
#endif
    append(buf, size, changemes_6);
    append(buf, size, pmd->key_check != 0 ? changemes_6a : changemes_6b);
    append(buf, size, changemes_7);
    uint8_t esc = pmd->esc_sp_key;
    uint8_t grph = pmd->grph_sp_key;
    uint8_t rew = pmd->rew_sp_key;
#if va
    esc = (uint8_t)(((esc << 3) | (esc >> 5)) & 0x07);
    grph = (uint8_t)(((grph << 3) | (grph >> 5)) & 0x07);
    rew = (uint8_t)(((rew << 3) | (rew >> 5)) & 0x07);
#endif
    append_number(buf, size, esc);
    append(buf, size, crlf);
    append(buf, size, changemes_8);
    append_number(buf, size, grph);
    append(buf, size, crlf);
    append(buf, size, changemes_8b);
    append_number(buf, size, rew);
    append(buf, size, crlf);
    append(buf, size, changemes_9);
    append(buf, size, pmd->fade_stop_flag != 0 ? changemes_6a : changemes_6b);
    append(buf, size, changemes_10);
    append(buf, size, pmd->disint != 0 ? changemes_6a : changemes_6b);
    append(buf, size, changemes_11);
    append(buf, size, pmd->ppsdrv_flag != 0 ? changemes_6a : changemes_6b);
#if ppz
    append(buf, size, changemes_11_);
    append(buf, size, pmd->ppz_call_seg != 0 ? changemes_6a : changemes_6b);
#endif
    append(buf, size, changemes_12);
    append_number(buf, size, pmd->wait_clock);
    append(buf, size, changemes_12a);
    append_number(buf, size,
                  (uint16_t)(pmd->wait_clock * pmd->wait1_clock));
    append(buf, size, changemes_12b);
    append(buf, size, changemes_13);
    append_number(buf, size, pmd->ff_tempo);
    append(buf, size, crlf);
#if board2 && adpcm && !ademu
    append(buf, size, changemes_14);
    append(buf, size, pmd->adpcm_wait == 0
                          ? changemes_14a
                          : (pmd->adpcm_wait == 1 ? changemes_14b
                                                  : changemes_14c));
#endif
#if board2 && pcm
    append(buf, size, changemes_15);
    append(buf, size, pmd->_pcm86_vol != 0 ? changemes_15b : changemes_15a);
    append(buf, size, changemes_16);
    append(buf, size, changemes_16a[pmd->p86_freq]);
    if (pmd->p86_freq != 8) append(buf, size, changemes_16b);
#endif
}

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
    CHECK(strcmp(pmd->mes_ongen1, "YM2608") == 0);
}

// pmd_init must not overwrite the host-supplied vtables.
static void test_host_pointers_preserved(void) {
    static const struct pmd_drv_pps pps = {0};
    static const struct pmd_drv_ppz8 ppz8 = {0};
    static const struct pmd_drv_p86 p86 = {0};

    struct pmd *pmd = alloc_pmd();
    pmd->pps = &pps;
    pmd->ppz8 = &ppz8;
    pmd->p86 = &p86;
    pmd_init(pmd);

    CHECK(pmd->sys == &test_sys);
    CHECK(pmd->hw == &test_hw);
    CHECK(pmd->pps == &pps);
    CHECK(pmd->ppz8 == &ppz8);
    CHECK(pmd->p86 == &p86);
    CHECK(pmd->user_data == &ctx);
    // pmd_init only initializes what the original does; board, for example, is
    // left for fm_check in pmd_resident_main.
    CHECK_EQ(pmd->ppsdrv_flag, 0xff);

    free_pmd(pmd);
}

static void test_message_buffer(struct pmd *pmd) {
    size_t len = strlen(pmd->mes_int);
    CHECK(len + 1 <= sizeof(pmd->mes_int));
    CHECK_EQ(pmd->mes_int[0], 'I');
    // The original's mes_int run ends with "(" before the port number is
    // printed before mes_int2.
    CHECK_EQ(pmd->mes_int[len - 1], '(');
}

// :8385
// The voice area is cleared and the effect area holds 128 little-endian 0100h
// entries followed by 80h.
static void test_buffer_init(void) {
    ctx_reset();
    struct pmd *pmd = new_pmd();
    // Mark everything past the initial image so only the initialized ranges
    // show up as cleared.
    memset(pmd->dataarea + 26, 0xa5, sizeof(pmd->dataarea) - 26);

    CHECK(run_resident(pmd, "/V1/E1"));
    CHECK_EQ(pmd->voicedat_lng, 1);
    CHECK_EQ(pmd->effecdat_lng, 1);
    CHECK(pmd->tondat == pmd->dataarea + 16 * 1024);
    CHECK(pmd->efcdat == pmd->tondat + 1024);

    for (size_t i = 0; i < 1024; i++) {
        if (pmd->tondat[i] != 0) {
            fprintf(stderr, "%s:%d: voice byte %zu not cleared\n", __FILE__,
                    __LINE__, i);
            failures++;
            break;
        }
    }
    for (size_t i = 0; i < 128; i++) {
        CHECK_EQ(pmd->efcdat[2 * i], 0x00);
        CHECK_EQ(pmd->efcdat[2 * i + 1], 0x01);
    }
    CHECK_EQ(pmd->efcdat[256], 0x80);
    for (size_t i = 257; i < 1024; i++) {
        if (pmd->efcdat[i] != 0xa5) {
            fprintf(stderr, "%s:%d: effect byte %zu overwritten\n", __FILE__,
                    __LINE__, i);
            failures++;
            break;
        }
    }
    free_pmd(pmd);

    // /E0 suppresses the effect initialization; the voice area is still
    // cleared.
    ctx_reset();
    pmd = new_pmd();
    memset(pmd->dataarea + 26, 0xa5, sizeof(pmd->dataarea) - 26);
    CHECK(run_resident(pmd, "/V1/E0"));
    CHECK_EQ(pmd->effecdat_lng, 0);
    CHECK(pmd->efcdat == pmd->tondat + 1024);
    CHECK_EQ(pmd->efcdat[0], 0xa5);
    CHECK_EQ(pmd->efcdat[256], 0xa5);
    CHECK_EQ(pmd->tondat[0], 0);
    free_pmd(pmd);

    // /V0 suppresses the voice clearing.
    ctx_reset();
    pmd = new_pmd();
    memset(pmd->dataarea + 26, 0xa5, sizeof(pmd->dataarea) - 26);
    CHECK(run_resident(pmd, "/V0/E0"));
    CHECK_EQ(pmd->voicedat_lng, 0);
    CHECK_EQ(pmd->tondat[0], 0xa5);
    free_pmd(pmd);
}

// :9196 fm_check with a host that has no hardware.
static void test_no_board(void) {
    ctx_reset();
    struct pmd *pmd = new_pmd();
    // The wait counts and the port work are uninitialized in the original, so
    // seed them to show that the no-board path leaves them alone.
    pmd->wait_clock = 0x1234;
    pmd->wait1_clock = 2;
    pmd->fm_port1 = 0x4321;
    CHECK(run_resident(pmd, ""));
    CHECK_EQ(pmd->board, 0);
    // fm_check returned before configuring the ports, so the wait counts and
    // the port work still hold their previous values.
    CHECK_EQ(pmd->wait_clock, 0x1234);
    CHECK_EQ(pmd->wait1_clock, 2);
    CHECK_EQ(pmd->fm_port1, 0x4321);
    // The port text is only updated (:8860) once a board was found; the VA
    // variants have a fixed 44H there.
#if va
    CHECK_EQ(pmd->mes_int[11], '4');
#else
    CHECK_EQ(pmd->mes_int[11], ' ');
#endif
    // :9334 check_86b fails on the absent OPNA ID port before touching one, so
    // the 86B-only builds write nothing; the other probes test a port first.
#if board2 && !va && (pcm || ppz)
    CHECK_EQ(ctx.write_count, 0);
#else
    CHECK(ctx.write_count != 0);
#endif

    char expected[test_out_size] = "";
    append(expected, sizeof(expected), mes_title);
    append(expected, sizeof(expected), mes_warning);
    append(expected, sizeof(expected), mes_not_board);
    append(expected, sizeof(expected), mes_wait1);
    append_number(expected, sizeof(expected), 0x1234);
    append(expected, sizeof(expected), mes_wait2);
    append_number(expected, sizeof(expected), 0x1234 * 2);
    append(expected, sizeof(expected), mes_wait3);
    append(expected, sizeof(expected), mes_exit);
    append(expected, sizeof(expected), mes_key);
    CHECK_OUTPUT_EQUALS(expected);
    free_pmd(pmd);
}

// A NULL hw must report no board before the first vtable dereference.
static void test_null_hw(void) {
    ctx_reset();
    struct pmd *pmd = new_pmd();
    pmd->hw = NULL;
    CHECK(run_resident(pmd, ""));
    CHECK_EQ(pmd->board, 0);
    CHECK_OUTPUT_CONTAINS(mes_not_board);
    free_pmd(pmd);
}

// :10467
static void test_usage(void) {
    // /H (and any unknown option) prints the usage text and exits 255.
    const char *bad[] = {"/H", "/X", "/KX", "/D", "/M", "/HZ9", "/W0", "/G0"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        ctx_reset();
        struct pmd *pmd = new_pmd();
        CHECK(!run_resident(pmd, bad[i]));
        CHECK(ctx.exited);
        if (ctx.exit_status != 255) {
            fprintf(stderr, "%s:%d: \"%s\": expected exit 255, got %d\n",
                    __FILE__, __LINE__, bad[i], ctx.exit_status);
            failures++;
        }
        char expected[test_out_size] = "";
        append(expected, sizeof(expected), mes_title);
        append(expected, sizeof(expected), mes_warning);
        append(expected, sizeof(expected), mes_not_board);
        append(expected, sizeof(expected), mes_usage);
        CHECK_OUTPUT_EQUALS(expected);
        free_pmd(pmd);
    }

    // /R is not valid before the driver is resident.
    ctx_reset();
    struct pmd *pmd = new_pmd();
    CHECK(!run_resident(pmd, "/R"));
    CHECK(ctx.exited);
    CHECK_EQ(ctx.exit_status, 1);
    CHECK_OUTPUT_CONTAINS(pmderror_mes5);
    free_pmd(pmd);
}

// :8582
static void test_capacity_limits(void) {
    struct {
        const char *cmdline;
        const char *message;
        int status;
    } cases[] = {
        {"/M0", pmderror_mes2, 1},
        {"/M29", pmderror_mes3, 1},
        {"/M32", pmderror_mes3, 1},
        {"/V255", pmderror_mes3, 1},
        {"/M255/V255/E255", pmderror_mes3, 1},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        ctx_reset();
        struct pmd *pmd = new_pmd();
        CHECK(!run_resident(pmd, cases[i].cmdline));
        CHECK(ctx.exited);
        CHECK_EQ(ctx.exit_status, cases[i].status);
        CHECK_OUTPUT_CONTAINS(cases[i].message);
        free_pmd(pmd);
    }

    // The largest accepted configuration ends exactly at the end of dataarea.
    ctx_reset();
    struct pmd *pmd = new_pmd();
    CHECK(run_resident(pmd, "/M28/V8/E4"));
    CHECK_EQ(pmd->mmldat_lng, 28);
    CHECK(pmd->tondat == pmd->dataarea + 28 * 1024);
    CHECK(pmd->efcdat == pmd->dataarea + 36 * 1024);
    CHECK(pmd->efcdat + 4 * 1024 == pmd->dataarea + sizeof(pmd->dataarea));
    free_pmd(pmd);

    // Without a command line the checked limits are not re-evaluated (:8569),
    // but the buffer pointers still follow PMDOPT.
    ctx_reset();
    ctx.pmdopt = "/M20";
    pmd = new_pmd();
    CHECK(run_resident(pmd, ""));
    CHECK_EQ(pmd->mmldat_lng, 20);
    CHECK(pmd->tondat == pmd->dataarea + 20 * 1024);
    CHECK(pmd->efcdat == pmd->dataarea + 28 * 1024);
    free_pmd(pmd);

    // PMDOPT alone can still ask for buffers that do not fit in dataarea; the
    // memory check (:8604) is what the original's MCB check did.
    const char *oversized[] = {"/M41", "/V41", "/M16/V8/E17"};
    for (size_t i = 0; i < sizeof(oversized) / sizeof(oversized[0]); i++) {
        ctx_reset();
        ctx.pmdopt = oversized[i];
        pmd = new_pmd();
        CHECK(!run_resident(pmd, ""));
        CHECK(ctx.exited);
        CHECK_EQ(ctx.exit_status, 1);
        CHECK_OUTPUT_CONTAINS(pmderror_mes7);
        free_pmd(pmd);
    }

    // The exact bound is still accepted from PMDOPT.
    ctx_reset();
    ctx.pmdopt = "/M28/V8/E4";
    pmd = new_pmd();
    CHECK(run_resident(pmd, ""));
    CHECK(pmd->efcdat + 4 * 1024 == pmd->dataarea + sizeof(pmd->dataarea));
    free_pmd(pmd);
}

// The resident option table (:10759).
static void test_resident_options(void) {
    ctx_reset();
    struct pmd *pmd = new_pmd();
    CHECK(run_resident(pmd, "/DF5/DS6/DP7/DZ8/DR9/N/F-/P -W9/G7/A2/S/KE3"));
    CHECK_EQ(pmd->fm_voldown, 5);
    CHECK_EQ(pmd->_fm_voldown, 5);
    CHECK_EQ(pmd->ssg_voldown, 6);
    CHECK_EQ(pmd->_ssg_voldown, 6);
    CHECK_EQ(pmd->pcm_voldown, 7);
    CHECK_EQ(pmd->_pcm_voldown, 7);
    CHECK_EQ(pmd->ppz_voldown, 8);
    CHECK_EQ(pmd->_ppz_voldown, 8);
    CHECK_EQ(pmd->rhythm_voldown, 9);
    CHECK_EQ(pmd->_rhythm_voldown, 9);
    CHECK_EQ(pmd->kp_rhythm_flag, 0);
    CHECK_EQ(pmd->fade_stop_flag, 1);
    CHECK_EQ(pmd->ppsdrv_flag, 0);
    CHECK_EQ(pmd->wait_clock, 9);
    CHECK_EQ(pmd->ff_tempo, 7);
    CHECK_EQ(pmd->adpcm_wait, 2);
    CHECK_EQ(pmd->pcm86_vol, 1);
    CHECK_EQ(pmd->_pcm86_vol, 1);
#if va
    // The VA key masks are stored rotated left by 3 (:10069).
    CHECK_EQ(pmd->esc_sp_key, (uint8_t)(((3 >> 3) | (3 << 5)) & 0xe0));
#else
    CHECK_EQ(pmd->esc_sp_key, 3);
#endif
    free_pmd(pmd);

    ctx_reset();
    pmd = new_pmd();
    CHECK(run_resident(pmd, "/K/N-/F/I/S-"));
    CHECK_EQ(pmd->key_check, 0);
    CHECK_EQ(pmd->kp_rhythm_flag, 1);
    CHECK_EQ(pmd->fade_stop_flag, 0);
    CHECK_EQ(pmd->disint, 1);
    CHECK_EQ(pmd->pcm86_vol, 0);
    free_pmd(pmd);

    // /W without a number falls back to wait_check, and /G without one resets
    // the fast-forward tempo.
    ctx_reset();
    pmd = new_pmd();
    CHECK(run_resident(pmd, "/W/G"));
    CHECK_EQ(pmd->wait_clock, 1);
    CHECK_EQ(pmd->wait1_clock, 1);
    CHECK_EQ(pmd->ff_tempo, 250);
    free_pmd(pmd);

    // /A without a number means 0 (the original ignores the carry flag).
    ctx_reset();
    pmd = new_pmd();
    pmd->adpcm_wait = 2;
    CHECK(run_resident(pmd, "/A"));
    CHECK_EQ(pmd->adpcm_wait, 0);
    free_pmd(pmd);

    // /KG, /KR, and /KE also ignore the carry flag (:10067, :10081, :10095),
    // so a missing number stores 0 instead of being a usage error.
    ctx_reset();
    pmd = new_pmd();
    pmd->grph_sp_key = 0x11;
    pmd->rew_sp_key = 0x12;
    pmd->esc_sp_key = 0x13;
    CHECK(run_resident(pmd, "/KG/KR/KE"));
    CHECK_EQ(pmd->grph_sp_key, 0);
    CHECK_EQ(pmd->rew_sp_key, 0);
    CHECK_EQ(pmd->esc_sp_key, 0);
    free_pmd(pmd);

    // /C suppresses the closing messages, but the banner and the fm_check
    // warning were already printed before the option was parsed.
    ctx_reset();
    pmd = new_pmd();
    CHECK(run_resident(pmd, "/C"));
    CHECK_EQ(pmd->message_flag, 0);
    char expected[test_out_size] = "";
    append(expected, sizeof(expected), mes_title);
    append(expected, sizeof(expected), mes_warning);
    append(expected, sizeof(expected), mes_not_board);
    CHECK_OUTPUT_EQUALS(expected);
    free_pmd(pmd);

#if !va
    // /O selects a port and re-probes the board (:10299).
    ctx_reset();
    pmd = new_pmd();
    CHECK(run_resident(pmd, "/O2"));
    CHECK_EQ(pmd->port_sel, 2);
    free_pmd(pmd);
#else
    // The VA variants accept /O but do not change the fixed port. port_sel is
    // uninitialized in these builds, so seed it to show the option leaves it.
    ctx_reset();
    pmd = new_pmd();
    pmd->port_sel = 0x7f;
    CHECK(run_resident(pmd, "/O2"));
    CHECK_EQ(pmd->port_sel, 0x7f);
    free_pmd(pmd);
#endif
}

#if board2 && pcm
// :10485 HZ downloads the selected frequency table.
static void test_hz_option(void) {
    ctx_reset();
    struct pmd *pmd = new_pmd();
    CHECK(run_resident(pmd, "/HZ0"));
    CHECK_EQ(pmd->p86_freq, 0);
    // HZDATA.INC table 0 starts with the 4.13kHz C entry.
    CHECK_EQ(pmd->pcm_tune_data[0], 0x00);
    CHECK_EQ(pmd->pcm_tune_data[1], 0xb7);
    CHECK_EQ(pmd->pcm_tune_data[2], 0x2a);
    free_pmd(pmd);

    // The auto setting leaves the current table alone.
    ctx_reset();
    pmd = new_pmd();
    CHECK(run_resident(pmd, "/HZ0"));
    pmd->pcm_tune_data[0] = 0x5a;
    CHECK(run_resident(pmd, "/HZ8"));
    CHECK_EQ(pmd->p86_freq, 8);
    CHECK_EQ(pmd->pcm_tune_data[0], 0x5a);
    free_pmd(pmd);
}
#endif

// :8542 the PMDOPT environment is read before the command line, so the command
// line wins, and a bad PMDOPT option only warns.
static void test_option_precedence(void) {
    ctx_reset();
    ctx.pmdopt = "/DF5";
    struct pmd *pmd = new_pmd();
    CHECK(run_resident(pmd, "/DF7"));
    CHECK_EQ(pmd->_fm_voldown, 7);
    free_pmd(pmd);

    ctx_reset();
    ctx.pmdopt = "/DF5";
    pmd = new_pmd();
    CHECK(run_resident(pmd, ""));
    CHECK_EQ(pmd->_fm_voldown, 5);
    free_pmd(pmd);

    // A bad PMDOPT option reports the warning and abandons the rest of the
    // environment options only. /DF5 was applied before /X failed.
    ctx_reset();
    ctx.pmdopt = "/DF5/X/DS9";
    pmd = new_pmd();
    CHECK(run_resident(pmd, "/DR3"));
    CHECK_EQ(pmd->_fm_voldown, 5);
    CHECK_EQ(pmd->_ssg_voldown, 0);
    CHECK_EQ(pmd->_rhythm_voldown, 3);
    CHECK_OUTPUT_CONTAINS(mes_warning);
    CHECK_OUTPUT_CONTAINS(mes_kankyo);
    free_pmd(pmd);

    // The command line is still parsed after an environment error.
    ctx_reset();
    ctx.pmdopt = "/X";
    pmd = new_pmd();
    CHECK(run_resident(pmd, "/DS9"));
    CHECK_EQ(pmd->_ssg_voldown, 9);
    free_pmd(pmd);
}

// :10148 /P turns PPSDRV support on or off; availability is the host vtable.
static void test_pps_option(void) {
    static const struct pmd_drv_pps pps = {0};

    ctx_reset();
    struct pmd *pmd = new_pmd();
    pmd->pps = &pps;
    CHECK(run_resident(pmd, "/P-"));
    CHECK_EQ(pmd->ppsdrv_flag, 1);
    free_pmd(pmd);

    ctx_reset();
    pmd = new_pmd();
    pmd->pps = &pps;
    CHECK(run_resident(pmd, "/P"));
    CHECK_EQ(pmd->ppsdrv_flag, 0);
    free_pmd(pmd);

    // Without the vtable the enabling option cannot turn it on.
    ctx_reset();
    pmd = new_pmd();
    CHECK(run_resident(pmd, "/P-"));
    CHECK_EQ(pmd->ppsdrv_flag, 0);
    free_pmd(pmd);
}

#if ppz
// :10164 /Z drives the PPZ8 negotiation and the removal protection.
static void test_ppz_options(void) {
    ctx_reset();
    struct pmd *pmd = new_pmd();
    pmd->ppz8 = &mock_ppz8;
    // int_level is only set by int_init, which runs after the options, so a
    // fresh driver compares against uninitialized state; seed it as a resident
    // driver would have it.
    pmd->int_level = 0;
    ctx.ppz_irq = 8; // matches int_level + 8
    CHECK(run_resident(pmd, "/Z-"));
    CHECK_EQ(pmd->ppz_call_seg, 2);
    CHECK_EQ(ctx.ppz_removal_calls, 1);
    CHECK(ctx.ppz_removal_protect);
    free_pmd(pmd);

    // A different PPZ8 interrupt level leaves only the ordinary INT 7Fh
    // interface available.
    ctx_reset();
    pmd = new_pmd();
    pmd->ppz8 = &mock_ppz8;
    pmd->int_level = 0;
    ctx.ppz_irq = 4;
    CHECK(run_resident(pmd, "/Z-"));
    CHECK_EQ(pmd->ppz_call_seg, 1);
    CHECK(ctx.ppz_removal_protect);
    free_pmd(pmd);

    // Disabling stops the FIFO interrupt and every channel.
    ctx_reset();
    pmd = new_pmd();
    pmd->ppz8 = &mock_ppz8;
    CHECK(run_resident(pmd, "/Z"));
    CHECK_EQ(pmd->ppz_call_seg, 0);
    CHECK_EQ(ctx.ppz_fifo_stops, 1);
    for (unsigned ch = 0; ch < 8; ch++) CHECK_EQ(ctx.ppz_keyoffs[ch], 1);
    CHECK_EQ(ctx.ppz_removal_calls, 1);
    CHECK(!ctx.ppz_removal_protect);
    free_pmd(pmd);
}
#endif

// :9511 pmd_change_main prints the banner and the status, and only reports
// "settings changed" when it was given options.
static void test_change_status(void) {
    ctx_reset();
    struct pmd *pmd = new_pmd();
    CHECK(run_resident(pmd, "/M12/V2/E2/DF200/DS12/W255/G7"));

    ctx_reset();
    enum pmd_change_result result = run_change(pmd, "");
    CHECK(!ctx.exited);
    CHECK_EQ(result, PMD_CHANGE_OK);
    char expected[test_out_size] = "";
    append(expected, sizeof(expected), mes_title);
    expected_status(expected, sizeof(expected), pmd);
    CHECK_OUTPUT_EQUALS(expected);
    free_pmd(pmd);

    // With options: "settings changed" is printed first.
    ctx_reset();
    pmd = new_pmd();
    CHECK(run_resident(pmd, ""));
    ctx_reset();
    result = run_change(pmd, "/DF3/DS4");
    CHECK(!ctx.exited);
    CHECK_EQ(result, PMD_CHANGE_OK);
    CHECK_EQ(pmd->_fm_voldown, 3);
    expected[0] = '\0';
    append(expected, sizeof(expected), mes_title);
    append(expected, sizeof(expected), changemes_0);
    expected_status(expected, sizeof(expected), pmd);
    CHECK_OUTPUT_EQUALS(expected);
    free_pmd(pmd);
}

// :10798 the runtime table rejects the options that cannot change.
static void test_change_options(void) {
    ctx_reset();
    struct pmd *pmd = new_pmd();
    CHECK(run_resident(pmd, ""));

    const char *rejected[] = {"/M16", "/V8", "/E4", "/Y", "/O1"};
    for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); i++) {
        ctx_reset();
        (void)run_change(pmd, rejected[i]);
        CHECK(ctx.exited);
        if (ctx.exit_status != 1) {
            fprintf(stderr, "%s:%d: \"%s\": expected exit 1, got %d\n",
                    __FILE__, __LINE__, rejected[i], ctx.exit_status);
            failures++;
        }
        CHECK_OUTPUT_CONTAINS(pmderror_mes6);
    }

    // An unknown option in change_main is a usage error.
    ctx_reset();
    (void)run_change(pmd, "/X");
    CHECK(ctx.exited);
    CHECK_EQ(ctx.exit_status, 255);

    // /R reports removal and prints neither the status nor the success line.
    ctx_reset();
    enum pmd_change_result result = run_change(pmd, "/R");
    CHECK(!ctx.exited);
    CHECK_EQ(result, PMD_CHANGE_REMOVED);
    char expected[test_out_size] = "";
    append(expected, sizeof(expected), mes_title);
    CHECK_OUTPUT_EQUALS(expected);
    free_pmd(pmd);

    // /R also stops the remaining options.
    ctx_reset();
    pmd = new_pmd();
    CHECK(run_resident(pmd, ""));
    ctx_reset();
    uint8_t voldown = pmd->_fm_voldown;
    result = run_change(pmd, "/R/DF3");
    CHECK_EQ(result, PMD_CHANGE_REMOVED);
    CHECK_EQ(pmd->_fm_voldown, voldown);
    free_pmd(pmd);
}

static void set_script(const struct test_io *script, size_t len) {
    ctx.script = script;
    ctx.script_len = len;
    ctx.script_pos = 0;
}

static bool has_write(uint16_t port, uint8_t value) {
    for (size_t i = 0; i < ctx.write_count; i++) {
        if (ctx.writes[i].port == port && ctx.writes[i].value == value) {
            return true;
        }
    }
    return false;
}

#if !va && !board2
// :9196 The 98+NORM probe path with a scripted board.
static const struct test_io board_script[] = {
    // check_spb: the OPNA ID port is present, and 088h answers as a YM2608.
    {0xa460, 0x00},
    {0x08a, 0x01},
    {0x08c, 0x00},
    {0x08e, 0x00},
    // int_init: two rdychk reads for each of the two register accesses.
    {0x088, 0x00},
    {0x088, 0x00},
    {0x088, 0x00},
    {0x088, 0x00},
    {0x08a, 0xc0}, // int5
    // opninit_loop, one entry per candidate port.
    {0x088, 0x00},
    {0x088, 0x00},
    {0x088, 0x00},
    {0x08a, 0xc0},
    {0x188, 0x00},
    {0x188, 0x00},
    {0x188, 0x00},
    {0x18a, 0xc0},
    {0x288, 0x00},
    {0x288, 0x00},
    {0x288, 0x00},
    {0x28a, 0xc0},
    {0x388, 0x00},
    {0x388, 0x00},
    {0x388, 0x00},
    {0x38a, 0xc0},
};

static void test_board_present(void) {
    ctx_reset();
    set_script(board_script, sizeof(board_script) / sizeof(board_script[0]));
    struct pmd *pmd = new_pmd();
    CHECK(run_resident(pmd, ""));
    CHECK_EQ(ctx.script_pos, ctx.script_len);

    CHECK_EQ(pmd->board, 1);
    CHECK_EQ(pmd->ongen, 1);
    CHECK_EQ(pmd->fm1_port1, 0x088);
    CHECK_EQ(pmd->fm1_port2, 0x08a);
    CHECK_EQ(pmd->fm2_port1, 0x08c);
    CHECK_EQ(pmd->fm2_port2, 0x08e);
    CHECK_EQ(pmd->fm_port1, 0x088);
    CHECK_EQ(pmd->fm_port2, 0x08a);
    // :8860 the resident port message reports the selected port.
    CHECK_EQ(pmd->mes_int[11], '0');
    // :8937 the interrupt number comes from the 0Eh bits.
    CHECK_EQ(pmd->mes_int[4], '5');
    CHECK_EQ(pmd->int_level, 12);
    CHECK_EQ(pmd->opn_0eh, 0xc0);
    // wait_check's placeholder counts.
    CHECK_EQ(pmd->wait_clock, 1);
    CHECK_EQ(pmd->wait1_clock, 1);
    // No PPSDRV vtable, so the auto check turns it off.
    CHECK_EQ(pmd->ppsdrv_flag, 0);
    CHECK_EQ(pmd->kp_rhythm_flag, 1);
    CHECK_EQ(pmd->pcm_gs_flag, 1);

    char expected[test_out_size] = "";
    append(expected, sizeof(expected), mes_title);
    append(expected, sizeof(expected), pmd->mes_int);
    append(expected, sizeof(expected), pmd->mes_ongen1);
    append(expected, sizeof(expected), mes_int2);
    append(expected, sizeof(expected), mes_wait1);
    append(expected, sizeof(expected), "1");
    append(expected, sizeof(expected), mes_wait2);
    append(expected, sizeof(expected), "1");
    append(expected, sizeof(expected), mes_wait3);
    append(expected, sizeof(expected), mes_exit);
    append(expected, sizeof(expected), mes_key);
    CHECK_OUTPUT_EQUALS(expected);

    // int_init reads PSG 0Eh through the primary ports, and opninit_loop
    // writes the initial registers to every matching port.
    CHECK(has_write(0x088, 0x07));
    CHECK(has_write(0x08a, 0x3f));
    CHECK(has_write(0x088, 0x0e));
    CHECK(has_write(0x088, 0x29));
    CHECK(has_write(0x08a, 0x00));
    CHECK(has_write(0x088, 0x27));
    CHECK(has_write(0x08a, 0x3f));
    CHECK(has_write(0x388, 0x27));
    CHECK(has_write(0x38a, 0x3f));

    free_pmd(pmd);
}

// :8971 int_init enables PPSDRV when the host supplies the vtable, and that in
// turn clears the SSG-drum rhythm flag.
static void test_board_ppsdrv(void) {
    static const struct pmd_drv_pps pps = {0};

    ctx_reset();
    set_script(board_script, sizeof(board_script) / sizeof(board_script[0]));
    struct pmd *pmd = new_pmd();
    pmd->pps = &pps;
    CHECK(run_resident(pmd, ""));
    CHECK_EQ(ctx.script_pos, ctx.script_len);

    CHECK_EQ(pmd->board, 1);
    CHECK_EQ(pmd->ppsdrv_flag, 1);
    CHECK_EQ(pmd->kp_rhythm_flag, 0);

    char expected[test_out_size] = "";
    append(expected, sizeof(expected), mes_title);
    append(expected, sizeof(expected), pmd->mes_int);
    append(expected, sizeof(expected), pmd->mes_ongen1);
    append(expected, sizeof(expected), mes_int2);
    append(expected, sizeof(expected), mes_ppsdrv);
    append(expected, sizeof(expected), mes_wait1);
    append(expected, sizeof(expected), "1");
    append(expected, sizeof(expected), mes_wait2);
    append(expected, sizeof(expected), "1");
    append(expected, sizeof(expected), mes_wait3);
    append(expected, sizeof(expected), mes_exit);
    append(expected, sizeof(expected), mes_key);
    CHECK_OUTPUT_EQUALS(expected);

    free_pmd(pmd);
}

// :10284 /O stores the port and re-runs fm_check (:10299), so the driver can be
// pointed at a board the initial scan did not find. The first fm_check sees no
// board at all; the second finds an SPB at 288H.
static const struct test_io portsel_script[] = {
    // First fm_check: no OPNA ID port, no YM2203-class card at 088H-388H.
    {0xa460, 0xff}, // check_spb
    {0x08a, 0x00},
    {0x18a, 0x00},
    {0x28a, 0x00},
    {0x38a, 0x00},
    {0xa460, 0xff}, // check_86b
    {0x08a, 0x00}, // the 088h/188h/288h/388h YM2203 probe
    {0x18a, 0x00},
    {0x28a, 0x00},
    {0x38a, 0x00},
    // Second fm_check, restricted to 288H by port_sel.
    {0xa460, 0x00}, // check_spb
    {0x28a, 0x01},
    {0x28c, 0x00},
    {0x28e, 0x00},
    // int_init, which then runs against the selected port.
    {0x288, 0x00},
    {0x288, 0x00},
    {0x288, 0x00},
    {0x288, 0x00},
    {0x28a, 0xc0}, // int5
};

static void test_portsel_reprobe(void) {
    ctx_reset();
    set_script(portsel_script,
               sizeof(portsel_script) / sizeof(portsel_script[0]));
    ctx.pad_read = 0x00; // idle status: no chip on the remaining scan ports
    struct pmd *pmd = new_pmd();
    CHECK(run_resident(pmd, "/O2"));
    CHECK_EQ(ctx.script_pos, ctx.script_len);

    CHECK_EQ(pmd->port_sel, 2);
    CHECK_EQ(pmd->board, 1);
    CHECK_EQ(pmd->fm1_port1, 0x288);
    CHECK_EQ(pmd->fm1_port2, 0x28a);
    CHECK_EQ(pmd->fm2_port1, 0x28c);
    CHECK_EQ(pmd->fm2_port2, 0x28e);
    CHECK_EQ(pmd->fm_port1, 0x288);
    CHECK_EQ(pmd->fm_port2, 0x28a);
    CHECK_EQ(pmd->mes_int[11], '2');
    CHECK_EQ(pmd->mes_int[4], '5');
    CHECK_EQ(pmd->int_level, 12);

    // The initial failed probe still printed its warning; the re-probe did not.
    char expected[test_out_size] = "";
    append(expected, sizeof(expected), mes_title);
    append(expected, sizeof(expected), mes_warning);
    append(expected, sizeof(expected), mes_not_board);
    append(expected, sizeof(expected), pmd->mes_int);
    append(expected, sizeof(expected), pmd->mes_ongen1);
    append(expected, sizeof(expected), mes_int2);
    append(expected, sizeof(expected), mes_wait1);
    append(expected, sizeof(expected), "1");
    append(expected, sizeof(expected), mes_wait2);
    append(expected, sizeof(expected), "1");
    append(expected, sizeof(expected), mes_wait3);
    append(expected, sizeof(expected), mes_exit);
    append(expected, sizeof(expected), mes_key);
    CHECK_OUTPUT_EQUALS(expected);

    free_pmd(pmd);
}
#endif

#if va && !board2
// :9196 The VA probe path (PMDVA1) with a scripted board.
static const struct test_io va_board_script[] = {
    {0x45, 0xaa}, // VA+NORM check
    {0x45, 0x00}, // no VA+BOARD2
    {0x44, 0x00}, // int_init rdychk
    {0x44, 0x00},
    {0x44, 0x00},
    {0x44, 0x00},
    {0x45, 0xc0}, // int5
    // :8690 opnset44's rdychk, once per initial register write.
    {0x44, 0x00},
    {0x44, 0x00},
    {0x44, 0x00},
    {0x44, 0x00},
    {0x44, 0x00},
    {0x44, 0x00},
    {0x44, 0x00},
    {0x44, 0x00},
    {0x44, 0x00},
    {0x44, 0x00},
};

static void test_board_present(void) {
    ctx_reset();
    set_script(va_board_script,
               sizeof(va_board_script) / sizeof(va_board_script[0]));
    struct pmd *pmd = new_pmd();
    CHECK(run_resident(pmd, ""));
    CHECK_EQ(ctx.script_pos, ctx.script_len);

    CHECK_EQ(pmd->board, 1);
    CHECK_EQ(pmd->ongen, 0);
    CHECK_EQ(pmd->fm1_port1, 0x44);
    CHECK_EQ(pmd->fm1_port2, 0x45);
    CHECK_EQ(pmd->mes_int[4], '5');
    // The VA port text is fixed at 44H.
    CHECK_EQ(pmd->mes_int[11], '4');
    CHECK_EQ(pmd->int_level, 12);
    CHECK_EQ(pmd->wait_clock, 1);
    CHECK_EQ(pmd->wait1_clock, 1);

    char expected[test_out_size] = "";
    append(expected, sizeof(expected), mes_title);
    append(expected, sizeof(expected), pmd->mes_int);
    append(expected, sizeof(expected), mes_ongen0);
    append(expected, sizeof(expected), mes_int2);
    append(expected, sizeof(expected), mes_wait1);
    append(expected, sizeof(expected), "1");
    append(expected, sizeof(expected), mes_wait2);
    append(expected, sizeof(expected), "1");
    append(expected, sizeof(expected), mes_wait3);
    append(expected, sizeof(expected), mes_exit);
    append(expected, sizeof(expected), mes_key);
    CHECK_OUTPUT_EQUALS(expected);

    CHECK(has_write(0x44, 0x07));
    CHECK(has_write(0x45, 0x3f));
    CHECK(has_write(0x44, 0x0e));
    CHECK(has_write(0x44, 0x29));
    CHECK(has_write(0x44, 0x25));
    CHECK(has_write(0x44, 0x27));

    free_pmd(pmd);
}
#endif

// :9314 The 98+86B probe path (PMD86, PMDPPZE). The chip is switched on
// before int_init and nothing else reads the FM2 ports.
#if board2 && !va && (pcm || ppz) && !(adpcm && !ademu)
static const struct test_io board_script[] = {
    // check_86b: the OPNA ID port is present and 188h answers as a YM2608.
    {0xa460, 0x00},
    {0x18a, 0x01},
    {0x18c, 0xff},
    {0x18e, 0xff},
    // init_86b switches the NEC OPNA on and unmutes a66e B0.
    {0xa460, 0x00},
    {0xa66e, 0x00},
#if adpcm
    // opnset44(2983h) before the ADPCM RAM check (:8872). PMDPPZE emulates
    // ADPCM, so the check itself does not run.
    {0x188, 0x00},
    {0x188, 0x00},
#endif
    // int_init: two rdychk reads for each of the two register accesses.
    {0x188, 0x00},
    {0x188, 0x00},
    {0x188, 0x00},
    {0x188, 0x00},
    {0x18a, 0xc0}, // int5
};

static void test_board_present(void) {
    ctx_reset();
    set_script(board_script, sizeof(board_script) / sizeof(board_script[0]));
    ctx.pad_read = 0x08; // BUSY clear, BRDY set
    struct pmd *pmd = new_pmd();
    CHECK(run_resident(pmd, ""));
    CHECK_EQ(ctx.script_pos, ctx.script_len);

    CHECK_EQ(pmd->board, 1);
    CHECK_EQ(pmd->ongen, 1);
    CHECK_EQ(pmd->fm1_port1, 0x188);
    CHECK_EQ(pmd->fm1_port2, 0x18a);
    CHECK_EQ(pmd->fm2_port1, 0x18c);
    CHECK_EQ(pmd->fm2_port2, 0x18e);
    CHECK_EQ(pmd->fm_port1, 0x188);
    CHECK_EQ(pmd->fm_port2, 0x18a);
    CHECK_EQ(pmd->mes_int[11], '1');
    CHECK_EQ(pmd->mes_int[4], '5');
    CHECK_EQ(pmd->int_level, 12);
    CHECK_EQ(pmd->opn_0eh, 0xc0);
    CHECK_EQ(pmd->wait_clock, 1);
    CHECK_EQ(pmd->wait1_clock, 1);
    CHECK_EQ(pmd->ppsdrv_flag, 0);
    CHECK_EQ(pmd->kp_rhythm_flag, 1);
    CHECK_EQ(pmd->pcm_gs_flag, 1);
    CHECK(strcmp(pmd->mes_ongen1, "YM2608") == 0);

    CHECK(has_write(0x188, 0x07));
    CHECK(has_write(0x18a, 0x3f));
    CHECK(has_write(0x188, 0x0e));
#if adpcm
    CHECK(has_write(0x188, 0x29));
    CHECK(has_write(0x18a, 0x83));
#endif

    char expected[test_out_size] = "";
    append(expected, sizeof(expected), mes_title);
    append(expected, sizeof(expected), pmd->mes_int);
    append(expected, sizeof(expected), pmd->mes_ongen1);
#if pcm
    append(expected, sizeof(expected), mes_pcm);
#endif
    append(expected, sizeof(expected), mes_int2);
    append(expected, sizeof(expected), mes_wait1);
    append(expected, sizeof(expected), "1");
    append(expected, sizeof(expected), mes_wait2);
    append(expected, sizeof(expected), "1");
    append(expected, sizeof(expected), mes_wait3);
    append(expected, sizeof(expected), mes_exit);
    append(expected, sizeof(expected), mes_key);
    CHECK_OUTPUT_EQUALS(expected);

    free_pmd(pmd);
}

// :9314 The same 86B probe on PMDPPZ, where the ADPCM RAM check runs and
// reports the chip as a YMF288 when BRDY never arrives (ADRAMCHK.ASM:137).
#elif board2 && !va && adpcm && !ademu && (pcm || ppz)
static const struct test_io board_script[] = {
    {0xa460, 0x00},
    {0x18a, 0x01},
    {0x18c, 0xff},
    {0x18e, 0xff},
    {0xa460, 0x00},
    {0xa66e, 0x00},
    // opnset44(2983h) before the RAM check (:8872).
    {0x188, 0x00},
    {0x188, 0x00},
};

static void test_board_present(void) {
    ctx_reset();
    set_script(board_script, sizeof(board_script) / sizeof(board_script[0]));
    // BRDY never arrives, which is how the RAM check spots a YMF288. The
    // check's reads are interleaved with int_init's, so only the probe is
    // scripted; the rest falls back to an idle-looking status port.
    ctx.pad_read = 0x00;
    struct pmd *pmd = new_pmd();
    CHECK(run_resident(pmd, ""));
    CHECK_EQ(ctx.script_pos, ctx.script_len);

    CHECK_EQ(pmd->board, 1);
    CHECK_EQ(pmd->ongen, 1);
    CHECK_EQ(pmd->fm1_port1, 0x188);
    CHECK_EQ(pmd->fm1_port2, 0x18a);
    CHECK_EQ(pmd->fm2_port1, 0x18c);
    CHECK_EQ(pmd->fm2_port2, 0x18e);
    CHECK_EQ(pmd->mes_int[11], '1');
    CHECK_EQ(pmd->mes_int[4], '0');
    CHECK_EQ(pmd->int_level, 3);
    CHECK_EQ(pmd->wait_clock, 1);
    CHECK_EQ(pmd->wait1_clock, 1);
    CHECK(strcmp(pmd->mes_ongen1, "YMF288") == 0);
    CHECK_EQ(pmd->pcm_gs_flag, 1);

    CHECK(has_write(0x188, 0x07));
    CHECK(has_write(0x18a, 0x3f));
    CHECK(has_write(0x188, 0x0e));
    // :8872 the 2608 is switched on before the RAM check.
    CHECK(has_write(0x188, 0x29));
    CHECK(has_write(0x18a, 0x83));

    char expected[test_out_size] = "";
    append(expected, sizeof(expected), mes_title);
    append(expected, sizeof(expected), pmd->mes_int);
    append(expected, sizeof(expected), pmd->mes_ongen1);
    append(expected, sizeof(expected), mes_only);
    append(expected, sizeof(expected), mes_int2);
    append(expected, sizeof(expected), mes_wait1);
    append(expected, sizeof(expected), "1");
    append(expected, sizeof(expected), mes_wait2);
    append(expected, sizeof(expected), "1");
    append(expected, sizeof(expected), mes_wait3);
    append(expected, sizeof(expected), mes_exit);
    append(expected, sizeof(expected), mes_key);
    CHECK_OUTPUT_EQUALS(expected);

    free_pmd(pmd);
}

// :9196 The VA+BOARD2 probe path (PMDVA), including the OPN register init that
// the VA build does instead of opninit_loop, plus the ADPCM RAM check finding
// no RAM.
#elif board2 && va
static const struct test_io board_script[] = {
    {0x45, 0xaa}, // VA+NORM check
    {0x45, 0x01}, // VA+BOARD2 check
};

static void test_board_present(void) {
    ctx_reset();
    set_script(board_script, sizeof(board_script) / sizeof(board_script[0]));
    // BRDY always arrives, so the RAM check finds no ADPCM.
    ctx.pad_read = 0x08;
    struct pmd *pmd = new_pmd();
    CHECK(run_resident(pmd, ""));
    CHECK_EQ(ctx.script_pos, ctx.script_len);

    CHECK_EQ(pmd->board, 1);
    CHECK_EQ(pmd->ongen, 1);
    CHECK_EQ(pmd->fm1_port1, 0x44);
    CHECK_EQ(pmd->fm1_port2, 0x45);
    CHECK_EQ(pmd->fm2_port1, 0x46);
    CHECK_EQ(pmd->fm2_port2, 0x47);
    CHECK_EQ(pmd->mes_int[11], '4');
    CHECK_EQ(pmd->pcm_gs_flag, 1);
    CHECK(strcmp(pmd->mes_ongen1, "YM2608") == 0);
    CHECK_EQ(pmd->wait_clock, 1);
    CHECK_EQ(pmd->wait1_clock, 1);

    // :8690 the VA build writes the initial registers through 44h/45h.
    CHECK(has_write(0x44, 0x29));
    CHECK(has_write(0x44, 0x24));
    CHECK(has_write(0x44, 0x25));
    CHECK(has_write(0x44, 0x26));
    CHECK(has_write(0x44, 0x27));
    CHECK(has_write(0x45, 0x3f));

    char expected[test_out_size] = "";
    append(expected, sizeof(expected), mes_title);
    append(expected, sizeof(expected), pmd->mes_int);
    append(expected, sizeof(expected), pmd->mes_ongen1);
    append(expected, sizeof(expected), mes_only);
    append(expected, sizeof(expected), mes_int2);
    append(expected, sizeof(expected), mes_wait1);
    append(expected, sizeof(expected), "1");
    append(expected, sizeof(expected), mes_wait2);
    append(expected, sizeof(expected), "1");
    append(expected, sizeof(expected), mes_wait3);
    append(expected, sizeof(expected), mes_exit);
    append(expected, sizeof(expected), mes_key);
    CHECK_OUTPUT_EQUALS(expected);

    free_pmd(pmd);
}

// :9406 The 98+SPB probe path (PMDB2) followed by the ADPCM RAM check finding
// the RAM.
#elif board2
static const struct test_io board_script[] = {
    // check_spb: the OPNA ID port is present and 088h answers as a YM2608.
    {0xa460, 0x00},
    {0x08a, 0x01},
    {0x08c, 0x00},
    {0x08e, 0x00},
};

// The ADPCM RAM is expected to return the pattern pcmstore wrote
// (ADRAMCHK.ASM:385); the two leading bytes are the reads pget discards
// (ADRAMCHK.ASM:311).
static const uint8_t adpcm_readback[] = {
    0x00, 0x00, '*', '+', '=', '-', 'P', 'M', 'D', ' ', 'A', 'D', 'P',
    'C',  'M',  ' ', 'R', 'A', 'M', ' ', 'C', 'h', 'e', 'c', 'k', ' ',
    'D',  'a',  't', 'a', '-', '=', '+', '*',
};

// True when one opnset46 call wrote `reg`/`data` to the FM2 ports. pcmstore
// and pcmread both write the same setup, so the register/data pair has to be
// checked as a unit.
static bool has_fm2_write(uint8_t reg, uint8_t data) {
    for (size_t i = 0; i + 1 < ctx.write_count; i++) {
        if (ctx.writes[i].port == 0x08c && ctx.writes[i].value == reg &&
            ctx.writes[i + 1].port == 0x08e &&
            ctx.writes[i + 1].value == data) {
            return true;
        }
    }
    return false;
}

static void test_board_present(void) {
    ctx_reset();
    set_script(board_script, sizeof(board_script) / sizeof(board_script[0]));
    // BUSY clear, BRDY set. The ADPCM check's reads are interleaved with
    // int_init's, so only the probe and the transferred data are modelled.
    ctx.pad_read = 0x08;
    ctx.data_port = 0x08e;
    ctx.data_in = adpcm_readback;
    ctx.data_in_len = sizeof(adpcm_readback);
    struct pmd *pmd = new_pmd();
    CHECK(run_resident(pmd, ""));
    CHECK_EQ(ctx.script_pos, ctx.script_len);
    CHECK_EQ(ctx.data_in_pos, ctx.data_in_len);

    CHECK_EQ(pmd->board, 1);
    CHECK_EQ(pmd->ongen, 1);
    CHECK_EQ(pmd->fm1_port1, 0x088);
    CHECK_EQ(pmd->fm1_port2, 0x08a);
    CHECK_EQ(pmd->fm2_port1, 0x08c);
    CHECK_EQ(pmd->fm2_port2, 0x08e);
    CHECK_EQ(pmd->mes_int[11], '0');
    CHECK_EQ(pmd->mes_int[4], '0');
    CHECK_EQ(pmd->int_level, 3);
    CHECK_EQ(pmd->wait_clock, 1);
    CHECK_EQ(pmd->wait1_clock, 1);
    // The read-back matched, so the ADPCM RAM is present.
    CHECK_EQ(pmd->pcm_gs_flag, 0);
    CHECK(strcmp(pmd->mes_ongen1, "YM2608") == 0);
    CHECK_EQ(pmd->adpcm_wait, 0);

    char expected[test_out_size] = "";
    append(expected, sizeof(expected), mes_title);
    append(expected, sizeof(expected), pmd->mes_int);
    append(expected, sizeof(expected), pmd->mes_ongen1);
    append(expected, sizeof(expected), mes_adpcm);
    append(expected, sizeof(expected), mes_int2);
    append(expected, sizeof(expected), mes_wait1);
    append(expected, sizeof(expected), "1");
    append(expected, sizeof(expected), mes_wait2);
    append(expected, sizeof(expected), "1");
    append(expected, sizeof(expected), mes_wait3);
    append(expected, sizeof(expected), mes_exit);
    append(expected, sizeof(expected), mes_key);
    CHECK_OUTPUT_EQUALS(expected);

    CHECK(has_write(0x088, 0x07));
    CHECK(has_write(0x08a, 0x3f));
    CHECK(has_write(0x088, 0x0e));
    CHECK(has_write(0x088, 0x29));
    CHECK(has_write(0x08a, 0x83));
    // The ADPCM transfer selector and length (ADRAMCHK.ASM:72 onwards).
    CHECK(has_write(0x08c, 0x10));
    CHECK(has_write(0x08e, 0x17));
    // Transfer setup: registers 02h/03h hold the start address 1FFFh and
    // 04h/05h the end address FFFFh (ADRAMCHK.ASM:90-100). 03h = 01h is the
    // value a mistranscribed start-address high byte would produce.
    CHECK(has_fm2_write(0x02, 0xff));
    CHECK(has_fm2_write(0x03, 0x1f));
    CHECK(!has_fm2_write(0x03, 0x01));
    CHECK(has_fm2_write(0x04, 0xff));
    CHECK(has_fm2_write(0x05, 0xff));

    free_pmd(pmd);

    // When the faster speeds read back garbage and only the low-speed transfer
    // matches, the check falls through every speed (ADRAMCHK.ASM:29-55), so
    // this covers the fast, middle, and low-speed pcmstore/pcmread paths.
    static uint8_t readbacks[3 * sizeof(adpcm_readback)];
    memset(readbacks, 0, 2 * sizeof(adpcm_readback));
    memcpy(readbacks + 2 * sizeof(adpcm_readback), adpcm_readback,
           sizeof(adpcm_readback));

    ctx_reset();
    set_script(board_script, sizeof(board_script) / sizeof(board_script[0]));
    ctx.pad_read = 0x08;
    ctx.data_port = 0x08e;
    ctx.data_in = readbacks;
    ctx.data_in_len = sizeof(readbacks);
    pmd = new_pmd();
    CHECK(run_resident(pmd, ""));
    CHECK_EQ(ctx.script_pos, ctx.script_len);
    CHECK_EQ(ctx.data_in_pos, ctx.data_in_len);

    CHECK_EQ(pmd->pcm_gs_flag, 0);
    CHECK(strcmp(pmd->mes_ongen1, "YM2608") == 0);
    // The successful compare was the low-speed one, which leaves adpcm_wait
    // at 2.
    CHECK_EQ(pmd->adpcm_wait, 2);
    free_pmd(pmd);
}
#endif

#if ppz
// :8899 int_init's PPSDRV and PPZ8 setup. Both drivers are present, so the
// automatic check enables PPSDRV and runs the 0410h/0409h negotiation; the
// remaining reads come from the idle status pad, which reports interrupt
// number 0 (int_level 3) for the 0Eh read.
static const struct test_io ext_driver_script[] = {
    // check_86b: the OPNA ID port is present and 188h answers as a YM2608.
    {0xa460, 0x00},
    {0x18a, 0x01},
    {0x18c, 0xff},
    {0x18e, 0xff},
    // init_86b switches the NEC OPNA on and unmutes a66e B0.
    {0xa460, 0x00},
    {0xa66e, 0x00},
};

static void expect_ext_driver_output(const struct pmd *pmd) {
    char expected[test_out_size] = "";
    append(expected, sizeof(expected), mes_title);
    append(expected, sizeof(expected), pmd->mes_int);
    append(expected, sizeof(expected), pmd->mes_ongen1);
#if pcm
    append(expected, sizeof(expected), mes_pcm);
#endif
#if adpcm && !ademu
    append(expected, sizeof(expected), mes_only);
#endif
    append(expected, sizeof(expected), mes_int2);
    append(expected, sizeof(expected), mes_ppsdrv);
    append(expected, sizeof(expected), mes_ppz8);
    append(expected, sizeof(expected), mes_wait1);
    append(expected, sizeof(expected), "1");
    append(expected, sizeof(expected), mes_wait2);
    append(expected, sizeof(expected), "1");
    append(expected, sizeof(expected), mes_wait3);
    append(expected, sizeof(expected), mes_exit);
    append(expected, sizeof(expected), mes_key);
    CHECK_OUTPUT_EQUALS(expected);
}

static void test_board_ext_drivers(void) {
    static const struct pmd_drv_pps pps = {0};

    // The PPZ8 interrupt level matches int_level + 8, so the directly callable
    // FM entry from 0409h is usable (ppz_call_seg == 2).
    ctx_reset();
    set_script(ext_driver_script,
               sizeof(ext_driver_script) / sizeof(ext_driver_script[0]));
    // BUSY clear, BRDY set: the ADPCM RAM check finds no RAM.
    ctx.pad_read = 0x08;
    ctx.ppz_irq = 3 + 8;
    struct pmd *pmd = new_pmd();
    pmd->pps = &pps;
    pmd->ppz8 = &mock_ppz8;
    CHECK(run_resident(pmd, ""));
    CHECK_EQ(ctx.script_pos, ctx.script_len);

    CHECK_EQ(pmd->board, 1);
    CHECK_EQ(pmd->int_level, 3);
    CHECK_EQ(pmd->ppsdrv_flag, 1);
    CHECK_EQ(pmd->kp_rhythm_flag, 0);
    CHECK_EQ(pmd->ppz_call_seg, 2);
    CHECK_EQ(pmd->pcm_gs_flag, 1);
    CHECK_EQ(ctx.ppz_removal_calls, 1);
    CHECK(ctx.ppz_removal_protect); // 1901h
    expect_ext_driver_output(pmd);
    free_pmd(pmd);

    // A different PPZ8 interrupt level still counts as resident, but only
    // through the ordinary INT 7Fh calls.
    ctx_reset();
    set_script(ext_driver_script,
               sizeof(ext_driver_script) / sizeof(ext_driver_script[0]));
    ctx.pad_read = 0x08;
    ctx.ppz_irq = 4;
    pmd = new_pmd();
    pmd->pps = &pps;
    pmd->ppz8 = &mock_ppz8;
    CHECK(run_resident(pmd, ""));
    CHECK_EQ(ctx.script_pos, ctx.script_len);

    CHECK_EQ(pmd->ppz_call_seg, 1);
    CHECK_EQ(ctx.ppz_removal_calls, 1);
    CHECK(ctx.ppz_removal_protect);
    expect_ext_driver_output(pmd);
    free_pmd(pmd);
}
#endif

int main(void) {
    struct pmd *pmd = new_pmd();

    test_empty_dataarea(pmd);
    test_buffer_layout(pmd);
    test_part_data_table(pmd);
    test_qq_aliases(pmd);
    test_qq_address(pmd);
    test_defaults(pmd);
    test_message_buffer(pmd);

    free_pmd(pmd);

    test_host_pointers_preserved();
    test_buffer_init();
    test_no_board();
    test_null_hw();
    test_usage();
    test_capacity_limits();
    test_resident_options();
#if board2 && pcm
    test_hz_option();
#endif
    test_option_precedence();
    test_pps_option();
#if ppz
    test_ppz_options();
#endif
    test_change_status();
    test_change_options();
    test_board_present();
#if !va && !board2
    test_board_ppsdrv();
    test_portsel_reprobe();
#endif
#if ppz
    test_board_ext_drivers();
#endif

    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
