// The test host is a POSIX program: the YMFM ROM case needs a controlled
// working directory.
#define _POSIX_C_SOURCE 200809L

#include "pmd.h"
#include "pmd_ymfm.h"

#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    // The ADPCM RAM check alone writes ~350 registers during the resident
    // setup, so the log needs room for a whole board initialization.
    test_write_max = 2048,
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

    // The installed FM timer handler.
    void (*fm_timer_handler)(struct pmd *);
    unsigned fm_timer_installs;
    unsigned fm_timer_removals;

    // Timer callback observations, recorded separately per callback.
    unsigned fm_calls;
    unsigned efc_calls;
    uint8_t cb_fm_int5_flag;
    uint8_t cb_efc_int5_flag;
    uint8_t cb_timer_aflag;
    uint8_t cb_timer_bflag;

    // Normalized keyboard snapshot returned by pmd_hw.read_key_state.
    uint8_t keys;

    // Mock PPZ8 driver calls.
    unsigned ppz_fifo_stops;
    unsigned ppz_keyoffs[8];
    unsigned ppz_removal_calls;
    bool ppz_removal_protect;
    uint8_t ppz_irq;
    unsigned ppz_inits;
    unsigned ppz_stop_alls;
    unsigned ppz_keyons[8];
    uint16_t ppz_keyon_voice[8];
    unsigned ppz_volumes[8];
    uint8_t ppz_volume[8];
    unsigned ppz_tones[8];
    uint16_t ppz_tone[8];
    unsigned ppz_loops[8];
    uint32_t ppz_loop_start[8];
    uint32_t ppz_loop_end[8];
    unsigned ppz_pans[8];
    uint8_t ppz_pan[8];
    unsigned ppz_rates[8];
    uint16_t ppz_rate[8];
    unsigned ppz_adpcm_calls;
    bool ppz_adpcm_emulate;
    unsigned ppz_fm_int_calls;
    unsigned ppz_voice_calls;
    uint8_t ppz_voice_num;
    const struct pmd_ppz8_voice *ppz_voice;

    // Mock PPSDRV driver calls.
    unsigned pps_keyoffs;
    unsigned pps_keyons;
    struct pmd_pps_keyon pps_keyon;
    unsigned pps_pdr_switches;

    // The PCM86 FIFO status port answers from here: while fifo_pending is set
    // the first read reports the interrupt and clears it, as fifo_main does.
    bool fifo_pending;

    // Mock P86DRV driver calls.
    uint8_t p86_version;
    unsigned p86_pushes;
    unsigned p86_pops;
    unsigned p86_data_calls;
    uint32_t p86_offset;
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
    // The PCM86 FIFO status is a live flag rather than a scripted register:
    // fifo_main's reset write clears it, which test_hw_outb models. It never
    // consumes a script entry, so the timer scripts need no extra rows.
    if (port == 0xa468) {
        if (ctx->fifo_pending) {
            ctx->fifo_pending = false;
            return 0x10;
        }
        return 0;
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
    if (port == 0xa468 && (value & 0x10) == 0) ctx->fifo_pending = false;
    if (ctx->write_count >= test_write_max) {
        snprintf(ctx->hw_error, sizeof(ctx->hw_error), "outb(%#x): write limit",
                 port);
        longjmp(ctx->jmp, 2);
    }
    ctx->writes[ctx->write_count].port = port;
    ctx->writes[ctx->write_count].value = value;
    ctx->write_count++;
}

static void test_hw_install_fm_timer(void (*handler)(struct pmd *),
                                     void *user_data) {
    struct test_ctx *c = user_data;
    CHECK(user_data == &ctx);
    c->fm_timer_handler = handler;
    c->fm_timer_installs++;
}

static uint8_t test_hw_read_key_state(void *user_data) {
    struct test_ctx *ctx = user_data;
    return ctx->keys;
}

static void test_hw_remove_fm_timer(void *user_data) {
    struct test_ctx *ctx = user_data;
    ctx->fm_timer_handler = NULL;
    ctx->fm_timer_removals++;
}

static const struct pmd_hw test_hw = {
    .inb = test_hw_inb,
    .outb = test_hw_outb,
    .install_fm_timer_handler = test_hw_install_fm_timer,
    .remove_fm_timer_handler = test_hw_remove_fm_timer,
    .read_key_state = test_hw_read_key_state,
};

static void test_fm_callback(void *user_data) {
    struct pmd *pmd = user_data;
    ctx.fm_calls++;
    ctx.cb_fm_int5_flag = pmd->int5_flag;
    ctx.cb_timer_bflag = pmd->TimerBflag;
}

static void test_efc_callback(void *user_data) {
    struct pmd *pmd = user_data;
    ctx.efc_calls++;
    ctx.cb_efc_int5_flag = pmd->int5_flag;
    ctx.cb_timer_aflag = pmd->TimerAflag;
}

#if ppz
static void mock_ppz8_fifo_stop(void *user_data) {
    struct test_ctx *ctx = user_data;
    ctx->ppz_fifo_stops++;
}

static void mock_ppz8_init(void *user_data) {
    struct test_ctx *ctx = user_data;
    ctx->ppz_inits++;
}

static void mock_ppz8_stop_all(void *user_data) {
    struct test_ctx *ctx = user_data;
    ctx->ppz_stop_alls++;
}

static void mock_ppz8_keyoff(uint8_t channel, void *user_data) {
    struct test_ctx *ctx = user_data;
    if (channel < 8) ctx->ppz_keyoffs[channel]++;
}

static void mock_ppz8_keyon(uint8_t channel, uint16_t voice, void *user_data) {
    struct test_ctx *ctx = user_data;
    if (channel < 8) {
        ctx->ppz_keyons[channel]++;
        ctx->ppz_keyon_voice[channel] = voice;
    }
}

static void mock_ppz8_set_volume(uint8_t channel, uint8_t volume,
                                 void *user_data) {
    struct test_ctx *ctx = user_data;
    if (channel < 8) {
        ctx->ppz_volumes[channel]++;
        ctx->ppz_volume[channel] = volume;
    }
}

static void mock_ppz8_set_tone(uint8_t channel, uint16_t tone,
                               void *user_data) {
    struct test_ctx *ctx = user_data;
    if (channel < 8) {
        ctx->ppz_tones[channel]++;
        ctx->ppz_tone[channel] = tone;
    }
}

static void mock_ppz8_set_loop(uint8_t channel, uint32_t start, uint32_t end,
                               void *user_data) {
    struct test_ctx *ctx = user_data;
    if (channel < 8) {
        ctx->ppz_loops[channel]++;
        ctx->ppz_loop_start[channel] = start;
        ctx->ppz_loop_end[channel] = end;
    }
}

static void mock_ppz8_set_pan(uint8_t channel, uint8_t pan, void *user_data) {
    struct test_ctx *ctx = user_data;
    if (channel < 8) {
        ctx->ppz_pans[channel]++;
        ctx->ppz_pan[channel] = pan;
    }
}

static void mock_ppz8_set_rate(uint8_t channel, uint16_t rate,
                               void *user_data) {
    struct test_ctx *ctx = user_data;
    if (channel < 8) {
        ctx->ppz_rates[channel]++;
        ctx->ppz_rate[channel] = rate;
    }
}

static void mock_ppz8_adpcm(bool enable, void *user_data) {
    struct test_ctx *ctx = user_data;
    ctx->ppz_adpcm_calls++;
    ctx->ppz_adpcm_emulate = enable;
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

static const struct pmd_ppz8_voice *mock_ppz8_voice(uint8_t voice,
                                                    void *user_data) {
    struct test_ctx *ctx = user_data;
    ctx->ppz_voice_calls++;
    ctx->ppz_voice_num = voice;
    return ctx->ppz_voice;
}

static void mock_ppz8_fm_int(void *user_data) {
    struct test_ctx *ctx = user_data;
    ctx->ppz_fm_int_calls++;
}

static const struct pmd_drv_ppz8 mock_ppz8 = {
    .init = mock_ppz8_init,
    .stop_all = mock_ppz8_stop_all,
    .fifo_interrupt_stop = mock_ppz8_fifo_stop,
    .keyon = mock_ppz8_keyon,
    .keyoff = mock_ppz8_keyoff,
    .set_volume = mock_ppz8_set_volume,
    .set_tone = mock_ppz8_set_tone,
    .set_loop = mock_ppz8_set_loop,
    .set_pan = mock_ppz8_set_pan,
    .set_source_rate = mock_ppz8_set_rate,
    .set_adpcm_emulation = mock_ppz8_adpcm,
    .set_removal_protection = mock_ppz8_removal,
    .get_irq_level = mock_ppz8_irq,
    .get_voice = mock_ppz8_voice,
    .fm_int = mock_ppz8_fm_int,
};
#endif

// The PPSDRV mock (INT 64h) records the effect requests eff_main sends.
static void mock_pps_keyoff(void *user_data) {
    struct test_ctx *ctx = user_data;
    ctx->pps_keyoffs++;
}

static void mock_pps_keyon(struct pmd_pps_keyon keyon, void *user_data) {
    struct test_ctx *ctx = user_data;
    ctx->pps_keyons++;
    ctx->pps_keyon = keyon;
}

static void mock_pps_pdr_switch(uint8_t value, void *user_data) {
    struct test_ctx *ctx = user_data;
    ctx->pps_pdr_switches++;
    (void)value;
}

static const struct pmd_drv_pps mock_pps = {
    .keyoff = mock_pps_keyoff,
    .keyon = mock_pps_keyon,
    .pdr_switch = mock_pps_pdr_switch,
};

#if board2 && pcm
// The P86DRV mock (INT 65h). get_data_offset resolves wide offsets into a
// fixed, zero-filled sample area; the FIFO transfer tests read from it.
enum {
    mock_pcm86_area_size = 0x10000,
};

static uint8_t mock_pcm86_area[mock_pcm86_area_size];

static uint8_t mock_p86_version(void *user_data) {
    struct test_ctx *ctx = user_data;
    return ctx->p86_version;
}

static void mock_p86_push(void *user_data) {
    struct test_ctx *ctx = user_data;
    ctx->p86_pushes++;
}

static void mock_p86_pop(void *user_data) {
    struct test_ctx *ctx = user_data;
    ctx->p86_pops++;
}

static const uint8_t *mock_p86_data(uint32_t offset, void *user_data) {
    struct test_ctx *ctx = user_data;
    ctx->p86_data_calls++;
    ctx->p86_offset = offset;
    if (offset >= mock_pcm86_area_size) return NULL;
    return mock_pcm86_area + offset;
}

static const struct pmd_drv_p86 mock_p86 = {
    .get_version = mock_p86_version,
    .push_ems = mock_p86_push,
    .pop_ems = mock_p86_pop,
    .get_data_offset = mock_p86_data,
};
#endif

static void ctx_reset(void) {
    memset(&ctx, 0, sizeof(ctx));
    ctx.exit_status = -1;
    ctx.pad_read = 0xff;
    ctx.data_port = 0xffff;
    ctx.p86_version = 0x10;
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
    ctx.fm_timer_installs = 0;
    if (setjmp(ctx.jmp) == 0) {
        pmd_resident_main(pmd, (char *)cmdline);
        // A board-present run installs the driver's FM timer handler; the
        // no-board path never touches the hardware vtable hooks.
        if (pmd->board != 0) {
            CHECK_EQ(ctx.fm_timer_installs, 1);
            CHECK(ctx.fm_timer_handler != NULL);
        } else {
            CHECK_EQ(ctx.fm_timer_installs, 0);
        }
        return true;
    }
    report_unwind("pmd_resident_main");
    return false;
}

// Runs pmd_resident_main against the variant's board script so the driver
// installs its FM timer handler. Each variant's board setup defines it.
static bool run_board_resident(struct pmd *pmd);

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
    append(expected, sizeof(expected), mes_cut);
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

// True when the recorded writes hold an OPN register write: `reg` to the
// address port immediately followed by `value` to the data port.
static bool has_reg_write(uint16_t port1, uint16_t port2, uint8_t reg,
                          uint8_t value) {
    for (size_t i = 0; i + 1 < ctx.write_count; i++) {
        if (ctx.writes[i].port == port1 && ctx.writes[i].value == reg &&
            ctx.writes[i + 1].port == port2 &&
            ctx.writes[i + 1].value == value) {
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

static bool run_board_resident(struct pmd *pmd) {
    ctx_reset();
    set_script(board_script, sizeof(board_script) / sizeof(board_script[0]));
    ctx.pad_read = 0x00;
    return run_resident(pmd, "");
}

static void test_board_present(void) {
    ctx_reset();
    set_script(board_script, sizeof(board_script) / sizeof(board_script[0]));
    ctx.pad_read = 0x00;
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
    const struct pmd_drv_pps *pps = &mock_pps;

    ctx_reset();
    set_script(board_script, sizeof(board_script) / sizeof(board_script[0]));
    ctx.pad_read = 0x00;
    struct pmd *pmd = new_pmd();
    pmd->pps = pps;
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

static bool run_board_resident(struct pmd *pmd) {
    ctx_reset();
    set_script(va_board_script,
               sizeof(va_board_script) / sizeof(va_board_script[0]));
    ctx.pad_read = 0x00;
    return run_resident(pmd, "");
}

static void test_board_present(void) {
    ctx_reset();
    set_script(va_board_script,
               sizeof(va_board_script) / sizeof(va_board_script[0]));
    ctx.pad_read = 0x00;
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

static bool run_board_resident(struct pmd *pmd) {
    ctx_reset();
    set_script(board_script, sizeof(board_script) / sizeof(board_script[0]));
    ctx.pad_read = 0x08; // BUSY clear, BRDY set
    return run_resident(pmd, "");
}

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
    CHECK_EQ(pmd->fm_port1, 0x18c);
    CHECK_EQ(pmd->fm_port2, 0x18e);
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

static bool run_board_resident(struct pmd *pmd) {
    ctx_reset();
    set_script(board_script, sizeof(board_script) / sizeof(board_script[0]));
    // BRDY never arrives, which is how the RAM check spots a YMF288.
    ctx.pad_read = 0x00;
    return run_resident(pmd, "");
}

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

static bool run_board_resident(struct pmd *pmd) {
    ctx_reset();
    set_script(board_script, sizeof(board_script) / sizeof(board_script[0]));
    // BRDY always arrives, so the RAM check finds no ADPCM.
    ctx.pad_read = 0x08;
    return run_resident(pmd, "");
}

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

static bool run_board_resident(struct pmd *pmd) {
    ctx_reset();
    set_script(board_script, sizeof(board_script) / sizeof(board_script[0]));
    // BUSY clear, BRDY set; the ADPCM read-back does not match, which does not
    // matter to the timer tests.
    ctx.pad_read = 0x08;
    return run_resident(pmd, "");
}

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
    const struct pmd_drv_pps *pps = &mock_pps;

    // The PPZ8 interrupt level matches int_level + 8, so the directly callable
    // FM entry from 0409h is usable (ppz_call_seg == 2).
    ctx_reset();
    set_script(ext_driver_script,
               sizeof(ext_driver_script) / sizeof(ext_driver_script[0]));
    // BUSY clear, BRDY set: the ADPCM RAM check finds no RAM.
    ctx.pad_read = 0x08;
    ctx.ppz_irq = 3 + 8;
    struct pmd *pmd = new_pmd();
    pmd->pps = pps;
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
    pmd->pps = pps;
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

// Invokes the installed handler, turning a hardware-script failure into a
// reported failure instead of unwinding into a dead frame.
static void run_fm_handler(struct pmd *pmd) {
    if (setjmp(ctx.jmp) != 0) {
        report_unwind("fm_timer_handler");
        return;
    }
    ctx.fm_timer_handler(pmd);
}

// The status reads consumed by one installed-handler invocation: opnint's
// pending check (two reads), FM_Timer_main's rdychk (two reads) and its 27h
// status read, then opnint's recheck (two reads). `pending` is the value
// opnint's check sees and `status` the value the 27h read returns.
static void fire_fm_timer(struct pmd *pmd, uint8_t pending, uint8_t status) {
    uint16_t port1 = pmd->fm1_port1;
    const struct test_io script[] = {
#if vsync
        // :7524-7531 and :7606-7613 read port 2 to stop and restore VSync.
        {2, 0x00},
#endif
        {port1, 0x00},
        {port1, pending},
        {port1, 0x00},
        {port1, 0x00},
        {port1, status},
        {port1, 0x00},
        {port1, 0x00},
#if vsync
        {2, 0x00},
#endif
    };
    set_script(script, sizeof(script) / sizeof(script[0]));
    ctx.reads = 0;
    ctx.write_count = 0;
    run_fm_handler(pmd);
    CHECK_EQ(ctx.script_pos, ctx.script_len);
}

// A Timer A invocation while the SSG effect driver runs. Only the timer
// dispatch reads are scripted: the effect's own register writes read the status
// port through the idle pad, which reports a clear BUSY bit.
static void fire_effect_timer(struct pmd *pmd) {
    uint16_t port1 = pmd->fm1_port1;
    const struct test_io script[] = {
#if vsync
        {2, 0x00},
#endif
        {port1, 0x00},
        {port1, 1}, // Timer A pending
        {port1, 0x00},
        {port1, 0x00},
        {port1, 1}, // 27h status: Timer A
    };
    set_script(script, sizeof(script) / sizeof(script[0]));
    ctx.reads = 0;
    ctx.write_count = 0;
    run_fm_handler(pmd);
    CHECK_EQ(ctx.script_pos, ctx.script_len);
}

// The same for a handler invocation whose timer work reaches the sequencer and
// so the hardware: only the reads up to the timer dispatch are scripted, and
// the rest read the idle pad.
static void fire_fm_timer_busy(struct pmd *pmd, uint8_t pending, uint8_t status) {
    uint16_t port1 = pmd->fm1_port1;
    const struct test_io script[] = {
#if vsync
        {2, 0x00},
#endif
        {port1, 0x00},
        {port1, pending},
        {port1, 0x00},
        {port1, 0x00},
        {port1, status},
    };
    set_script(script, sizeof(script) / sizeof(script[0]));
    ctx.reads = 0;
    ctx.write_count = 0;
    run_fm_handler(pmd);
    CHECK_EQ(ctx.script_pos, ctx.script_len);
}

// Runs the variant's board-present resident setup, which installs the FM timer
// handler, and seeds the timer state that phase 5's data_init will own.
static struct pmd *new_timer_pmd(void) {
    struct pmd *pmd = alloc_pmd();
    pmd_init(pmd);
    CHECK(run_board_resident(pmd));
    CHECK(ctx.fm_timer_handler != NULL);
    pmd->TimerAtime = 10;
    pmd->lastTimerAtime = 20;
    pmd->play_flag = 0;
    pmd->ch3mode = 0x3f;
    // The resident run leaves the log full of board-initialization writes.
    ctx.write_count = 0;
    return pmd;
}

static void test_timer_a(void) {
    struct pmd *pmd = new_timer_pmd();

    fire_fm_timer(pmd, 1, 1);

    CHECK_EQ(pmd->TimerAtime, 11);
    // Timer B did not run, so it did not record the bar counter.
    CHECK_EQ(pmd->lastTimerAtime, 20);
    CHECK_EQ(pmd->TimerAflag, 0);
    CHECK_EQ(pmd->TimerBflag, 0);
    // The nesting counter returns to its previous value.
    CHECK_EQ(pmd->int5_flag, 0);
    // :7669-7678 the 27h address and ch3mode reset the timer flags.
    CHECK(has_write(pmd->fm1_port1, 0x27));
    CHECK(has_write(pmd->fm1_port2, 0x3f));

    free_pmd(pmd);
}

static void test_timer_b(void) {
    struct pmd *pmd = new_timer_pmd();
    pmd->play_flag = 1;

    fire_fm_timer(pmd, 2, 2);

    CHECK_EQ(pmd->TimerAtime, 10);
#if sync
    // :7726-7729 Timer B is inert under MIDI sync, so it does not record the
    // bar counter.
    CHECK_EQ(pmd->lastTimerAtime, 20);
#else
    // :7747-7751 Timer B stores the bar counter so Timer A can detect a whole
    // bar, so it records the unchanged count here.
    CHECK_EQ(pmd->lastTimerAtime, 10);
#endif
    CHECK_EQ(pmd->TimerAflag, 0);
    CHECK_EQ(pmd->TimerBflag, 0);
    CHECK_EQ(pmd->int5_flag, 0);

    free_pmd(pmd);
}

static void test_timer_ab(void) {
    struct pmd *pmd = new_timer_pmd();
    pmd->play_flag = 1;

    fire_fm_timer(pmd, 3, 3);

    // :7706 Timer B runs before Timer A for a simultaneous interrupt, so the
    // recorded counter is the one from before Timer A's increment.
    CHECK_EQ(pmd->TimerAtime, 11);
#if sync
    // :7726-7729 Timer B is inert under MIDI sync.
    CHECK_EQ(pmd->lastTimerAtime, 20);
#else
    CHECK_EQ(pmd->lastTimerAtime, 10);
#endif
    CHECK_EQ(pmd->TimerAflag, 0);
    CHECK_EQ(pmd->TimerBflag, 0);
    CHECK_EQ(pmd->int5_flag, 0);

    free_pmd(pmd);
}

static void test_timer_status_zero(void) {
    struct pmd *pmd = new_timer_pmd();
    pmd->play_flag = 1;

    // :7701-7706 The two `dec ah; jz` tests fall through to both handlers when
    // the 27h status is 0, which opnint's own check cannot produce because it
    // would already have returned. Preserve the original's behavior.
    fire_fm_timer(pmd, 1, 0);

    CHECK_EQ(pmd->TimerAtime, 11);
#if sync
    // :7726-7729 Timer B is inert under MIDI sync.
    CHECK_EQ(pmd->lastTimerAtime, 20);
#else
    CHECK_EQ(pmd->lastTimerAtime, 10);
#endif
    CHECK_EQ(pmd->int5_flag, 0);

    free_pmd(pmd);
}

// :7499 int5_flag is a nesting counter, not a reentrancy guard: a handler
// entered while it is already set keeps counting and still runs its work.
static void test_timer_nesting(void) {
    struct pmd *pmd = new_timer_pmd();
    pmd->play_flag = 1;
    pmd->fmint = (struct pmd_int_handler){test_fm_callback, pmd};
    pmd->intfook_flag = 1;

    pmd->int5_flag = 1;
    fire_fm_timer(pmd, 2, 2);
#if sync
    // :7726-7729 Timer B is inert under MIDI sync, so no callback observes the
    // counter.
    CHECK_EQ(pmd->lastTimerAtime, 20);
#else
    CHECK_EQ(ctx.cb_fm_int5_flag, 2);
    CHECK_EQ(pmd->lastTimerAtime, 10);
#endif
    CHECK_EQ(pmd->TimerBflag, 0);
    CHECK_EQ(pmd->int5_flag, 1);

    // The counter is a byte, so consecutive interrupts wrap it rather than
    // saturating or refusing the second one.
    pmd->int5_flag = 0xff;
    fire_fm_timer(pmd, 2, 2);
#if !sync
    CHECK_EQ(ctx.cb_fm_int5_flag, 0);
#endif
    CHECK_EQ(pmd->int5_flag, 0xff);

    free_pmd(pmd);
}

static void test_timer_callbacks(void) {
    struct pmd *pmd = new_timer_pmd();
    pmd->play_flag = 1;
    pmd->fmint = (struct pmd_int_handler){test_fm_callback, pmd};
    pmd->efcint = (struct pmd_int_handler){test_efc_callback, pmd};
    pmd->intfook_flag = 3;

    fire_fm_timer(pmd, 3, 3);

#if sync
    // :7726-7729 Timer B is inert under MIDI sync, so its callback does not
    // run; Timer A's still does.
    CHECK_EQ(ctx.fm_calls, 0);
#else
    CHECK_EQ(ctx.fm_calls, 1);
    // :7755-7757 The Timer B callback runs after its flag is cleared, while
    // int5_flag still counts the handler as active.
    CHECK_EQ(ctx.cb_timer_bflag, 0);
    CHECK_EQ(ctx.cb_fm_int5_flag, 1);
#endif
    CHECK_EQ(ctx.efc_calls, 1);
    // :7822-7824 The Timer A callback runs after its flag is cleared and while
    // int5_flag still counts the handler as active.
    CHECK_EQ(ctx.cb_efc_int5_flag, 1);
    CHECK_EQ(ctx.cb_timer_aflag, 0);
    CHECK_EQ(pmd->int5_flag, 0);

    // The intfook_flag bits gate the callbacks.
    ctx.fm_calls = 0;
    ctx.efc_calls = 0;
    pmd->intfook_flag = 0;
    fire_fm_timer(pmd, 3, 3);
    CHECK_EQ(ctx.fm_calls, 0);
    CHECK_EQ(ctx.efc_calls, 0);
    // int5_flag returns to zero between interrupts because the handler has no
    // early-return guard.
    CHECK_EQ(pmd->int5_flag, 0);

    free_pmd(pmd);
}

#if sync
static void test_timer_sync(void) {
    struct pmd *pmd = new_timer_pmd();
    pmd->play_flag = 1;

    // :7726-7729 The timer's Timer B path is inert under MIDI sync.
    fire_fm_timer(pmd, 2, 2);
    CHECK_EQ(pmd->TimerAtime, 10);
    CHECK_EQ(pmd->lastTimerAtime, 20);

    // :7728 opnint_sub runs the same work for the AH=FFh entry point.
    pmd_opnint_sub(pmd);
    CHECK_EQ(pmd->lastTimerAtime, 10);
    CHECK_EQ(pmd->TimerBflag, 0);

    free_pmd(pmd);
}
#endif

#if vsync
static void test_timer_vsync(void) {
    struct pmd *pmd = new_timer_pmd();
    // :7524-7531 and :7606-7613 stop VSync around the handler and restore it
    // afterwards; fire_fm_timer scripts the two port 2 reads.
    fire_fm_timer(pmd, 1, 1);

    CHECK(has_write(2, 0x04));
    CHECK(has_write(2, 0x00));
    CHECK_EQ(pmd->TimerAtime, 11);
    CHECK_EQ(pmd->int5_flag, 0);

    free_pmd(pmd);
}

// A Timer A handler invocation whose entry and exit port 2 reads are scripted
// separately, so the restore can be observed against the saved state.
static void fire_vsync_timer(struct pmd *pmd, uint8_t saved, uint8_t live) {
    uint16_t port1 = pmd->fm1_port1;
    const struct test_io script[] = {
        {2, saved},
        {port1, 0x00},
        {port1, 1},
        {port1, 0x00},
        {port1, 0x00},
        {port1, 1},
        {port1, 0x00},
        {port1, 0x00},
        {2, live},
    };
    set_script(script, sizeof(script) / sizeof(script[0]));
    ctx.reads = 0;
    ctx.write_count = 0;
    run_fm_handler(pmd);
    CHECK_EQ(ctx.script_pos, ctx.script_len);
}

// :7606-7613 The restore puts back the state saved on entry rather than
// unconditionally restarting VSync.
static void test_timer_vsync_restore(void) {
    struct pmd *pmd = new_timer_pmd();

    // VSync was already stopped on entry, so it stays stopped.
    fire_vsync_timer(pmd, 0x04, 0x04);
    CHECK(has_write(2, 0x04));
    CHECK(!has_write(2, 0x00));

    // :7607-7610 `and al,dl` restores the saved bit only when the live read
    // still has it set; it is not forced back on.
    fire_vsync_timer(pmd, 0x04, 0x00);
    CHECK(has_write(2, 0x04));
    CHECK(has_write(2, 0x00));

    free_pmd(pmd);
}
#endif

// Phase 5: a minimal module image. Every part points at the end marker, so the
// sequencer only runs the parts a test loads explicitly.
static void load_empty_module(struct pmd *pmd) {
    uint8_t *m = pmd->mmlbuf;
    memset(m, 0, 0x200);
    for (int i = 0; i < 11; i++) {
        m[2 * i] = 0x60;
        m[2 * i + 1] = 0;
    }
    m[0x60] = 0x80;       // end marker
    pmd->dataarea[0] = 0; // x68_flg
}

// The module part-table indices of SSG1 and of the rhythm part (the original's
// part7 and part11); the table layout is the same in every variant.
enum {
    test_part_ssg1 = 6,
    test_part_ssg3 = 8,
    test_part_pcm = 9,
    test_part_rhythm = 10,
    // The PPZ8 parts, whose work areas ppz_extpartset (0b4h) fills.
    test_part_ppz1 = 14,
};

// Points part `index` (0 = FM1) at `off` and copies its data there.
static void load_part(struct pmd *pmd, int index, size_t off,
                      const uint8_t *data, size_t len) {
    pmd->mmlbuf[2 * index] = (uint8_t)off;
    pmd->mmlbuf[2 * index + 1] = (uint8_t)(off >> 8);
    memcpy(pmd->mmlbuf + off, data, len);
}

static void load_module(struct pmd *pmd, const uint8_t *part, size_t len) {
    load_empty_module(pmd);
    // FM1 starts at 24; its low byte also selects the "no program area" path.
    load_part(pmd, 0, 24, part, len);
    // Rhythm address table (unused because part 11 points at the end marker).
    pmd->mmlbuf[22] = 0x60;
    pmd->mmlbuf[23] = 0;
}

// Loads a stream into the SSG1 part (part 7).
static void load_ssg_module(struct pmd *pmd, const uint8_t *part, size_t len) {
    load_empty_module(pmd);
    load_part(pmd, test_part_ssg1, 24, part, len);
    pmd->mmlbuf[22] = 0x60;
    pmd->mmlbuf[23] = 0;
}

static struct pmd *setup_playback(void) {
    struct pmd *pmd = new_timer_pmd();
    ctx.script = NULL;
    ctx.script_len = 0;
    ctx.script_pos = 0;
    ctx.pad_read = 0x00;
    ctx.write_count = 0;
    ctx.reads = 0;
    return pmd;
}

static void start_playback(struct pmd *pmd) {
    ctx.write_count = 0;
    CHECK(pmd_mstart_f(pmd));
    CHECK_EQ(pmd->play_flag, 1);
}

// Runs one Timer B tick. The timer-status reads are scripted; the playback
// register writes that follow read from the idle pad.
static void run_timer_b(struct pmd *pmd) {
#if sync
    ctx.write_count = 0;
    ctx.reads = 0;
    pmd_opnint_sub(pmd);
#else
    uint16_t port1 = pmd->fm1_port1;
    const struct test_io script[] = {
#if vsync
        {2, 0x00},
#endif
        {port1, 0x00},
        {port1, 2}, // Timer B pending
        {port1, 0x00},
        {port1, 0x00},
        {port1, 2}, // 27h status: Timer B
    };
    set_script(script, sizeof(script) / sizeof(script[0]));
    ctx.pad_read = 0x00;
    ctx.reads = 0;
    ctx.write_count = 0;
    run_fm_handler(pmd);
    CHECK_EQ(ctx.script_pos, ctx.script_len);
#endif
}

// :440/:874 start and stop, including the busy deferral.
static void test_playback_start_stop(void) {
    struct pmd *pmd = setup_playback();
    static const uint8_t part[] = {0x40, 1, 0x80};
    load_module(pmd, part, sizeof(part));

    start_playback(pmd);
    CHECK_EQ(pmd->mstart_flag, 1);
    CHECK_EQ(pmd->fadeout_volume, 0);
    // opn_init, then setint's Timer A/B setup.
    CHECK(has_write(pmd->fm1_port1, 0x29));
    CHECK(has_write(pmd->fm1_port1, 0x25));
    CHECK(has_write(pmd->fm1_port1, 0x24));
    CHECK(has_write(pmd->fm1_port1, 0x27));
    // :729-741 SSG-EG reset writes 90h-9Eh, skipping every fourth operator,
    // and leaves the SSG volume/envelope registers alone.
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x90, 0x00));
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x9e, 0x00));
    CHECK(!has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x93, 0x00));
    CHECK(!has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x09, 0x00));
    CHECK(!has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x0e, 0x00));

    // A running timer defers both start and stop to Timer B.
    pmd->TimerAflag = 1;
    CHECK(!pmd_mstart_f(pmd));
    CHECK_EQ(pmd->music_flag & 1, 1);
    pmd->music_flag = 0;
    CHECK(!pmd_mstop_f(pmd));
    CHECK_EQ(pmd->music_flag & 2, 2);
    pmd->TimerAflag = 0;

    CHECK(pmd_mstop_f(pmd));
    CHECK_EQ(pmd->play_flag, 0);
    CHECK_EQ(pmd->status2, 0xff);
    CHECK_EQ(pmd->fadeout_volume, 0xff);
    CHECK(has_write(pmd->fm1_port1, 0x07));
    CHECK(has_write(pmd->fm1_port2, 0xbf)); // PSG keyoff
    // :6054 FM KEYOFF: one 28h write per channel, not one register each.
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x28, 0x00));
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x28, 0x01));
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x28, 0x02));
    CHECK(!has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x29, 0x00));
#if board2
    // :6074 the back chip's channels: 28h bit 2 selects the second chip.
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x28, 0x04));
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x28, 0x05));
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x28, 0x06));
#endif

    free_pmd(pmd);
}

// :1053 fmmain's note path: pitch, gate, key-on, and cursor advance.
static void test_playback_note(void) {
    struct pmd *pmd = setup_playback();
    static const uint8_t part[] = {0x40, 1, 0x80};
    load_module(pmd, part, sizeof(part));
    start_playback(pmd);

    run_timer_b(pmd);

    struct pmd_qq *di = &pmd->parts[0];
    CHECK_EQ(di->onkai, 0x40);
    CHECK_EQ(di->fnum, 0x226a); // C, block 4
    CHECK_EQ(di->leng, 1);
    CHECK_EQ(di->keyon_flag, 1);
    CHECK_EQ(di->keyoff_flag, 0);
    CHECK(di->address == pmd->mmlbuf + 26);
    CHECK(has_write(pmd->fm1_port1, 0xa4));
    CHECK(has_write(pmd->fm1_port2, 0x22));
    CHECK(has_write(pmd->fm1_port1, 0xa0));
    CHECK(has_write(pmd->fm1_port2, 0x6a));
    CHECK(has_write(pmd->fm1_port1, 0x28));
    CHECK(has_write(pmd->fm1_port2, 0xf0));

    free_pmd(pmd);

    // The tone tables are word arrays: a non-zero note index must not be
    // scaled by two. D4 and the PSG D4 with its octave shift and rounding.
    static const uint8_t d4[] = {0x42, 1, 0x80};
    pmd = setup_playback();
    load_module(pmd, d4, sizeof(d4));
    start_playback(pmd);
    run_timer_b(pmd);
    CHECK_EQ(pmd->parts[0].fnum, 0x22b6);
    free_pmd(pmd);

    pmd = setup_playback();
    load_ssg_module(pmd, d4, sizeof(d4));
    start_playback(pmd);
    run_timer_b(pmd);
    CHECK_EQ(pmd->part_data_table[test_part_ssg1]->fnum, 0xd5);
    free_pmd(pmd);
}

// :5645 lfoinit: 'x' is the note byte 0ch and replays the last defined note,
// which the caller's AL must carry on to oshift and fnumset.
static void test_playback_note_repeat(void) {
    struct pmd *pmd = setup_playback();
    static const uint8_t part[] = {0x40, 1, 0x0c, 1, 0x80};
    load_module(pmd, part, sizeof(part));
    start_playback(pmd);

    run_timer_b(pmd);
    struct pmd_qq *di = &pmd->parts[0];
    CHECK_EQ(di->onkai, 0x40);
    CHECK_EQ(di->onkai_def, 0x40);
    CHECK_EQ(di->fnum, 0x226a); // C, block 4

    run_timer_b(pmd);
    CHECK_EQ(di->onkai, 0x40);
    CHECK_EQ(di->onkai_def, 0x40);
    CHECK_EQ(di->fnum, 0x226a);
    free_pmd(pmd);
}

// :5274 toneadr_calc: the program-change area holds 26-byte entries, one
// program number byte followed by 25 voice bytes.
static void test_playback_program_voice(void) {
    struct pmd *pmd = setup_playback();
    uint8_t *m = pmd->mmlbuf;
    enum {
        part_off = 26,
        prg_off = 0x80,
    };

    memset(m, 0, 0x200);
    for (int i = 1; i < 11; i++) {
        m[2 * i] = 0x60;
        m[2 * i + 1] = 0;
    }
    m[0x60] = 0x80;
    // A first part offset other than 2 * (max_part2 + 1) announces the
    // program area, whose offset sits in the word after the part table.
    m[0] = part_off;
    m[24] = prg_off;
    m[25] = 0;

    uint8_t *entry0 = m + prg_off;
    uint8_t *entry1 = entry0 + 26;
    entry0[0] = 0;
    entry1[0] = 1;
    for (int i = 1; i < 26; i++) {
        entry0[i] = 0x11;
        entry1[i] = 0x22;
    }

    static const uint8_t part[] = {0xff, 1, 0x80}; // '@1'
    memcpy(m + part_off, part, sizeof(part));

    start_playback(pmd);
    run_timer_b(pmd);

    struct pmd_qq *di = &pmd->parts[0];
    CHECK_EQ(di->voicenum, 1);
    // With no slot mask the tone address only reaches the TL work area.
    CHECK_EQ(di->slot1, 0x22);
    CHECK_EQ(di->slot3, 0x22);
    CHECK_EQ(di->slot2, 0x22);
    CHECK_EQ(di->slot4, 0x22);
    free_pmd(pmd);
}

// :4549 fm_block_calc: the upper bound is 26ah * 2 (4d4h), not the 4d2h its
// stale comment names. Landing on 4d2h exactly oscillates forever otherwise.
static void test_playback_detune_block(void) {
    struct pmd *pmd = setup_playback();
    static const uint8_t part[] = {0xfa, 0xfe, 0xff, 0x40, 1, 0x80}; // D-2
    load_module(pmd, part, sizeof(part));
    start_playback(pmd);

    run_timer_b(pmd);
    // 226ah - 2 is below the C block's range, so the block drops to 3.
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0xa4, 0x1c));
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0xa0, 0xd2));
    free_pmd(pmd);
}

// :3406/:3417 'v' and 't': volume and tempo commands, and the tempo write.
static void test_playback_commands(void) {
    struct pmd *pmd = setup_playback();
    static const uint8_t part[] = {0x40, 1, 0xfd, 100, 0xfc, 100, 0x80};
    load_module(pmd, part, sizeof(part));
    start_playback(pmd);

    run_timer_b(pmd); // play the note
    CHECK_EQ(pmd->parts[0].volume, 108);

    run_timer_b(pmd); // process v/t and the end marker
    CHECK_EQ(pmd->parts[0].volume, 100);
    CHECK_EQ(pmd->tempo_d, 100);
    CHECK_EQ(pmd->tempo_48, 28); // 112Ch / (100h - 64h) = 28.1C
    CHECK_EQ(pmd->parts[0].loopcheck, 3);
    CHECK_EQ(pmd->status2, 0xff);
    CHECK(has_write(pmd->fm1_port1, 0x26));
    CHECK(has_write(pmd->fm1_port2, 100));

    free_pmd(pmd);
}

// :3545/:3613 'L': the end marker loops back to the saved address.
static void test_playback_loop(void) {
    struct pmd *pmd = setup_playback();
    static const uint8_t part[] = {0x40, 1, 0xf6, 0x41, 1, 0x80};
    load_module(pmd, part, sizeof(part));
    start_playback(pmd);

    run_timer_b(pmd); // first note
    run_timer_b(pmd); // 'L' then the second note
    struct pmd_qq *di = &pmd->parts[0];
    CHECK(di->partloop == pmd->mmlbuf + 27);
    CHECK_EQ(di->onkai, 0x41);
    CHECK_EQ(di->keyon_flag, 2);

    run_timer_b(pmd); // end marker loops to partloop
    // The loop flag was consumed before mmain cleared the per-part check.
    CHECK_EQ(pmd->loop_work, 1);
    CHECK_EQ(di->loopcheck, 0);
    CHECK_EQ(di->keyon_flag, 3);
    CHECK_EQ(di->onkai, 0x41);

    free_pmd(pmd);
}

// :3029/:5967 fade command and Timer A's fadeout step.
static void test_playback_fade(void) {
    struct pmd *pmd = setup_playback();
    static const uint8_t part[] = {0x40, 1, 0xd2, 8, 0x80};
    load_module(pmd, part, sizeof(part));
    start_playback(pmd);

    run_timer_b(pmd); // note
    run_timer_b(pmd); // fade command, then end
    CHECK_EQ(pmd->fadeout_flag, 1);
    CHECK_EQ(pmd->fadeout_speed, 8);

    for (int i = 0; i < 8; i++) fire_fm_timer(pmd, 1, 1); // Timer A
    CHECK_EQ(pmd->fadeout_volume, 8);
    CHECK_EQ(pmd->TimerAtime, 8);

    free_pmd(pmd);
}

// :7791 CTRL+ESC, :7874 GRPH, and the NULL keyboard hook.
static void test_playback_keys(void) {
    struct pmd *pmd = setup_playback();
    static const uint8_t part[] = {0x40, 1, 0x80};
    load_module(pmd, part, sizeof(part));
    start_playback(pmd);

    pmd->key_check = 1;
    pmd->music_flag = 0;
    ctx.keys = PMD_KEY_CTRL | PMD_KEY_ESC;
    fire_fm_timer(pmd, 1, 1);
    CHECK_EQ(pmd->music_flag & 2, 2);
    CHECK_EQ(pmd->fadeout_flag, 0);

    // ESC without the configured modifier does not stop.
    pmd->music_flag = 0;
    ctx.keys = PMD_KEY_ESC;
    fire_fm_timer(pmd, 1, 1);
    CHECK_EQ(pmd->music_flag & 2, 0);

    // GRPH+CTRL selects the fast-forward tempo in settempo_b.
    pmd->TimerB_speed = 0;
    pmd->ff_tempo = 250;
    ctx.keys = PMD_KEY_GRPH | PMD_KEY_CTRL;
    run_timer_b(pmd);
    CHECK_EQ(pmd->TimerB_speed, 250);
    CHECK(has_write(pmd->fm1_port1, 0x26));
    CHECK(has_write(pmd->fm1_port2, 250));

    // GRPH alone is not the configured combination.
    pmd->TimerB_speed = 0;
    pmd->tempo_d = 100;
    ctx.keys = PMD_KEY_GRPH;
    run_timer_b(pmd);
    CHECK_EQ(pmd->TimerB_speed, 100);

    // A NULL read_key_state hook means no keys are pressed.
    struct pmd_hw no_keys = test_hw;
    no_keys.read_key_state = NULL;
    pmd->hw = &no_keys;
    pmd->music_flag = 0;
    ctx.keys = PMD_KEY_CTRL | PMD_KEY_ESC;
    fire_fm_timer(pmd, 1, 1);
    CHECK_EQ(pmd->music_flag & 2, 0);

    free_pmd(pmd);
}

// Writes one 32-byte voice into the direct voice area (tondat).
static void set_voice(struct pmd *pmd, uint8_t num, const uint8_t *voice) {
    memcpy(pmd->tondat + (size_t)num * 32, voice, 32);
}

// :5144 ':'/'@' voice change: ALG/FB, the parameter walk, and the carrier.
static void test_playback_voice(void) {
    struct pmd *pmd = setup_playback();
    // DT/ML, TL, the 16 remaining parameters, then ALG 1 / FB 0.
    static const uint8_t voice[32] = {
        0x11, 0x22, 0x33, 0x44, // DT/ML
        0x15, 0x26, 0x37, 0x48, // TL
        0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
        0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f, 0x60,
        0x01, // ALG 1 / FB 0
    };
    set_voice(pmd, 0, voice);
    static const uint8_t part[] = {0xff, 0, 0x40, 1, 0x80};
    load_module(pmd, part, sizeof(part));
    start_playback(pmd);

    run_timer_b(pmd);

    struct pmd_qq *di = &pmd->parts[0];
    CHECK_EQ(di->voicenum, 0);
    // The voice's ALG/FB is applied; af_check is clear during a '@'.
    CHECK_EQ(di->alg_fb, 0x01);
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0xb0, 0x01));
    // The voice's carrier mask becomes the part's.
    CHECK_EQ(di->carrier, 0x80);
    CHECK_EQ(di->volmask, 0x80);
    CHECK_EQ(di->_volmask, 0x80);
    // :5220 the parameter walk: DT/ML at 30h, TL at 40h, the rest at 50h+.
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x30, 0x11));
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x3c, 0x44));
    // The TL loop only writes the modulators (carrier_table's inverted mask).
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x40, 0x15));
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x44, 0x26));
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x48, 0x37));
    CHECK(!has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x4c, 0x48));
    // The rotated neiromask covers all sixteen remaining parameters.
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x50, 0x51));
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x7c, 0x5c));
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x8c, 0x60));
    // :5258 the slot TLs are kept in the work area.
    CHECK_EQ(di->slot1, 0x15);
    CHECK_EQ(di->slot3, 0x26);
    CHECK_EQ(di->slot2, 0x37);
    CHECK_EQ(di->slot4, 0x48);

    free_pmd(pmd);
}

// :2213 'b6' with bit 7 set is the relative FB change; the FB lands in the
// high nibble of B0h and the ALG bits are preserved.
static void test_playback_fb_relative(void) {
    struct pmd *pmd = setup_playback();
    static const uint8_t voice[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x26};
    set_voice(pmd, 0, voice);
    // '@0', then 'b6' with FB += 1.
    static const uint8_t part[] = {0xff, 0, 0xb6, 0x81, 0x40, 1, 0x80};
    load_module(pmd, part, sizeof(part));
    start_playback(pmd);

    run_timer_b(pmd);

    struct pmd_qq *di = &pmd->parts[0];
    CHECK_EQ(di->alg_fb, 0x2e); // ALG 6, FB 5
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0xb0, 0x2e));

    free_pmd(pmd);
}

// Part 11 selects pattern 0 from the rhythm address table; the pattern is a
// hit, its length, then the end marker.
static void load_rhythm_module(struct pmd *pmd, const uint8_t *pattern,
                               size_t len) {
    load_empty_module(pmd);
    static const uint8_t part11[] = {0x00, 0x80};
    load_part(pmd, test_part_rhythm, 0x50, part11, sizeof(part11));
    // radtbl[0] -> the pattern at 40h.
    pmd->mmlbuf[22] = 0x30;
    pmd->mmlbuf[23] = 0;
    pmd->mmlbuf[0x30] = 0x40;
    pmd->mmlbuf[0x31] = 0;
    memcpy(pmd->mmlbuf + 0x40, pattern, len);
}

// :1539 the rhythm part: the hit's 14-bit mask, its single data byte, and the
// PPSDRV re-scan.
static void test_playback_rhythm(void) {
    // Bass drum and snare drum (bits 0 and 1), length 1, then the end marker.
    static const uint8_t pattern[] = {0x80, 0x03, 0x01, 0xff};

    struct pmd *pmd = setup_playback();
    load_rhythm_module(pmd, pattern, sizeof(pattern));
    start_playback(pmd);
    CHECK_EQ(pmd->ppsdrv_flag, 0); // the test host supplies no PPSDRV vtable

    run_timer_b(pmd);
    struct pmd_qq *di = pmd->part_data_table[test_part_rhythm];
    CHECK_EQ(pmd->kshot_dat, 3);
    // Only the hit's data byte is consumed: the length byte follows it.
    CHECK(pmd->rhyadr == pmd->mmlbuf + 0x43);
    CHECK(di->address == pmd->mmlbuf + 0x51);
    CHECK_EQ(di->leng, 1);
    CHECK_EQ(di->keyon_flag, 1);
#if board2
    // kp_rhythm_flag is set without PPSDRV, so the rhythm registers are
    // written directly: level then the key-on bit.
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x18, 0xdf));
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x10, 0x01));
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x19, 0xdf));
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x10, 0x02));
#endif

    // The next tick reaches the end marker and restores the dummy address.
    run_timer_b(pmd);
    CHECK_EQ(di->loopcheck, 3);
    CHECK(pmd->rhyadr == &pmd->rhydmy);
    CHECK(di->address == pmd->mmlbuf + 0x51);

    free_pmd(pmd);

    // :4064 with PPSDRV the scan continues past the first instrument and must
    // stop after the last one. A regression here spins instead of failing, so
    // this half of the test relies on the harness timeout.
    pmd = setup_playback();
    load_rhythm_module(pmd, pattern, sizeof(pattern));
    start_playback(pmd);
    pmd->ppsdrv_flag = 1;
    run_timer_b(pmd);
    CHECK_EQ(pmd->kshot_dat, 3);
    CHECK_EQ(pmd->part_data_table[test_part_rhythm]->leng, 1);
    free_pmd(pmd);
}

#if board2
// :4064 '\v': the rhythm instrument number is 1-based, so rdat is addressed
// as `rdat-1 + n` and the register is 17h + n.
static void test_playback_rhythm_volume(void) {
    struct pmd *pmd = setup_playback();
    static const uint8_t part[] = {0xea, 0x30, 0x40, 1, 0x80}; // '\vb16'
    load_module(pmd, part, sizeof(part));
    start_playback(pmd);

    run_timer_b(pmd);
    // opn_init leaves every slot at pan L+R; only the bass drum changes.
    CHECK_EQ(pmd->rdat[0], 0xd0);
    CHECK_EQ(pmd->rdat[1], 0xcf);
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x18, 0xd0));
    free_pmd(pmd);
}
#endif

// :5612 seinit: a zero attack rate starts the envelope in decay at the peak
// volume; a non-zero one keeps the attack phase.
static void test_playback_psg_envelope(void) {
    // 'y' pat/pv2/pr1/pr2, then a note and the end marker.
    static const uint8_t decay[] = {0xf0, 0x00, 0x09, 0x02, 0x02, 0x40, 1, 0x80};
    struct pmd *pmd = setup_playback();
    load_ssg_module(pmd, decay, sizeof(decay));
    start_playback(pmd);

    run_timer_b(pmd);

    struct pmd_qq *di = pmd->part_data_table[test_part_ssg1];
    CHECK_EQ(di->pat, 0);
    CHECK_EQ(di->patb, 0);
    CHECK_EQ(di->envf, 1); // ATTACK=0: straight to decay
    CHECK_EQ(di->penv, 9); // pv2
    CHECK_EQ(di->pr1, 2);
    CHECK_EQ(di->pr2, 2);
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x08, 15));
    // keyonp enables the part's own PSG channel, part G being partb 1, whose
    // tone bit in PSG register 07h is bit 0.
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x07, 0x08));
    free_pmd(pmd);

    static const uint8_t attack[] = {0xf0, 0x05, 0x09, 0x02, 0x02, 0x40, 1, 0x80};
    pmd = setup_playback();
    load_ssg_module(pmd, attack, sizeof(attack));
    start_playback(pmd);

    run_timer_b(pmd);

    di = pmd->part_data_table[test_part_ssg1];
    CHECK_EQ(di->pat, 5);
    CHECK_EQ(di->envf, 0); // attack phase
    CHECK_EQ(di->penv, 0);
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x08, 8));
    free_pmd(pmd);
}

// :7864 the rewind key is GRPH+SHIFT, distinct from GRPH+CTRL (fast forward).
// A rewind restarts the module and skips to the previous bar; this module ends
// during the skip, which is ff_music_main's past-the-end path.
static void test_playback_rewind_key(void) {
    struct pmd *pmd = setup_playback();
    static const uint8_t part[] = {0x40, 1, 0x80};
    load_module(pmd, part, sizeof(part));
    start_playback(pmd);

    pmd->key_check = 1;
    pmd->syousetu = 4;
    pmd->opncount = 1;
    pmd->ff_tempo = 250;
    pmd->tempo_d = 100;
    pmd->TimerB_speed = 0;
    ctx.keys = PMD_KEY_GRPH | PMD_KEY_SHIFT;

    // Timer A runs the rewind step. The restart clears the bar counter and the
    // timer count; the module ends before the requested bar, so AL reports
    // PMD_FF_MUSIC_PAST.
    pmd->TimerAtime = 7;
    fire_fm_timer_busy(pmd, 1, 1);
    CHECK_EQ(pmd->TimerAtime, 0);
    CHECK_EQ(pmd->syousetu, 0);
    CHECK_EQ(pmd->status2, 0xff);
    CHECK_EQ(pmd->skip_flag, 0);
    // The rewind itself requests no stop.
    CHECK_EQ(pmd->music_flag & 2, 0);
    CHECK_EQ(pmd->play_flag, 1);
    // BUG: ffm_exit1 discards the saved fadeout volume rather than restoring
    // it (:6885), so a skip that runs past the end leaves the volume silent.
    CHECK_EQ(pmd->fadeout_volume, 0xff);
    CHECK_EQ(pmd->rhythmmask, 0);

    // GRPH+SHIFT is not the fast-forward combination.
    run_timer_b(pmd);
    CHECK_EQ(pmd->TimerB_speed, 100);

    free_pmd(pmd);
}

// :1215 calc_q: the 'q'/'Q' scaling, the 'q' minimum-gate floor, and 'QR'.
static void test_playback_gate(void) {
    // q 8, Q 40h: 8 + ((leng 4 * 40h) >> 8) = 9.
    static const uint8_t scaled[] = {0xfe, 8, 0xc4, 0x40, 0x40, 4, 0x80};
    struct pmd *pmd = setup_playback();
    load_module(pmd, scaled, sizeof(scaled));
    start_playback(pmd);
    run_timer_b(pmd);
    CHECK_EQ(pmd->parts[0].qdata, 8);
    CHECK_EQ(pmd->parts[0].qdatb, 0x40);
    CHECK_EQ(pmd->parts[0].qdat, 9);
    free_pmd(pmd);

    // q 8, qdat2 (0b3h) 2: the gate is clipped to leng - qdat2.
    static const uint8_t floor[] = {0xfe, 8, 0xb3, 2, 0x40, 4, 0x80};
    pmd = setup_playback();
    load_module(pmd, floor, sizeof(floor));
    start_playback(pmd);
    run_timer_b(pmd);
    CHECK_EQ(pmd->parts[0].qdatb, 0);
    CHECK_EQ(pmd->parts[0].qdat, 2);
    free_pmd(pmd);

    // The same floor below the note length borrows and zeroes the gate.
    static const uint8_t over[] = {0xfe, 8, 0xb3, 5, 0x40, 4, 0x80};
    pmd = setup_playback();
    load_module(pmd, over, sizeof(over));
    start_playback(pmd);
    run_timer_b(pmd);
    CHECK_EQ(pmd->parts[0].qdat, 0);
    free_pmd(pmd);

    // QR 7fh adds and QR ffh subtracts rnd(128); seed 1 makes rnd return 1.
    static const uint8_t plus[] = {0xfe, 8, 0xb1, 0x7f, 0x40, 4, 0x80};
    pmd = setup_playback();
    load_module(pmd, plus, sizeof(plus));
    pmd->seed = 1;
    start_playback(pmd);
    run_timer_b(pmd);
    CHECK_EQ(pmd->parts[0].qdat3, 0x7f);
    CHECK_EQ(pmd->parts[0].qdat, 9);
    free_pmd(pmd);

    static const uint8_t minus[] = {0xfe, 8, 0xb1, 0xff, 0x40, 4, 0x80};
    pmd = setup_playback();
    load_module(pmd, minus, sizeof(minus));
    pmd->seed = 1;
    start_playback(pmd);
    run_timer_b(pmd);
    CHECK_EQ(pmd->parts[0].qdat3, 0xff);
    CHECK_EQ(pmd->parts[0].qdat, 7);
    free_pmd(pmd);
}

// :3085 'da' portamento: the note pair, the signed division into porta_num2
// and porta_num3, and one porta_calc step on the following tick.
static void test_playback_porta(void) {
    // C4 then D4, length 10: (28fh - 26ah) / 10 = 3 remainder 7.
    static const uint8_t part[] = {0xda, 0x40, 0x41, 10, 0x80};
    struct pmd *pmd = setup_playback();
    load_module(pmd, part, sizeof(part));
    start_playback(pmd);

    run_timer_b(pmd);

    struct pmd_qq *di = &pmd->parts[0];
    CHECK_EQ(di->fnum, 0x226a); // the source pitch is left in place
    CHECK_EQ(di->onkai, 0x40);
    CHECK_EQ(di->leng, 10);
    CHECK_EQ(di->lfoswi & 8, 8);
    CHECK_EQ(di->porta_num, 0);
    CHECK_EQ(di->porta_num2, 3);
    CHECK_EQ(di->porta_num3, 7);

    // The next tick runs porta_calc: porta_num += 3, then the remainder pulls
    // it up by one more and counts down.
    run_timer_b(pmd);
    CHECK_EQ(di->porta_num, 4);
    CHECK_EQ(di->porta_num3, 6);
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0xa4, 0x22));
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0xa0, 0x6e));

    free_pmd(pmd);
}

// :5341 lfo/lfo_main for the triangle and square waveforms.
static void test_playback_lfo(void) {
    static const uint8_t part[] = {0x40, 20, 0x80};
    struct pmd *pmd = setup_playback();
    load_module(pmd, part, sizeof(part));
    start_playback(pmd);

    // Play the note, then configure a triangle LFO on the part work.
    run_timer_b(pmd);
    struct pmd_qq *di = &pmd->parts[0];
    di->lfoswi = 1;
    di->lfo_wave = 0;
    di->delay = 0;
    di->speed = 1;
    di->speed2 = 1;
    di->step = 2;
    di->time = 0xff;

    // The triangle adds the step every tick and holds time at ffh.
    run_timer_b(pmd);
    CHECK_EQ(di->lfodat, 2);
    CHECK_EQ(di->time, 0xff);
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0xa0, 0x6c));

    run_timer_b(pmd);
    CHECK_EQ(di->lfodat, 4);
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0xa0, 0x6e));
    free_pmd(pmd);

    // The square sets lfodat to step * time outright and flips the step.
    pmd = setup_playback();
    load_module(pmd, part, sizeof(part));
    start_playback(pmd);
    run_timer_b(pmd);
    di = &pmd->parts[0];
    di->lfoswi = 1;
    di->lfo_wave = 2;
    di->delay = 0;
    di->speed = 1;
    di->step = 3;
    di->time = 2;

    run_timer_b(pmd);
    CHECK_EQ(di->lfodat, 6);
    CHECK_EQ(di->step, 0xfd);
    free_pmd(pmd);
}

// :7874 a multi-bit key mask needs every bit held, in both the BIOS and the VA
// port representations.
static void test_playback_key_mask(void) {
    struct pmd *pmd = setup_playback();
    static const uint8_t part[] = {0x40, 1, 0x80};
    load_module(pmd, part, sizeof(part));
    start_playback(pmd);

    pmd->key_check = 1;
#if va
    // SHIFT+CTRL in port-8 bit positions (what '/g3' rotates to).
    pmd->grph_sp_key = 0xc0;
    pmd->esc_sp_key = 0xc0;
#else
    pmd->grph_sp_key = PMD_KEY_CTRL | PMD_KEY_SHIFT;
    pmd->esc_sp_key = PMD_KEY_CTRL | PMD_KEY_SHIFT;
#endif

    // One of the two mask bits is not enough for the fast-forward tempo, even
    // with GRPH held.
    pmd->TimerB_speed = 0;
    pmd->tempo_d = 100;
    pmd->ff_tempo = 250;
    ctx.keys = PMD_KEY_GRPH | PMD_KEY_CTRL;
    run_timer_b(pmd);
    CHECK_EQ(pmd->TimerB_speed, 100);

    // Both mask bits plus GRPH fast-forward.
    pmd->TimerB_speed = 0;
    ctx.keys = PMD_KEY_GRPH | PMD_KEY_CTRL | PMD_KEY_SHIFT;
    run_timer_b(pmd);
    CHECK_EQ(pmd->TimerB_speed, 250);

    // The same applies to the CTRL+ESC stop, which needs every mask bit but no
    // GRPH.
    pmd->music_flag = 0;
    ctx.keys = PMD_KEY_CTRL | PMD_KEY_ESC;
    fire_fm_timer(pmd, 1, 1);
    CHECK_EQ(pmd->music_flag & 2, 0);

    pmd->music_flag = 0;
    ctx.keys = PMD_KEY_CTRL | PMD_KEY_SHIFT | PMD_KEY_ESC;
    fire_fm_timer(pmd, 1, 1);
    CHECK_EQ(pmd->music_flag & 2, 2);

    free_pmd(pmd);
}

// The board type drv_chk reports for this variant (:6452).
static uint8_t expected_board_type(void) {
#if board2 && ppz
#if ademu
    return PMD_DRV_BOARD_PPZE;
#else
    return PMD_DRV_BOARD_PPZ;
#endif
#elif board2 && pcm
    return PMD_DRV_BOARD_86;
#elif board2
    return PMD_DRV_BOARD_B2;
#else
    return PMD_DRV_BOARD_PMD;
#endif
}

// The no-board half of n_int60_jumptable (:7294): the entries that `nothing`
// covers change nothing, the others report their sentinels, and no call
// reaches the hardware.
static void test_int60_no_board(void) {
    struct pmd *pmd = new_pmd();
    pmd->board = 0;
    ctx.reads = 0;
    ctx.write_count = 0;

    // Command entries: no effect at all.
    pmd->play_flag = 1;
    CHECK(pmd_mstart_f(pmd));
    CHECK_EQ(pmd->play_flag, 1);
    CHECK(pmd_mstop_f(pmd));
    CHECK_EQ(pmd->play_flag, 1);

    pmd->fadeout_speed = 7;
    pmd_fout(pmd, 3);
    CHECK_EQ(pmd->fadeout_speed, 7);

    // :7354 The FM effect entries are translated, so the board guard is
    // observable here: the effect is not started.
    pmd->fm_effec_flag = 0;
    pmd->fm_effec_num = 9;
    pmd_fm_effect_on(pmd, 2);
    CHECK_EQ(pmd->fm_effec_num, 9);
    CHECK_EQ(pmd->fm_effec_flag, 0);

    // :25/:184/:821 The SSG effect and PCM effect entries do nothing without a
    // board, so they reach no hardware.
    ctx.reads = 0;
    ctx.write_count = 0;
    pmd_eff_on(pmd, 1);
    pmd_effoff(pmd);
    pmd_fm_effect_off(pmd);
    pmd_pcm_effect(pmd, 1, 2, 3, 4);
    CHECK_EQ(ctx.reads, 0);
    CHECK_EQ(ctx.write_count, 0);

    pmd->fadeout_volume = 9;
    pmd_set_fv(pmd, 0);
    CHECK_EQ(pmd->fadeout_volume, 9);

    pmd->ppsdrv_flag = 3;
    pmd_set_ppsdrv_flag(pmd, 1);
    CHECK_EQ(pmd->ppsdrv_flag, 3);

    pmd->pause_flag = 3;
    pmd_pause_on(pmd);
    CHECK_EQ(pmd->pause_flag, 3);
    pmd->play_flag = 0;
    pmd->pause_flag = 1;
    pmd_pause_off(pmd);
    CHECK_EQ(pmd->play_flag, 0);
    CHECK_EQ(pmd->pause_flag, 1);

    struct pmd_int_handler handler = {test_fm_callback, pmd};
    pmd_set_fm_int(pmd, handler);
    CHECK(pmd->fmint.fn == NULL);
    pmd_set_efc_int(pmd, handler);
    CHECK(pmd->efcint.fn == NULL);
    CHECK_EQ(pmd->intfook_flag, 0);
    CHECK_EQ(pmd->rescut_cant, 0);

    pmd->parts[0].partmask = 0;
    pmd_part_mask(pmd, 0);
    CHECK_EQ(pmd->parts[0].partmask, 0);

    CHECK_EQ(pmd_ff_music(pmd, 3), PMD_FF_MUSIC_NO_BOARD);
    CHECK_EQ(pmd->skip_flag, 0);

    // Query entries: the sentinels from get_255/get_65535.
    CHECK_EQ(pmd_get_ss(pmd), PMD_NO_BOARD);
    CHECK_EQ(pmd_get_fv(pmd), PMD_NO_BOARD);
    CHECK_EQ(pmd_get_fmefc_num(pmd), PMD_NO_BOARD);
    CHECK_EQ(pmd_get_pcmefc_num(pmd), PMD_NO_BOARD);
    CHECK_EQ(pmd_get_ppsdrv_flag(pmd), PMD_NO_BOARD);

    struct pmd_status status = pmd_get_status(pmd);
    CHECK_EQ(status.status, PMD_NO_BOARD);
    CHECK_EQ(status.status2, PMD_NO_BOARD);

    struct pmd_psgefcnum psgefcnum = pmd_get_psgefcnum(pmd);
    CHECK_EQ(psgefcnum.psgefcnum, PMD_NO_BOARD);
    CHECK_EQ(psgefcnum.effon, PMD_NO_BOARD);

    // :6592 The joystick is a hardware read, so it reports the sentinel
    // without touching the PSG.
    struct pmd_joy joy = pmd_get_joy(pmd);
    CHECK_EQ(joy.joystick1, PMD_NO_BOARD);
    CHECK_EQ(joy.joystick2, PMD_NO_BOARD);
    CHECK_EQ(ctx.reads, 0);
    CHECK_EQ(ctx.write_count, 0);

    // :7339 Without a board drv_chk2 reports AL=255 alongside the version.
    struct pmd_drv_info info = pmd_drv_chk(pmd);
    CHECK_EQ(info.board, PMD_DRV_BOARD_ABSENT);
    CHECK_EQ(info.version, vers);
    CHECK_EQ(info.version_char, (uint8_t)verc[0]);

    // The address entries work either way.
    CHECK(pmd_get_musdat_adr(pmd) == pmd->dataarea);
    CHECK_EQ(pmd_get_musdat_adr(pmd)[0], pmd->mmlbuf[-1]);
    CHECK(pmd_get_tondat_adr(pmd) == pmd->tondat);
    CHECK(pmd_get_efcdat_adr(pmd) == pmd->efcdat);
    CHECK(pmd_get_workadr(pmd) == pmd->part_data_table);
#if board2
    CHECK(pmd_get_pcm_adr(pmd) == &pmd->pcm_table);
#else
    CHECK(pmd_get_pcm_adr(pmd) == NULL);
#endif
    CHECK(pmd_get_mus_name(pmd) == pmd->mus_filename);
    struct pmd_size size = pmd_get_size(pmd);
    CHECK_EQ(size.music, 16);
    CHECK_EQ(size.voice, 8);
    CHECK_EQ(size.effect, 4);

    struct pmd_int_handler fmint = pmd_get_fm_int(pmd);
    CHECK(fmint.fn == NULL);
    CHECK(fmint.user_data == NULL);
    struct pmd_int_handler efcint = pmd_get_efc_int(pmd);
    CHECK(efcint.fn == NULL);
    CHECK(efcint.user_data == NULL);

    CHECK_EQ(pmd->int60flag, 0);

    free_pmd(pmd);
}

// The board-present halves of the same entries, plus the setters.
static void test_int60_board(void) {
    static const struct pmd_drv_pps pps = {0};

    struct pmd *pmd = setup_playback();

    pmd->syousetu = 0x1234;
    CHECK_EQ(pmd_get_ss(pmd), 0x1234);

    pmd->status = 0x12;
    pmd->status2 = 0x34;
    struct pmd_status status = pmd_get_status(pmd);
    CHECK_EQ(status.status, 0x12);
    CHECK_EQ(status.status2, 0x34);

    pmd->fadeout_volume = 0x2a;
    CHECK_EQ(pmd_get_fv(pmd), 0x2a);

    struct pmd_drv_info info = pmd_drv_chk(pmd);
    CHECK_EQ(info.board, expected_board_type());
    CHECK_EQ(info.version, vers);
    CHECK_EQ(info.version_char, (uint8_t)verc[0]);

    pmd->fm_effec_num = 6;
    CHECK_EQ(pmd_get_fmefc_num(pmd), 6);
    pmd->pcm_effec_num = 8;
    CHECK_EQ(pmd_get_pcmefc_num(pmd), 8);

    pmd->psgefcnum = 0x80;
    pmd->effon = 1;
    struct pmd_psgefcnum psgefcnum = pmd_get_psgefcnum(pmd);
    CHECK_EQ(psgefcnum.psgefcnum, 0x80);
    CHECK_EQ(psgefcnum.effon, 1);

    // :6580 the sizes come back in AL/AH/DL.
    struct pmd_size size = pmd_get_size(pmd);
    CHECK_EQ(size.music, pmd->mmldat_lng);
    CHECK_EQ(size.voice, pmd->voicedat_lng);
    CHECK_EQ(size.effect, pmd->effecdat_lng);

    strcpy(pmd->mus_filename, "TEST.M");
    CHECK(strcmp(pmd_get_mus_name(pmd), "TEST.M") == 0);

    CHECK(pmd_get_musdat_adr(pmd) == pmd->dataarea);
    CHECK(pmd_get_tondat_adr(pmd) == pmd->tondat);
    CHECK(pmd_get_efcdat_adr(pmd) == pmd->efcdat);
    CHECK(pmd_get_workadr(pmd) == pmd->part_data_table);
#if board2
    CHECK(pmd_get_pcm_adr(pmd) == &pmd->pcm_table);
#else
    CHECK(pmd_get_pcm_adr(pmd) == NULL);
#endif

    // :6558 set_ppsdrv_flag clears the flag first and only stores the value
    // while PPSDRV is resident.
    pmd->pps = NULL;
    pmd->ppsdrv_flag = 5;
    pmd_set_ppsdrv_flag(pmd, 1);
    CHECK_EQ(pmd->ppsdrv_flag, 0);
    CHECK_EQ(pmd_get_ppsdrv_flag(pmd), 0);
    pmd->pps = &pps;
    pmd_set_ppsdrv_flag(pmd, 1);
    CHECK_EQ(pmd->ppsdrv_flag, 1);
    CHECK_EQ(pmd_get_ppsdrv_flag(pmd), 1);
    pmd->pps = NULL;

    // :6441 set_fv to zero restores the rhythm volume in the board2 builds.
    ctx.write_count = 0;
    pmd_set_fv(pmd, 0);
    CHECK_EQ(pmd->fadeout_volume, 0);
#if board2
    // opn_init sets rhyvol from the rhythm volume, 48 at full volume (:811).
    CHECK_EQ(pmd->rhyvol, 0x30);
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x11, 0x30));
#else
    CHECK_EQ(ctx.write_count, 0);
#endif

    // The SSG effect entry points do nothing without a board; calling them
    // must be harmless.
    pmd_eff_on(pmd, 1);
    pmd_effoff(pmd);

    CHECK_EQ(pmd->int60flag, 0);
    free_pmd(pmd);
}

// :7354 The FM effect part: the voice-table address, the fixed defaults, and
// the FM3 mode save and restore.
static void test_int60_fm_effect(void) {
    struct pmd *pmd = setup_playback();
    pmd->play_flag = 1;
    // The effect area holds the part's little-endian data offset.
    pmd->efcdat[0] = 0x40;
    pmd->efcdat[1] = 0x00;

    ctx.write_count = 0;
    pmd_fm_effect_on(pmd, 0);
    CHECK_EQ(pmd->fm_effec_num, 0);
    CHECK_EQ(pmd->fm_effec_flag, 1);
    CHECK_EQ(pmd->partb, 3);
    CHECK(pmd->part_e.address == pmd->efcdat + 0x40);
    CHECK_EQ(pmd->part_e.leng, 1);
    CHECK_EQ(pmd->part_e.volume, 108);
    CHECK_EQ(pmd->part_e.slotmask, 0xf0);
    CHECK_EQ(pmd->part_e.neiromask, 0xff);
    // The effect masks its FM channel so the music leaves it alone: part6 in
    // the board2 builds, the four FM3 parts otherwise.
#if board2
    CHECK_EQ(pmd->parts[5].partmask & 2, 2);
#else
    for (int i = 2; i < 6; i++) CHECK_EQ(pmd->parts[i].partmask & 2, 2);
#endif
#if board2
    CHECK_EQ(pmd->part_e.fmpan, 0xc0);
    CHECK(has_reg_write(pmd->fm2_port1, pmd->fm2_port2, 0xb6, 0xc0));
#else
    // :7410 The FM3 mode is forced while the effect plays.
    CHECK_EQ(pmd->ch3mode_push, 0x3f);
    CHECK_EQ(pmd->ch3mode, 0x3f);
#endif

    // Silencing it clears the effect and restores the FM3 mode.
    ctx.write_count = 0;
    pmd_fm_effect_off(pmd);
    CHECK_EQ(pmd->fm_effec_num, 0xff);
    CHECK_EQ(pmd->fm_effec_flag, 0);
#if !board2
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x27, 0x0f));
#endif

    // With no effect playing, silencing it does nothing.
    ctx.write_count = 0;
    pmd_fm_effect_off(pmd);
    CHECK_EQ(ctx.write_count, 0);

    free_pmd(pmd);
}

// :6663 Pause silences the parts and resumes them.
static void test_int60_pause(void) {
    struct pmd *pmd = setup_playback();
    static const uint8_t part[] = {0x40, 1, 0x80};
    load_module(pmd, part, sizeof(part));
    start_playback(pmd);

    ctx.write_count = 0;
    pmd_pause_on(pmd);
    CHECK_EQ(pmd->play_flag, 0);
    CHECK_EQ(pmd->pause_flag, 1);
    // silence() keys every FM channel off.
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x28, 0x00));

    // A paused driver ignores another pause.
    ctx.write_count = 0;
    pmd_pause_on(pmd);
    CHECK_EQ(ctx.write_count, 0);

    // :6675 Resuming restores the voices of the parts that have music.
    pmd->parts[0].address = pmd->mmlbuf + 24;
    pmd->parts[0].neiromask = 0xff;
    pmd->parts[0].voicenum = 1;
    ctx.write_count = 0;
    pmd_pause_off(pmd);
    CHECK_EQ(pmd->play_flag, 1);
    CHECK_EQ(pmd->pause_flag, 0);
    // neiro_reset restores the pan register of FM1 channel 1.
    CHECK(has_write(pmd->fm1_port1, 0xb0));

    // A running driver ignores the resume.
    ctx.write_count = 0;
    pmd_pause_off(pmd);
    CHECK_EQ(pmd->play_flag, 1);
    CHECK_EQ(ctx.write_count, 0);

    free_pmd(pmd);
}

// :6967 part_mask: the mask bit, the rhythm part, and the part numbers the
// table does not cover.
static void test_int60_part_mask(void) {
    struct pmd *pmd = setup_playback();
    pmd->play_flag = 1;

    // FM1 part 0: mask and unmask. The voice mask makes the mask silent the
    // part instead of only keying it off.
    pmd->parts[0].partmask = 0;
    pmd->parts[0].neiromask = 0xff;
    ctx.write_count = 0;
    pmd_part_mask(pmd, 0);
    CHECK_EQ(pmd->parts[0].partmask, 1);
    CHECK_EQ(pmd->partb, 1);
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x28, 0x00));
    pmd_part_mask(pmd, 0x80);
    CHECK_EQ(pmd->parts[0].partmask, 0);

    // :6997 A part that is already masked is not masked again.
    pmd->parts[0].partmask = 1;
    ctx.write_count = 0;
    pmd_part_mask(pmd, 0);
    CHECK_EQ(pmd->parts[0].partmask, 1);
    CHECK_EQ(ctx.write_count, 0);

    // :7111 Unmasking an unmasked part does nothing.
    pmd->parts[0].partmask = 0;
    ctx.write_count = 0;
    pmd_part_mask(pmd, 0x80);
    CHECK_EQ(pmd->parts[0].partmask, 0);
    CHECK_EQ(ctx.write_count, 0);

    // The SSG part masks its channel in PSG register 07h.
    ctx.write_count = 0;
    pmd_part_mask(pmd, 6);
    CHECK_EQ(pmd->parts[6].partmask, 1);
    // The PSG keyoff mask combines the part's own channel bit (bit partb-1)
    // with the matching noise bit three bits above it.
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x07, 0x09));
    pmd_part_mask(pmd, 0x86);
    CHECK_EQ(pmd->parts[6].partmask, 0);

    // :6973 Part numbers past the table are ignored.
    ctx.write_count = 0;
#if ppz
    pmd_part_mask(pmd, 16 + 8);
#else
    pmd_part_mask(pmd, 16);
#endif
    CHECK_EQ(ctx.write_count, 0);

    // The rhythm part masks the rhythm source instead of a part.
    pmd->rhythmmask = 0xff;
    pmd_part_mask(pmd, 14);
    CHECK_EQ(pmd->rhythmmask, 0);
    pmd_part_mask(pmd, 0x8e);
    CHECK_EQ(pmd->rhythmmask, 0xff);

    // The effect part is part_e, which the table's last entry names in every
    // variant. An empty part_e keeps the unmask from restoring a voice.
    pmd->part_e = (struct pmd_qq){0};
    pmd_part_mask(pmd, 15);
    CHECK_EQ(pmd->part_e.partmask, 1);
    pmd_part_mask(pmd, 0x8f);
    CHECK_EQ(pmd->part_e.partmask, 0);

    free_pmd(pmd);
}

// :6741 get_memo walks the module's memo table.
static void test_int60_get_memo(void) {
    struct pmd *pmd = new_pmd();
    pmd->board = 0;

    // The empty module image has no voice header, so no memo table.
    CHECK(pmd_get_memo(pmd, 0) == NULL);

    uint8_t *m = pmd->mmlbuf;
    memset(m, 0, 0x100);
    m[0] = 0x1a; // the voice header marker
    m[1] = 0x00;
    m[0x18] = 0x40; // the 4-byte record the code reads backwards from
    m[0x19] = 0x00;
    m[0x3c] = 0x50; // memo table offset
    m[0x3d] = 0x00;
    m[0x3e] = 0x48; // MC version 4.8
    m[0x3f] = 0xfe;
    m[0x50] = 0x60; // #PPSFile
    m[0x52] = 0x70; // #PPZFile
    m[0x54] = 0x80; // memo 0
    m[0x56] = 0x00; // end of table
    strcpy((char *)m + 0x80, "memo zero");

    // From version 4.2 and 4.8 the two leading entries are the file names, so
    // memo 0 is the third entry.
    CHECK(pmd_get_memo(pmd, 0) == m + 0x80);
    CHECK(pmd_get_memo(pmd, 1) == NULL); // the table ends first

    // Version 4.0 has neither bump, so memo 0 is the first entry.
    m[0x3e] = 0x40;
    CHECK(pmd_get_memo(pmd, 0) == m + 0x60);
    CHECK(pmd_get_memo(pmd, 1) == m + 0x70);

    // :6753 A high version byte other than 0FEh is not a memo table.
    m[0x3e] = 0x41;
    m[0x3f] = 0x00;
    CHECK(pmd_get_memo(pmd, 0) == NULL);

    free_pmd(pmd);
}

// :6592 Both joystick axes come through PSG registers 0Eh/0Fh.
static void test_int60_get_joy(void) {
    struct pmd *pmd = setup_playback();
    uint16_t port1 = pmd->fm1_port1;
    uint16_t port2 = pmd->fm1_port2;

    const struct test_io script[] = {
        {port1, 0x00}, {port1, 0x00}, {port2, 0x10}, // get07: mode set
        {port1, 0x00}, {port1, 0x00}, // opnset44
        {port1, 0x00}, {port1, 0x00}, {port2, 0x01}, // init (1)
        {port1, 0x00}, {port1, 0x00}, {port2, 0x15}, // data (1)
        {port1, 0x00}, {port1, 0x00}, {port2, 0x02}, // init (2)
        {port1, 0x00}, {port1, 0x00}, {port2, 0x2a}, // data (2)
    };
    set_script(script, sizeof(script) / sizeof(script[0]));
    ctx.write_count = 0;

    struct pmd_joy joy = pmd_get_joy(pmd);
    CHECK_EQ(ctx.script_pos, ctx.script_len);
    // The data reads mask off the buttons' upper bits.
    CHECK_EQ(joy.joystick1, 0x15);
    CHECK_EQ(joy.joystick2, 0x2a);
    // :6614 The init writes keep the low nibble and set the top bits.
    CHECK(has_write(port2, 0x81));
    CHECK(has_write(port2, 0xc2));

    free_pmd(pmd);
}

// :6504 The callbacks and the removal lock they take.
static void test_int60_callbacks(void) {
    struct pmd *pmd = setup_playback();

    struct pmd_int_handler handler = {test_fm_callback, pmd};
    pmd_set_fm_int(pmd, handler);
    CHECK(pmd->fmint.fn == test_fm_callback);
    CHECK(pmd->fmint.user_data == pmd);
    CHECK_EQ(pmd->intfook_flag, 1);
    CHECK_EQ(pmd->rescut_cant, 0x80);

    struct pmd_int_handler got = pmd_get_fm_int(pmd);
    CHECK(got.fn == test_fm_callback);
    CHECK(got.user_data == pmd);

    handler.fn = test_efc_callback;
    pmd_set_efc_int(pmd, handler);
    CHECK(pmd->efcint.fn == test_efc_callback);
    CHECK_EQ(pmd->intfook_flag, 3);
    CHECK_EQ(pmd->rescut_cant, 0xc0);
    got = pmd_get_efc_int(pmd);
    CHECK(got.fn == test_efc_callback);

    // :6513 A null handler clears its flag and its part of the lock.
    pmd_set_fm_int(pmd, (struct pmd_int_handler){NULL, NULL});
    CHECK(pmd->fmint.fn == NULL);
    CHECK_EQ(pmd->intfook_flag, 2);
    CHECK_EQ(pmd->rescut_cant, 0x40);
    pmd_set_efc_int(pmd, (struct pmd_int_handler){NULL, NULL});
    CHECK(pmd->efcint.fn == NULL);
    CHECK_EQ(pmd->intfook_flag, 0);
    CHECK_EQ(pmd->rescut_cant, 0);

    // The timer path calls them at the original points.
    pmd->play_flag = 1;
    pmd->fmint = (struct pmd_int_handler){test_fm_callback, pmd};
    pmd->efcint = (struct pmd_int_handler){test_efc_callback, pmd};
    pmd->intfook_flag = 3;
    ctx.fm_calls = 0;
    ctx.efc_calls = 0;
    fire_fm_timer(pmd, 3, 3);
    CHECK_EQ(ctx.efc_calls, 1);
#if sync
    CHECK_EQ(ctx.fm_calls, 0);
#else
    CHECK_EQ(ctx.fm_calls, 1);
#endif

    free_pmd(pmd);
}

// :6360 The reint_chk classes: Timer A, Timer B, and nested INT 60h calls.
static void test_int60_reentrancy(void) {
    struct pmd *pmd = setup_playback();
    pmd->play_flag = 1;

    // A nested call is rejected: mstart_f reports the deferral without
    // scheduling the start.
    pmd->int60flag = 1;
    pmd->music_flag = 0;
    CHECK(!pmd_mstart_f(pmd));
    CHECK_EQ(pmd->music_flag, 0);
    CHECK_EQ(pmd_ff_music(pmd, 0), PMD_FF_MUSIC_REJECTED);
    pmd->parts[0].partmask = 0;
    pmd_part_mask(pmd, 0);
    CHECK_EQ(pmd->parts[0].partmask, 0);
    CHECK_EQ(pmd->int60flag, 1);
    pmd->int60flag = 0;

    // 00h's class only names nested calls, so Timer B's flag does not reject
    // it: the request is deferred to Timer B instead.
    pmd->TimerBflag = 1;
    pmd->music_flag = 0;
    CHECK(!pmd_mstart_f(pmd));
    CHECK_EQ(pmd->music_flag, 1);
    pmd->music_flag = 0;
    pmd->play_flag = 1;
    pmd_pause_on(pmd); // 1Ah rejects while Timer B runs
    CHECK_EQ(pmd->play_flag, 1);
    CHECK_EQ(pmd->pause_flag, 0);
    pmd->fm_effec_flag = 1;
    pmd->fm_effec_num = 5;
    pmd_fm_effect_off(pmd); // 0Dh checks both timers
    CHECK_EQ(pmd->fm_effec_num, 5);
    pmd->TimerBflag = 0;

    // Timer A only rejects the entries whose class names it.
    pmd->TimerAflag = 1;
    pmd_pause_on(pmd);
    CHECK_EQ(pmd->play_flag, 0);
    CHECK_EQ(pmd->pause_flag, 1);
    pmd->play_flag = 1;
    pmd->pause_flag = 0;
    pmd->fadeout_speed = 1;
    pmd_fout(pmd, 9); // 02h has an empty class
    CHECK_EQ(pmd->fadeout_speed, 9);
    pmd->TimerAflag = 0;

    // Every call balances the counter, rejected or not.
    CHECK_EQ(pmd->int60flag, 0);

    free_pmd(pmd);
}

// :6797 ff_music: the stopped, restart, and past-the-end paths.
static void test_int60_ff_music(void) {
    static const uint8_t part[] = {0x40, 1, 0x80};
    struct pmd *pmd = setup_playback();
    load_module(pmd, part, sizeof(part));

    // :6822 A stopped music reports AL=2 and keeps its state.
    pmd->status2 = 0xff;
    pmd->syousetu = 5;
    pmd->TimerAtime = 7;
    ctx.write_count = 0;
    CHECK_EQ(pmd_ff_music(pmd, 0), PMD_FF_MUSIC_STOPPED);
    CHECK_EQ(pmd->syousetu, 5);
    CHECK_EQ(pmd->TimerAtime, 7);
    CHECK_EQ(ctx.write_count, 0);

    // Bar 0 restarts from the top and returns at the fast-forward tempo.
    pmd->status2 = 0;
    pmd->syousetu = 4;
    pmd->ff_tempo = 200;
    pmd->TimerB_speed = 0;
    ctx.write_count = 0;
    CHECK_EQ(pmd_ff_music(pmd, 0), PMD_FF_MUSIC_OK);
    CHECK_EQ(pmd->syousetu, 0); // data_init2 cleared the counters
    CHECK_EQ(pmd->TimerAtime, 0);
    CHECK_EQ(pmd->skip_flag, 0);
    CHECK_EQ(pmd->TimerB_speed, 199); // :6890 one less than ff_tempo
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x26, 199));
    // The temporary part masks are released again.
    CHECK_EQ(pmd->parts[0].partmask, 0);

    // A skip past the module's end reports AL=1 and leaves it stopped.
    pmd->status2 = 0;
    pmd->syousetu = 0;
    ctx.write_count = 0;
    CHECK_EQ(pmd_ff_music(pmd, 4), PMD_FF_MUSIC_PAST);
    CHECK_EQ(pmd->status2, 0xff);
    CHECK_EQ(pmd->skip_flag, 0);
    CHECK_EQ(pmd->parts[0].partmask, 0);
    // BUG: :6885 pops the saved fadeout volume without restoring it on the
    // past-the-end path, so the skip leaves the music silent.
    CHECK_EQ(pmd->fadeout_volume, 0xff);
    CHECK_EQ(pmd->rhythmmask, 0);

    free_pmd(pmd);
}

// :6856 A skip that reaches its bar inside the module takes the loop's
// break-out path, which restores the fadeout volume and rhythm mask it
// suspended for the skip.
static void test_int60_ff_music_skip(void) {
    struct pmd *pmd = setup_playback();
    // 'L' loops the part, so the module outlives the skip.
    static const uint8_t part[] = {0x40, 1, 0xf6, 0x41, 1, 0x80};
    load_module(pmd, part, sizeof(part));
    start_playback(pmd);

    pmd->status2 = 0;
    pmd->syousetu = 0;
    // One count short of the next bar, so the first loop iteration reaches it.
    pmd->opncount = (uint8_t)(pmd->syousetu_lng - 1);
    // The skip suspends both and must put them back on the way out.
    pmd->fadeout_volume = 0;
    pmd->rhythmmask = 0xff;
    pmd->ff_tempo = 200;
    pmd->TimerB_speed = 0;
    ctx.write_count = 0;

    CHECK_EQ(pmd_ff_music(pmd, 1), PMD_FF_MUSIC_OK);
    CHECK_EQ(pmd->syousetu, 1);
    CHECK_EQ(pmd->opncount, 0);
    CHECK_EQ(pmd->status2, 0);
    CHECK_EQ(pmd->skip_flag, 0);
    CHECK_EQ(pmd->fadeout_volume, 0);
    CHECK_EQ(pmd->rhythmmask, 0xff);
    CHECK_EQ(pmd->TimerB_speed, 199);
    // The temporary part masks are released again.
#if ppz
    const size_t count = 14 + 8;
#elif board2
    const size_t count = 14;
#else
    const size_t count = 11;
#endif
    for (size_t i = 0; i < count; i++) {
        CHECK_EQ(pmd->parts[i].partmask & 0x80, 0);
    }
#if board2
    // :6888 A silent master volume restores the rhythm volume on the way out.
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x11, pmd->rhyvol));
#endif

    free_pmd(pmd);
}

// :9753 The removal sequence and its refusals.
static void test_resident_cut(void) {
    // A registered callback forbids removal.
    struct pmd *pmd = setup_playback();
    pmd_set_fm_int(pmd, (struct pmd_int_handler){test_fm_callback, pmd});
    CHECK_EQ(pmd->rescut_cant, 0x80);
    ctx.out_len = 0;
    ctx.out[0] = '\0';
    int unwound = setjmp(ctx.jmp);
    if (unwound == 0) {
        pmd_resident_cut(pmd);
        fprintf(stderr, "%s:%d: pmd_resident_cut did not exit\n", __FILE__,
                __LINE__);
        failures++;
    }
    CHECK(ctx.exited);
    CHECK_EQ(ctx.exit_status, 1);
    CHECK_OUTPUT_EQUALS(cantcut_mes);
    CHECK_EQ(ctx.fm_timer_removals, 0);
    free_pmd(pmd);

    // The ordinary sequence.
    ctx_reset();
    pmd = setup_playback();
    CHECK(ctx.fm_timer_handler != NULL);
    pmd->play_flag = 1;
    pmd->status2 = 0;
    pmd->fm_effec_flag = 1;
    pmd->fm_effec_num = 4;
    pmd->ppsdrv_flag = 1;
    ctx.out_len = 0;
    ctx.out[0] = '\0';
    ctx.write_count = 0;
    pmd_resident_cut(pmd);
    CHECK_OUTPUT_EQUALS(mes_cut);
    // The timer handler goes first, and nothing can call back afterwards.
    CHECK_EQ(ctx.fm_timer_removals, 1);
    CHECK(ctx.fm_timer_handler == NULL);
    // :9777 the FM effect and the music stop; no PPSDRV is resident, so its
    // flag resets.
    CHECK_EQ(pmd->fm_effec_flag, 0);
    CHECK_EQ(pmd->fm_effec_num, 0xff);
    CHECK_EQ(pmd->play_flag, 0);
    CHECK_EQ(pmd->status2, 0xff);
    CHECK_EQ(pmd->ppsdrv_flag, 0);
    // :9827 The FM timer register is reset.
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x27, 0x30));
    CHECK_EQ(pmd->int60flag, 0);
    free_pmd(pmd);

    // :9777 A sounding SSG effect is stopped through pmd_effoff, which now runs
    // the effect driver's own teardown.
    ctx_reset();
    pmd = setup_playback();
    pmd->play_flag = 1;
    pmd_eff_on(pmd, 0);
    CHECK_EQ(pmd->effon, 1);
    ctx.write_count = 0;
    pmd_resident_cut(pmd);
    CHECK_EQ(pmd->effon, 0);
    CHECK_EQ(pmd->psgefcnum, 0xff);
    CHECK(has_reg_write(pmd->fm1_port1, pmd->fm1_port2, 0x0a, 0x00));
    CHECK_EQ(ctx.fm_timer_removals, 1);
    free_pmd(pmd);

    // A driver that found no board never installed the FM timer handler, and
    // :9818 gates the teardown on the same board flag the install used, so the
    // sequence reaches no vtable entry.
    ctx_reset();
    pmd = new_pmd();
    CHECK(run_resident(pmd, ""));
    CHECK_EQ(pmd->board, 0);
    ctx.out_len = 0;
    ctx.out[0] = '\0';
    ctx.reads = 0;
    ctx.write_count = 0;
    pmd_resident_cut(pmd);
    CHECK_OUTPUT_EQUALS(mes_cut);
    CHECK_EQ(ctx.fm_timer_removals, 0);
    CHECK_EQ(ctx.reads, 0);
    CHECK_EQ(ctx.write_count, 0);
    free_pmd(pmd);
}

// :9753 /R removes the driver through pmd_change_main.
static void test_change_remove(void) {
    ctx_reset();
    struct pmd *pmd = new_pmd();
    CHECK(run_board_resident(pmd));
    CHECK(ctx.fm_timer_handler != NULL);
    CHECK_EQ(ctx.fm_timer_removals, 0);

    ctx.out_len = 0;
    ctx.out[0] = '\0';
    enum pmd_change_result result = run_change(pmd, "/R");
    CHECK(!ctx.exited);
    CHECK_EQ(result, PMD_CHANGE_REMOVED);
    CHECK_EQ(ctx.fm_timer_removals, 1);
    CHECK(ctx.fm_timer_handler == NULL);
    char expected[test_out_size] = "";
    append(expected, sizeof(expected), mes_title);
    append(expected, sizeof(expected), mes_cut);
    CHECK_OUTPUT_EQUALS(expected);
    free_pmd(pmd);
}

// Without a board both entry points report success without touching playback.
static void test_playback_no_board(void) {
    struct pmd *pmd = alloc_pmd();
    pmd_init(pmd);
    pmd->board = 0;
    pmd->play_flag = 1;

    CHECK(pmd_mstart_f(pmd));
    CHECK_EQ(pmd->play_flag, 1); // unchanged: no board
    CHECK(pmd_mstop_f(pmd));
    CHECK_EQ(pmd->play_flag, 1);

    free_pmd(pmd);
}

// Phase 7: the YMFM backend. The pmd_hw vtable delivers the FM timer handler
// through pmd->user_data, which therefore has to be the backend itself, so
// these pmd_sys wrappers ignore user_data and use the global test context.
static void ymfm_sys_putc(char c, void *user_data) {
    (void)user_data;
    test_out_char(&ctx, c);
}

static void ymfm_sys_print(const char *mes, void *user_data) {
    (void)user_data;
    for (const char *p = mes; *p != '\0'; p++) test_out_char(&ctx, *p);
}

PMDC_NORETURN static void ymfm_sys_exit(int status, void *user_data) {
    (void)user_data;
    ctx.exited = true;
    ctx.exit_status = status;
    longjmp(ctx.jmp, 1);
}

static char *ymfm_sys_getenv(const char *name, void *user_data) {
    (void)user_data;
    if (strcmp(name, "PMDOPT") == 0) return (char *)ctx.pmdopt;
    return NULL;
}

static const struct pmd_sys ymfm_sys = {
    .putc = ymfm_sys_putc,
    .print = ymfm_sys_print,
    .exit = ymfm_sys_exit,
    .getenv = ymfm_sys_getenv,
};

// Allocates and initializes a driver instance backed by the described chip.
static struct pmd *ymfm_pmd_gain(enum pmd_ymfm_chip chip,
                                 enum pmd_ymfm_board board, uint16_t base_port,
                                 double ssg_gain) {
    ctx_reset();
    struct pmd *pmd = alloc_pmd();
    struct pmd_ymfm_config config;
    pmd_ymfm_default_config(&config, pmd);
    config.chip = chip;
    config.board = board;
    config.base_port = base_port;
    config.ssg_gain = ssg_gain;
    struct pmd_ymfm *ymfm = pmd_ymfm_create(&config);
    if (ymfm == NULL) {
        fprintf(stderr, "%s:%d: pmd_ymfm_create failed\n", __FILE__, __LINE__);
        exit(1);
    }
    pmd->sys = &ymfm_sys;
    pmd->hw = &pmd_hw_ymfm;
    pmd->user_data = ymfm;
    pmd_init(pmd);
    return pmd;
}

static struct pmd *ymfm_pmd(enum pmd_ymfm_chip chip, enum pmd_ymfm_board board,
                            uint16_t base_port) {
    return ymfm_pmd_gain(chip, board, base_port, PMD_YMFM_SSG_GAIN_DEFAULT);
}

// The chip, board layout, and base port the current variant was written for.
static struct pmd *new_ymfm_pmd(void) {
    struct pmd_ymfm_config config;
    pmd_ymfm_default_config(&config, NULL);
    return ymfm_pmd(config.chip, config.board, config.base_port);
}

static void free_ymfm_pmd(struct pmd *pmd) {
    pmd_ymfm_destroy(pmd->user_data);
    pmd->user_data = NULL;
    free_pmd(pmd);
}

static bool run_ymfm_resident(struct pmd *pmd) {
    if (setjmp(ctx.jmp) == 0) {
        pmd_resident_main(pmd, "");
        return true;
    }
    report_unwind("pmd_resident_main (YMFM)");
    return false;
}

// The backend leaves input to the host, so the CLI-style backend has none.
static void test_ymfm_vtable(void) {
    CHECK(pmd_hw_ymfm.inb != NULL);
    CHECK(pmd_hw_ymfm.outb != NULL);
    CHECK(pmd_hw_ymfm.install_fm_timer_handler != NULL);
    CHECK(pmd_hw_ymfm.remove_fm_timer_handler != NULL);
    CHECK(pmd_hw_ymfm.read_key_state == NULL);
}

// The port scan finds the configured board through the real chip.
static void test_ymfm_probe(void) {
    struct pmd *pmd = new_ymfm_pmd();
    CHECK(run_ymfm_resident(pmd));
    struct pmd_ymfm *ymfm = pmd->user_data;
    CHECK(!ctx.exited);

    CHECK_EQ(pmd->board, 1);
    CHECK_EQ(pmd->int_level, 12);
    CHECK_EQ(pmd->mes_int[4], '5');
    CHECK_EQ(pmd->wait_clock, 1);
    CHECK_EQ(pmd->wait1_clock, 1);
    CHECK_OUTPUT_CONTAINS(mes_wait1);
    CHECK_OUTPUT_CONTAINS(mes_exit);

#if va && board2
    CHECK_EQ(pmd->ongen, 1);
    CHECK_EQ(pmd->fm1_port1, 0x44);
    CHECK_EQ(pmd->fm1_port2, 0x45);
    CHECK_EQ(pmd->fm2_port1, 0x46);
    CHECK_EQ(pmd->fm2_port2, 0x47);
    CHECK_EQ(pmd->mes_int[11], '4');
    CHECK_OUTPUT_CONTAINS(pmd->mes_ongen1);
#elif va
    CHECK_EQ(pmd->ongen, 0);
    CHECK_EQ(pmd->fm1_port1, 0x44);
    CHECK_EQ(pmd->fm1_port2, 0x45);
    CHECK_EQ(pmd->mes_int[11], '4');
    CHECK_OUTPUT_CONTAINS(mes_ongen0);
#elif board2 && (pcm || ppz)
    CHECK_EQ(pmd->ongen, 1);
    CHECK_EQ(pmd->fm1_port1, 0x188);
    CHECK_EQ(pmd->fm1_port2, 0x18a);
    CHECK_EQ(pmd->fm2_port1, 0x18c);
    CHECK_EQ(pmd->fm2_port2, 0x18e);
    CHECK_EQ(pmd->fm_port1, 0x18c);
    CHECK_EQ(pmd->fm_port2, 0x18e);
    CHECK_EQ(pmd->opn_0eh, 0xc0);
    CHECK_EQ(pmd->mes_int[11], '1');
    CHECK_OUTPUT_CONTAINS(pmd->mes_ongen1);
#else
    CHECK_EQ(pmd->ongen, 1);
    CHECK_EQ(pmd->fm1_port1, 0x088);
    CHECK_EQ(pmd->fm1_port2, 0x08a);
    CHECK_EQ(pmd->fm2_port1, 0x08c);
    CHECK_EQ(pmd->fm2_port2, 0x08e);
    CHECK_EQ(pmd->opn_0eh, 0xc0);
    CHECK_EQ(pmd->mes_int[11], '0');
    CHECK_OUTPUT_CONTAINS(pmd->mes_ongen1);
#endif

    // Register 0Eh combines the interrupt level with the joystick inputs, so
    // the joystick read sees idle inputs and not the IRQ bits.
    struct pmd_joy joy = pmd_get_joy(pmd);
    CHECK_EQ(joy.joystick1, 0x3f);
    CHECK_EQ(joy.joystick2, 0x3f);

    // The ADPCM RAM check runs against the chip's real external memory, so the
    // backend reports an ADPCM-capable YM2608 wherever the check is reachable.
#if board2 && adpcm && !ademu
    CHECK_EQ(pmd->pcm_gs_flag, 0);
    CHECK_OUTPUT_CONTAINS(mes_adpcm);
#else
    CHECK_EQ(pmd->pcm_gs_flag, 1);
#endif

    // The chip runs at YMFM's lowest fidelity, which is three times the FM rate
    // for every OPN type and the same rate for either default clock.
    CHECK_EQ(pmd_ymfm_sample_rate(ymfm), 166400);

    // The system VSync port is host state: the driver brackets its interrupt
    // handler with a read-modify-write of it and must see its own bits.
    pmd_ymfm_outb(ymfm, 2, 0x04);
    CHECK_EQ(pmd_ymfm_inb(ymfm, 2), 0x04);
    pmd_ymfm_outb(ymfm, 2, 0x00);
    CHECK_EQ(pmd_ymfm_inb(ymfm, 2), 0x00);

    free_ymfm_pmd(pmd);
}

// The 088h boards also answer as a YM2203 or a YMF288, which the plain PMD
// variant finds through its register echo probe.
#if !va && !board2
static void test_ymfm_ym2203(void) {
    struct pmd *pmd = ymfm_pmd(PMD_YMFM_YM2203, PMD_YMFM_BOARD_PC98, 0x088);
    CHECK(run_ymfm_resident(pmd));
    CHECK_EQ(pmd->board, 1);
    CHECK_EQ(pmd->ongen, 0);
    CHECK_EQ(pmd->fm1_port1, 0x088);
    CHECK_EQ(pmd->fm1_port2, 0x08a);
    CHECK_EQ(pmd->int_level, 12);
    CHECK_OUTPUT_CONTAINS(mes_ongen0);
    free_ymfm_pmd(pmd);
}

static void test_ymfm_ymf288(void) {
    struct pmd *pmd = ymfm_pmd(PMD_YMFM_YMF288, PMD_YMFM_BOARD_PC98, 0x088);
    CHECK(run_ymfm_resident(pmd));
    CHECK_EQ(pmd->board, 1);
    CHECK_EQ(pmd->ongen, 0);
    CHECK_EQ(pmd->fm1_port1, 0x088);
    CHECK_EQ(pmd->fm1_port2, 0x08a);
    CHECK_OUTPUT_CONTAINS(mes_ongen0);
    free_ymfm_pmd(pmd);
}
#endif

// Counts the callbacks the backend delivers, for the install/remove pair.
static unsigned ymfm_handler_calls;

static void ymfm_count_handler(struct pmd *pmd) {
    (void)pmd;
    ymfm_handler_calls++;
}

// The vtable's handler is the one the backend calls while IRQ is asserted, and
// removing it stops the callbacks before the removal returns.
static void test_ymfm_timer_install(void) {
    struct pmd *pmd = new_ymfm_pmd();
    CHECK(run_ymfm_resident(pmd));
    struct pmd_ymfm *ymfm = pmd->user_data;

    // Nothing runs until the clock is advanced.
    uint8_t timer_atime = pmd->TimerAtime;
    pmd_ymfm_generate(ymfm, 20000, NULL);
    CHECK(pmd->TimerAtime > timer_atime);
    CHECK_EQ(pmd->int5_flag, 0);
    CHECK_EQ(pmd->TimerAflag, 0);
    CHECK_EQ(pmd->TimerBflag, 0);

    // A handler of the test's own. It never clears the chip's IRQ, so once a
    // timer expires it is called for every generated sample.
    pmd->hw->install_fm_timer_handler(ymfm_count_handler, pmd->user_data);
    timer_atime = pmd->TimerAtime;
    ymfm_handler_calls = 0;
    pmd_ymfm_generate(ymfm, 20000, NULL);
    CHECK(ymfm_handler_calls > 1);
    CHECK_EQ(pmd->TimerAtime, timer_atime);

    pmd->hw->remove_fm_timer_handler(pmd->user_data);
    unsigned calls = ymfm_handler_calls;
    pmd_ymfm_generate(ymfm, 200000, NULL);
    CHECK_EQ(ymfm_handler_calls, calls);

    free_ymfm_pmd(pmd);
}

// pmd_resident_cut removes the driver's handler, so no callback can occur after
// it returns.
static void test_ymfm_timer_remove(void) {
    struct pmd *pmd = new_ymfm_pmd();
    CHECK(run_ymfm_resident(pmd));
    struct pmd_ymfm *ymfm = pmd->user_data;

    uint8_t timer_atime = pmd->TimerAtime;
    pmd_ymfm_generate(ymfm, 20000, NULL);
    CHECK(pmd->TimerAtime > timer_atime);

    ctx.out_len = 0;
    ctx.out[0] = '\0';
    pmd_resident_cut(pmd);
    CHECK_OUTPUT_EQUALS(mes_cut);

    timer_atime = pmd->TimerAtime;
    uint8_t int5_flag = pmd->int5_flag;
    pmd_ymfm_generate(ymfm, 200000, NULL);
    CHECK_EQ(pmd->TimerAtime, timer_atime);
    CHECK_EQ(pmd->int5_flag, int5_flag);

    free_ymfm_pmd(pmd);
}

// The backend has no input device, so the driver's keyboard checks see an idle
// keyboard while the emulated clock runs.
static void test_ymfm_keys(void) {
    struct pmd *pmd = new_ymfm_pmd();
    CHECK(run_ymfm_resident(pmd));
    struct pmd_ymfm *ymfm = pmd->user_data;

    static const uint8_t part[] = {0xff, 0, 0x40, 1, 0xf6, 0x41, 1, 0x80};
    load_module(pmd, part, sizeof(part));
    CHECK(pmd_mstart_f(pmd));

    // Idle keys must not reach either combination the timers test for:
    // CTRL+ESC stops the music, GRPH+CTRL selects the fast-forward tempo.
    pmd->key_check = 1;
    pmd->esc_sp_key = PMD_KEY_CTRL | PMD_KEY_ESC;
    pmd->grph_sp_key = PMD_KEY_GRPH | PMD_KEY_CTRL;
    pmd->music_flag = 0;
    pmd->tempo_d = 100;
    pmd->ff_tempo = 250;
    pmd->TimerB_speed = 0;
    pmd_ymfm_generate(ymfm, 200000, NULL);

    CHECK_EQ(pmd->play_flag, 1);
    CHECK_EQ(pmd->music_flag & 2, 0);
#if !sync
    // The sync build advances the music from its sync source, so Timer B never
    // reaches settempo_b and there is no fast-forward tempo to check.
    CHECK_EQ(pmd->TimerB_speed, pmd->tempo_d);
#endif

    free_ymfm_pmd(pmd);
}

// Playback through the emulated chip: the sequencer keys a note on, the
// generated samples become audible, and Timer A keeps counting.
#if !sync
static void test_ymfm_playback(void) {
    struct pmd *pmd = new_ymfm_pmd();
    CHECK(run_ymfm_resident(pmd));
    struct pmd_ymfm *ymfm = pmd->user_data;

    // A loud voice with a finite attack rate: a cleared voice area leaves every
    // operator at the slowest attack, which never becomes audible here.
    static const uint8_t voice[32] = {
        0x01, 0x01, 0x01, 0x01, // DT/ML
        0x00, 0x00, 0x00, 0x00, // TL
        0x1f, 0x1f, 0x1f, 0x1f, // KS/AR
        0x00, 0x00, 0x00, 0x00, // DR
        0x00, 0x00, 0x00, 0x00, // SR
        0x0f, 0x0f, 0x0f, 0x0f, // SL/RR
        0x07, // ALG 7 / FB 0
    };
    set_voice(pmd, 0, voice);

    // '@0' selects the voice, 'L' loops the notes, so the part keeps sounding.
    static const uint8_t part[] = {0xff, 0, 0x40, 1, 0xf6, 0x41, 1, 0x80};
    load_module(pmd, part, sizeof(part));
    CHECK(pmd_mstart_f(pmd));
    CHECK_EQ(pmd->play_flag, 1);

    uint8_t timer_atime = pmd->TimerAtime;
    bool audio = false;
    static int32_t samples[2 * 512];
    for (unsigned i = 0; i < 256; i++) {
        pmd_ymfm_generate(ymfm, 512, samples);
        if (i < 4) continue; // the envelope has not started yet
        for (size_t j = 0; j < sizeof(samples) / sizeof(samples[0]); j++) {
            if (samples[j] != 0) audio = true;
        }
    }

    CHECK(audio);
    CHECK(pmd->TimerAtime > timer_atime);
    CHECK_EQ(pmd->int5_flag, 0);
    CHECK_EQ(pmd->TimerAflag, 0);
    CHECK_EQ(pmd->TimerBflag, 0);
    // The sequencer has played the notes and looped back to the 'L' target.
    CHECK(pmd->parts[0].onkai == 0x40 || pmd->parts[0].onkai == 0x41);
    CHECK(pmd->parts[0].partloop != NULL);

    free_ymfm_pmd(pmd);
}
#endif

// The driver writes a note's keyoff and the next note's keyon in one pass. A
// port access clocks the chip, so the chip sees the off state and restarts the
// envelope; collapsing the two writes into one chip clock leaves the note at
// whatever the envelope had decayed to, which is silence for a percussive
// voice.
#if !sync
// Whether the chip produces sound over the next few milliseconds. The test
// voice decays, so the caller only looks at a short window after a key on.
static bool ymfm_audible(struct pmd_ymfm *ymfm) {
    static int32_t samples[2 * 512];
    pmd_ymfm_generate(ymfm, 512, samples);
    for (size_t j = 0; j < sizeof(samples) / sizeof(samples[0]); j++) {
        if (samples[j] != 0) return true;
    }
    return false;
}

// Drops the audio the port accesses so far (driver initialization, register
// writes) produced, so the next window starts at the current chip state.
static void ymfm_drain(struct pmd_ymfm *ymfm) {
    pmd_ymfm_generate(ymfm, 40000, NULL);
}

static void test_ymfm_retrigger(void) {
    struct pmd *pmd = new_ymfm_pmd();
    CHECK(run_ymfm_resident(pmd));
    struct pmd_ymfm *ymfm = pmd->user_data;
    uint16_t port1 = pmd->fm1_port1;
    uint16_t port2 = pmd->fm1_port2;

    // An instantly attacked voice that decays to silence within a few
    // milliseconds, on channel 1.
    static const uint8_t regs[][2] = {
        {0x30, 0x01}, {0x34, 0x01}, {0x38, 0x01}, {0x3c, 0x01},
        {0x40, 0x00}, {0x44, 0x00}, {0x48, 0x00}, {0x4c, 0x00},
        {0x50, 0x1f}, {0x54, 0x1f}, {0x58, 0x1f}, {0x5c, 0x1f},
        {0x60, 0x10}, {0x64, 0x10}, {0x68, 0x10}, {0x6c, 0x10},
        {0x70, 0x1f}, {0x74, 0x1f}, {0x78, 0x1f}, {0x7c, 0x1f},
        {0x80, 0xff}, {0x84, 0xff}, {0x88, 0xff}, {0x8c, 0xff},
        {0xb0, 0x0e}, {0xb4, 0xc0},
        {0xa4, 0x22}, {0xa0, 0x69},
    };
    for (size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
        pmd_ymfm_outb(ymfm, port1, regs[i][0]);
        pmd_ymfm_outb(ymfm, port2, regs[i][1]);
    }
    ymfm_drain(ymfm);

    // Key channel 1 on: the note is audible, then decays to silence.
    pmd_ymfm_outb(ymfm, port1, 0x28);
    pmd_ymfm_outb(ymfm, port2, 0x00);
    pmd_ymfm_outb(ymfm, port1, 0x28);
    pmd_ymfm_outb(ymfm, port2, 0xf0);
    CHECK(ymfm_audible(ymfm));
    pmd_ymfm_generate(ymfm, 200000, NULL);
    CHECK(!ymfm_audible(ymfm));

    // The same keyoff/keyon pair the sequencer writes for two consecutive
    // notes restarts the envelope, so the note is audible again.
    pmd_ymfm_outb(ymfm, port1, 0x28);
    pmd_ymfm_outb(ymfm, port2, 0x00);
    pmd_ymfm_outb(ymfm, port1, 0x28);
    pmd_ymfm_outb(ymfm, port2, 0xf0);
    CHECK(ymfm_audible(ymfm));

    free_ymfm_pmd(pmd);
}
#endif

// The SSG mix level scales the AC-coupled SSG output only, and the default is
// the level a PMD player mixes it at.
static int32_t ymfm_swing(struct pmd_ymfm *ymfm) {
    static int32_t samples[2 * 256];
    int32_t lo = 0, hi = 0;
    for (unsigned i = 0; i < 4; i++) {
        pmd_ymfm_generate(ymfm, 256, samples);
        for (size_t j = 0; j < sizeof(samples) / sizeof(samples[0]); j += 2) {
            if (samples[j] < lo) lo = samples[j];
            if (samples[j] > hi) hi = samples[j];
        }
    }
    return hi - lo;
}

// A full-volume SSG tone on channel 1, after the driver's own initialization.
static int32_t ymfm_ssg_tone_swing(struct pmd *pmd) {
    struct pmd_ymfm *ymfm = pmd->user_data;
    static const uint8_t regs[][2] = {
        {0x07, 0x3e}, // channel 1 tone on, channels 2/3 and noise off
        {0x00, 0xdd}, // a mid-range note
        {0x01, 0x01},
        {0x08, 0x0f}, // full volume
    };
    for (size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
        pmd_ymfm_outb(ymfm, pmd->fm1_port1, regs[i][0]);
        pmd_ymfm_outb(ymfm, pmd->fm1_port2, regs[i][1]);
    }
    // Drop the queued initialization audio, then let the DC blocker settle.
    pmd_ymfm_generate(ymfm, 40000, NULL);
    return ymfm_swing(ymfm);
}

static void test_ymfm_ssg_gain(void) {
    struct pmd_ymfm_config config;
    pmd_ymfm_default_config(&config, NULL);

    struct pmd *pmd = ymfm_pmd_gain(config.chip, config.board,
                                     config.base_port, 1.0);
    CHECK(run_ymfm_resident(pmd));
    int32_t chip = ymfm_ssg_tone_swing(pmd);
    free_ymfm_pmd(pmd);

    pmd = new_ymfm_pmd();
    CHECK(run_ymfm_resident(pmd));
    int32_t mixed = ymfm_ssg_tone_swing(pmd);
    free_ymfm_pmd(pmd);

    pmd = ymfm_pmd_gain(config.chip, config.board, config.base_port, 0.0);
    CHECK(run_ymfm_resident(pmd));
    int32_t muted = ymfm_ssg_tone_swing(pmd);
    free_ymfm_pmd(pmd);

    CHECK(chip > 0);
    // The gain scales the AC-coupled level; allow for the note's own edges.
    CHECK(mixed > (int32_t)(chip * PMD_YMFM_SSG_GAIN_DEFAULT * 0.98));
    CHECK(mixed < (int32_t)(chip * PMD_YMFM_SSG_GAIN_DEFAULT * 1.02));
    CHECK_EQ(muted, 0);
}

// The rhythm ROM is a file the host loads. A missing image must warn through
// pmd_sys and leave the store silent instead of failing.
static void test_ymfm_rom(void) {
    char cwd[512];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        fprintf(stderr, "%s:%d: getcwd failed\n", __FILE__, __LINE__);
        failures++;
        return;
    }

    char dir[] = "/tmp/pmd-ymfm-XXXXXX";
    if (mkdtemp(dir) == NULL) {
        fprintf(stderr, "%s:%d: mkdtemp failed\n", __FILE__, __LINE__);
        failures++;
        return;
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/ym2608_adpcm_rom.bin", dir);
    if (chdir(dir) != 0) {
        fprintf(stderr, "%s:%d: chdir failed\n", __FILE__, __LINE__);
        failures++;
        rmdir(dir);
        return;
    }

    ctx_reset();
    struct pmd *pmd = new_ymfm_pmd();
    struct pmd_ymfm *ymfm = pmd->user_data;

    // Missing image: one warning naming the file, and no sample data.
    pmd_ymfm_load_adpcm_a_file(ymfm, "ym2608_adpcm_rom.bin");
    CHECK_OUTPUT_CONTAINS(mes_warning);
    CHECK_OUTPUT_CONTAINS("ym2608_adpcm_rom.bin");
    CHECK_EQ(pmd_ymfm_adpcm_a_byte(ymfm, 0), 0);
    CHECK_EQ(pmd_ymfm_adpcm_a_byte(ymfm, 0x1234), 0);

    // Present image: no warning, and the bytes reach the chip's store.
    static const uint8_t image[] = {0x12, 0x34, 0x56, 0x78};
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "%s:%d: cannot create %s\n", __FILE__, __LINE__, path);
        failures++;
    } else {
        CHECK_EQ(fwrite(image, 1, sizeof(image), file), sizeof(image));
        fclose(file);

        ctx.out_len = 0;
        ctx.out[0] = '\0';
        pmd_ymfm_load_adpcm_a_file(ymfm, "ym2608_adpcm_rom.bin");
        CHECK_OUTPUT_EQUALS("");
        CHECK_EQ(pmd_ymfm_adpcm_a_byte(ymfm, 0), 0x12);
        CHECK_EQ(pmd_ymfm_adpcm_a_byte(ymfm, 1), 0x34);
        CHECK_EQ(pmd_ymfm_adpcm_a_byte(ymfm, 2), 0x56);
        CHECK_EQ(pmd_ymfm_adpcm_a_byte(ymfm, 3), 0x78);
        CHECK_EQ(pmd_ymfm_adpcm_a_byte(ymfm, 4), 0);
        CHECK_EQ(pmd_ymfm_adpcm_a_byte(ymfm, 0x4000), 0);

        // Loading a missing image again clears the store without warning twice.
        remove(path);
        pmd_ymfm_load_adpcm_a_file(ymfm, "ym2608_adpcm_rom.bin");
        CHECK_OUTPUT_EQUALS("");
        CHECK_EQ(pmd_ymfm_adpcm_a_byte(ymfm, 0), 0);
    }

    free_ymfm_pmd(pmd);

    if (chdir(cwd) != 0) {
        fprintf(stderr, "%s:%d: cannot restore the working directory\n",
                __FILE__, __LINE__);
        failures++;
    }
    rmdir(dir);
}


// Phase 8: the SSG effect driver (EFCDRV.ASM). pmd_eff_on loads the first frame
// of the effect, pmd_effoff silences it, and Timer A advances the frames.
static void test_int60_ssg_effect(void) {
    struct pmd *pmd = new_timer_pmd();
    ctx.write_count = 0;

    // Effect 0 (EFFECT.INC D_000): priority 1, one frame of one tick.
    pmd_eff_on(pmd, 0);
    CHECK_EQ(pmd->effon, 1);
    CHECK_EQ(pmd->psgefcnum, 0);
    CHECK_EQ(pmd->effcnt, 1);
    CHECK(pmd->effadr != NULL);
    CHECK_EQ(pmd->eswthz, 0x05dc); // registers 4 and 5
    CHECK_EQ(pmd->eswnhz, 31);     // register 6
    CHECK_EQ(pmd->eswtst, 127);
    CHECK_EQ(pmd->eswnst, 0);
    CHECK_EQ(pmd->eswnct, 0);
    CHECK_EQ(pmd->parts[test_part_ssg3].partmask & 2, 2);

    uint16_t port1 = pmd->fm1_port1;
    uint16_t port2 = pmd->fm1_port2;
    // :152 The mix register keeps the SSG enable bits, which get07 reads back
    // from the idle data port.
    uint8_t mix = (uint8_t)(ctx.pad_read & 0xdb);
    CHECK(has_reg_write(port1, port2, 0x04, 0xdc));
    CHECK(has_reg_write(port1, port2, 0x05, 0x05));
    CHECK(has_reg_write(port1, port2, 0x06, 0x1f));
    CHECK(has_reg_write(port1, port2, 0x07, mix));
    CHECK(has_reg_write(port1, port2, 0x0a, 0x0f));
    CHECK(has_reg_write(port1, port2, 0x0b, 0x00));
    CHECK(has_reg_write(port1, port2, 0x0c, 0x00));
    CHECK(has_reg_write(port1, port2, 0x0d, 0x00));

    // Effect 11 has priority 2, so it replaces effect 0. Effect 0 then loses
    // the priority test, though psgefcnum is already overwritten.
    pmd_eff_on(pmd, 11);
    CHECK_EQ(pmd->effon, 2);
    CHECK_EQ(pmd->psgefcnum, 11);
    pmd_eff_on(pmd, 0);
    CHECK_EQ(pmd->effon, 2);
    CHECK_EQ(pmd->psgefcnum, 0);

    // effoff: minimum volume, cut the noise channel, and clear the state.
    ctx.write_count = 0;
    pmd_effoff(pmd);
    CHECK_EQ(pmd->effon, 0);
    CHECK_EQ(pmd->psgefcnum, 0xff);
    CHECK(has_reg_write(port1, port2, 0x0a, 0x00));
    CHECK(has_reg_write(port1, port2, 0x07, (uint8_t)(mix | 0x24)));
    free_pmd(pmd);

    // The sweep: Timer A advances the frame count, and the frame's tone
    // increment is applied when the count runs out.
    pmd = new_timer_pmd();
    pmd_eff_on(pmd, 0);
    CHECK_EQ(pmd->effcnt, 1);
    ctx.write_count = 0;
    fire_effect_timer(pmd);
    CHECK_EQ(pmd->effcnt, 8);      // D_000's second frame
    CHECK_EQ(pmd->eswthz, 0x06a4);
    CHECK_EQ(pmd->eswtst, 127);
    CHECK(has_reg_write(port1, port2, 0x04, 0xa4));
    CHECK(has_reg_write(port1, port2, 0x05, 0x06));

    ctx.write_count = 0;
    fire_effect_timer(pmd);
    CHECK_EQ(pmd->effcnt, 7);
    CHECK_EQ(pmd->eswthz, 0x0723); // 0x06a4 + 127
    CHECK(has_reg_write(port1, port2, 0x04, 0x23));
    CHECK(has_reg_write(port1, port2, 0x05, 0x07));
    free_pmd(pmd);
}

// Phase 8: the PPSDRV path of eff_main. A negative effect number is handed to
// PPSDRV with the inverted volume, and the same voice is keyed off first.
static void test_int60_ssg_effect_pps(void) {
    struct pmd *pmd = new_timer_pmd();
    pmd->pps = &mock_pps;
    pmd->ppsdrv_flag = 1;

    pmd_eff_on(pmd, 0x85);
    CHECK_EQ(pmd->effon, 1);
    CHECK_EQ(pmd->psgefcnum, 0x85);
    // Only the rhythm path (effgo) records the last PPSDRV voice.
    CHECK_EQ(pmd->last_shot_data, 0);
    CHECK_EQ(ctx.pps_keyons, 1);
    CHECK_EQ(ctx.pps_keyon.num, 5);
    CHECK_EQ(ctx.pps_keyon.detune, 0);
    CHECK_EQ(ctx.pps_keyon.volume, 0); // the inverted default volume 15
    CHECK_EQ(pmd->parts[test_part_ssg3].partmask & 2, 2);

    // A second request does not reach PPSDRV: the priority test sees the
    // running effect and returns.
    pmd_eff_on(pmd, 0x85);
    CHECK_EQ(ctx.pps_keyoffs, 0);
    CHECK_EQ(ctx.pps_keyons, 2);

    ctx.write_count = 0;
    pmd_effoff(pmd);
    CHECK_EQ(ctx.pps_keyoffs, 1);
    CHECK_EQ(pmd->effon, 0);
    free_pmd(pmd);
}

#if ppz
// Phase 8: the PPZ8 driver calls mstart (:461-486) and opn_init (:848) make.
static void test_ppz8_start(void) {
    struct pmd *pmd = new_timer_pmd();
    pmd->ppz8 = &mock_ppz8;
    pmd->ppz_call_seg = 1;

    start_playback(pmd);
    CHECK_EQ(ctx.ppz_inits, 1);
    CHECK_EQ(ctx.ppz_stop_alls, 1);
    CHECK_EQ(ctx.ppz_removal_calls, 1);
    CHECK(ctx.ppz_removal_protect);
    // opn_init pans all eight PPZ8 channels to the centre.
    for (int ch = 0; ch < 8; ch++) {
        CHECK_EQ(ctx.ppz_pans[ch], 1);
        CHECK_EQ(ctx.ppz_pan[ch], 5);
    }

    // silence (called by mstop) stops the FIFO interrupt and keys off all
    // eight channels.
    ctx.ppz_fifo_stops = 0;
    for (int ch = 0; ch < 8; ch++) ctx.ppz_keyoffs[ch] = 0;
    CHECK(pmd_mstop_f(pmd));
    CHECK_EQ(ctx.ppz_fifo_stops, 1);
    for (int ch = 0; ch < 8; ch++) CHECK_EQ(ctx.ppz_keyoffs[ch], 1);
    free_pmd(pmd);
}

// Phase 8: a PPZ8 part's note, volume, tone, and pan through the vtable. The
// part addresses come from the PCM part's 0b4h command, as in a real module.
static void test_ppz8_part(void) {
    // The PPZ8 interrupt level matched, so the directly callable FM entry is
    // in use and mnp_ret dispatches through it.
    static const struct pmd_ppz8_voice voice = {
        .start = 0x100,
        .size = 0x0200,
        .loop_start = 0x40,
        .loop_end = 0x1f0,
        .rate = 16000,
    };
    struct pmd *pmd = new_timer_pmd();
    pmd->ppz8 = &mock_ppz8;
    pmd->ppz_call_seg = 2;
    ctx.ppz_voice = &voice;

    // 0b4h: the eight PPZ8 part offsets, then the PCM part's end marker.
    static const uint8_t pcm_part[] = {
        0xb4, 0x30, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0x80,
    };
    // Note, centre pan, voice 0, and the end marker.
    static const uint8_t ppz_part[] = {0x40, 1, 0xec, 3, 0xff, 0, 0x80};
    load_empty_module(pmd);
    load_part(pmd, 9, 24, pcm_part, sizeof(pcm_part));
    memcpy(pmd->mmlbuf + 0x30, ppz_part, sizeof(ppz_part));
    pmd->mmlbuf[22] = 0x60;
    pmd->mmlbuf[23] = 0;

    start_playback(pmd);
    run_timer_b(pmd);
    struct pmd_qq *di = &pmd->parts[test_part_ppz1];
    CHECK(di->address == pmd->mmlbuf + 0x30 + 2); // past the note and its length
    CHECK_EQ(di->fnum, 0x8000); // o5 C
    CHECK_EQ(di->leng, 1);
    CHECK_EQ(di->volume, 128); // ppz_extpartset's default
    CHECK_EQ(ctx.ppz_keyons[0], 1);
    CHECK_EQ(ctx.ppz_keyon_voice[0], 0);
    CHECK_EQ(ctx.ppz_volumes[0], 1);
    CHECK_EQ(ctx.ppz_volume[0], 8); // 128 >> 4
    CHECK_EQ(ctx.ppz_tones[0], 1);

    // mnp_ret gives PPZ8's own handler a turn (ppz_call_seg == 2).
    CHECK(ctx.ppz_fm_int_calls > 0);

    // The next tick runs the pan command, loads voice 0's loop and rate, and
    // then reaches the end marker.
    ctx.ppz_fm_int_calls = 0;
    run_timer_b(pmd);
    CHECK_EQ(di->fmpan, 5); // ppzpandata[3]
    // opn_init panned every channel to the centre at mstart, so this is the
    // second pan call for channel 0.
    CHECK_EQ(ctx.ppz_pans[0], 2);
    CHECK_EQ(ctx.ppz_pan[0], 5);
    CHECK_EQ(ctx.ppz_voice_calls, 1);
    CHECK_EQ(ctx.ppz_voice_num, 0);
    CHECK_EQ(ctx.ppz_loops[0], 1);
    CHECK_EQ(ctx.ppz_loop_start[0], 0x40);
    CHECK_EQ(ctx.ppz_loop_end[0], 0x1f0);
    CHECK_EQ(ctx.ppz_rates[0], 1);
    CHECK_EQ(ctx.ppz_rate[0], 16000);
    CHECK_EQ(di->loopcheck, 3);
    free_pmd(pmd);

    // Without PPZ8 the same module still sequencer-plays and makes no calls.
    pmd = new_timer_pmd();
    load_empty_module(pmd);
    load_part(pmd, 9, 24, pcm_part, sizeof(pcm_part));
    memcpy(pmd->mmlbuf + 0x30, ppz_part, sizeof(ppz_part));
    pmd->mmlbuf[22] = 0x60;
    pmd->mmlbuf[23] = 0;
    start_playback(pmd);
    run_timer_b(pmd);
    di = &pmd->parts[test_part_ppz1];
    CHECK_EQ(di->fnum, 0x8000);
    CHECK_EQ(ctx.ppz_keyons[0], 0);
    free_pmd(pmd);
}

// Phase 8: ppz_mml_part_mask (0c0h) masks and revives a PPZ8 part.
static void test_ppz8_mml_mask(void) {
    struct pmd *pmd = new_timer_pmd();
    pmd->ppz8 = &mock_ppz8;
    pmd->ppz_call_seg = 1;

    static const uint8_t pcm_part[] = {
        0xb4, 0x30, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0x80,
    };
    static const uint8_t ppz_part[] = {0x40, 1, 0xc0, 1, 0x80};
    load_empty_module(pmd);
    load_part(pmd, 9, 24, pcm_part, sizeof(pcm_part));
    memcpy(pmd->mmlbuf + 0x30, ppz_part, sizeof(ppz_part));
    pmd->mmlbuf[22] = 0x60;
    pmd->mmlbuf[23] = 0;

    start_playback(pmd);
    run_timer_b(pmd);
    struct pmd_qq *di = &pmd->parts[test_part_ppz1];
    CHECK_EQ(ctx.ppz_keyons[0], 1);
    run_timer_b(pmd);
    CHECK_EQ(di->partmask & 0x40, 0x40);
    // mstop's silence keyed all channels off once at mstart.
    CHECK_EQ(ctx.ppz_keyoffs[0], 2);
    free_pmd(pmd);
}
#endif

#if board2 && pcm
// Phase 8: the PCM86 path. A note loads its voice, starts the FIFO transfer,
// and advances through fifo_main.
static void test_pcm86_part(void) {
    struct pmd *pmd = new_timer_pmd();
    pmd->p86 = &mock_p86;
    pmd->pcm_table.pcm86.pcmadrs[0].offset = 0x100;
    pmd->pcm_table.pcm86.pcmadrs[0].size = 0x0400;

    // Voice 0, a note, and the end marker.
    static const uint8_t part[] = {0xff, 0, 0x40, 1, 0x80};
    load_empty_module(pmd);
    load_part(pmd, 9, 24, part, sizeof(part));
    pmd->mmlbuf[22] = 0x60;
    pmd->mmlbuf[23] = 0;

    ctx.write_count = 0;
    start_playback(pmd);
    run_timer_b(pmd);

    struct pmd_qq *di = &pmd->parts[test_part_pcm];
    CHECK_EQ(di->voicenum, 0);
    CHECK_EQ(di->onkai, 0x40);
    CHECK_EQ(pmd->_start_ofs, 0x100);
    CHECK_EQ(pmd->_size1, 0x0400);
    CHECK_EQ(pmd->play86_flag, 1);
    CHECK_EQ(pmd->trans_flag, 1);
    // play_86pcm's first transfer: one EMS push/pop and one data lookup.
    CHECK_EQ(ctx.p86_pushes, 1);
    CHECK_EQ(ctx.p86_pops, 1);
    CHECK_EQ(ctx.p86_data_calls, 1);
    CHECK_EQ(ctx.p86_offset, 0x100);
    // The volume register runs inverted (0Ah is the minimum).
    CHECK(has_write(0xa466, (uint8_t)((0xa0 | (((128 >> 4) & 0x0f) ^ 0x0f)))));
    // The transferred samples advanced the cursor and used up the size.
    CHECK(pmd->start_ofs > 0x100);
    CHECK(pmd->size1 < 0x0400);

    // A FIFO interrupt transfers the next buffer.
    ctx.fifo_pending = true;
    ctx.p86_pushes = 0;
    ctx.p86_pops = 0;
    run_fm_handler(pmd);
    CHECK_EQ(ctx.p86_pushes, 1);
    CHECK_EQ(ctx.p86_pops, 1);
    CHECK_EQ(pmd->play86_flag, 1);

    // Stopping the music stops the PCM86 and silences the FIFO.
    ctx.write_count = 0;
    CHECK(pmd_mstop_f(pmd));
    CHECK_EQ(pmd->play86_flag, 0);
    CHECK_EQ(pmd->trans_flag, 0);
    CHECK(has_write(0xa468, 0x00)); // playback stopped
    free_pmd(pmd);
}

// Phase 8: /HZ rewrites the table fnumsetm reads, which the PCM86 pitch then
// follows.
static void test_pcm86_freq_table(void) {
    struct pmd *pmd = new_timer_pmd();
    pmd->p86 = &mock_p86;

    static const uint8_t part[] = {0xff, 0, 0x40, 1, 0x80};
    load_empty_module(pmd);
    load_part(pmd, 9, 24, part, sizeof(part));
    pmd->mmlbuf[22] = 0x60;
    pmd->mmlbuf[23] = 0;

    start_playback(pmd);
    run_timer_b(pmd);
    uint16_t fnum = pmd->parts[test_part_pcm].fnum;
    uint16_t fnum2 = pmd->parts[test_part_pcm].fnum2;

    // The same note with a different /HZ table must produce a new pitch.
    free_pmd(pmd);

    pmd = new_timer_pmd();
    pmd->p86 = &mock_p86;
    load_empty_module(pmd);
    load_part(pmd, 9, 24, part, sizeof(part));
    pmd->mmlbuf[22] = 0x60;
    pmd->mmlbuf[23] = 0;
    // Rewrite the o5 C entry (index 48) to a different addsize pair.
    pmd->pcm_tune_data[48 * 3] = 0x05;
    pmd->pcm_tune_data[48 * 3 + 1] = 0x34;
    pmd->pcm_tune_data[48 * 3 + 2] = 0x12;
    start_playback(pmd);
    run_timer_b(pmd);
    CHECK_EQ(pmd->parts[test_part_pcm].fnum, 0xff05);
    CHECK_EQ(pmd->parts[test_part_pcm].fnum2, 0x1234);
    CHECK(fnum != pmd->parts[test_part_pcm].fnum || fnum2 != 0x1234);
    free_pmd(pmd);
}
#endif

#if board2 && adpcm && !ademu
// Phase 8: the YM2608 ADPCM effect path (PCMDRV.ASM:821).
static void test_adpcm_effect(void) {
    struct pmd *pmd = new_timer_pmd();
    pmd->pcm_gs_flag = 0; // ADPCM available
    pmd->pcm_table.adpcm_data.pcmadrs[0].start = 0x0100;
    pmd->pcm_table.adpcm_data.pcmadrs[0].stop = 0x0200;

    ctx.write_count = 0;
    pmd_pcm_effect(pmd, 0, 0x1234, 0, 60);

    CHECK_EQ(pmd->pcmflag, 1);
    CHECK_EQ(pmd->pcm_effec_num, 0);
    CHECK_EQ(pmd->parts[test_part_pcm].partmask & 2, 2);
    CHECK_EQ(pmd->_pcmstart, 0x0100);
    CHECK_EQ(pmd->_pcmstop, 0x0200);
    CHECK_EQ(pmd->_pcm_volume, 60);
    CHECK(has_reg_write(pmd->fm2_port1, pmd->fm2_port2, 0x0b, 60));
    CHECK(has_reg_write(pmd->fm2_port1, pmd->fm2_port2, 0x09, 0x34));
    CHECK(has_reg_write(pmd->fm2_port1, pmd->fm2_port2, 0x0a, 0x12));
    CHECK(has_reg_write(pmd->fm2_port1, pmd->fm2_port2, 0x01, 0x02));
    CHECK(has_reg_write(pmd->fm2_port1, pmd->fm2_port2, 0x00, 0x21));
    CHECK(has_reg_write(pmd->fm2_port1, pmd->fm2_port2, 0x02, 0x00));
    CHECK(has_reg_write(pmd->fm2_port1, pmd->fm2_port2, 0x03, 0x01));
    CHECK(has_reg_write(pmd->fm2_port1, pmd->fm2_port2, 0x04, 0x00));
    CHECK(has_reg_write(pmd->fm2_port1, pmd->fm2_port2, 0x05, 0x02));
    CHECK(has_reg_write(pmd->fm2_port1, pmd->fm2_port2, 0x00, 0xa0));
    CHECK(has_reg_write(pmd->fm2_port1, pmd->fm2_port2, 0x10, 0x80));
    CHECK(has_reg_write(pmd->fm2_port1, pmd->fm2_port2, 0x10, 0x18));
    free_pmd(pmd);

    // :883 The pan argument is rotated into the top two bits of register 1.
    pmd = new_timer_pmd();
    pmd->pcm_gs_flag = 0;
    pmd->pcm_table.adpcm_data.pcmadrs[0].start = 0x0100;
    pmd->pcm_table.adpcm_data.pcmadrs[0].stop = 0x0200;
    ctx.write_count = 0;
    pmd_pcm_effect(pmd, 0, 0x1234, 1, 60); // p1 = right
    CHECK(has_reg_write(pmd->fm2_port1, pmd->fm2_port2, 0x01, 0x42));
    free_pmd(pmd);

    pmd = new_timer_pmd();
    pmd->pcm_gs_flag = 0;
    pmd->pcm_table.adpcm_data.pcmadrs[0].start = 0x0100;
    pmd->pcm_table.adpcm_data.pcmadrs[0].stop = 0x0200;
    ctx.write_count = 0;
    pmd_pcm_effect(pmd, 0, 0x1234, 2, 60); // p2 = left
    CHECK(has_reg_write(pmd->fm2_port1, pmd->fm2_port2, 0x01, 0x82));
    free_pmd(pmd);

    // Without ADPCM the effect is dropped.
    pmd = new_timer_pmd();
    pmd->pcm_gs_flag = 1;
    ctx.write_count = 0;
    pmd_pcm_effect(pmd, 0, 0x1234, 0, 60);
    CHECK_EQ(pmd->pcmflag, 0);
    CHECK_EQ(ctx.write_count, 0);
    free_pmd(pmd);
}
#endif

#if board2 && !pcm
// Phase 8: the PCM part's note path for the YM2608 ADPCM variants. The PCM86
// variant takes it in test_pcm86_part.
static void test_pcm_part(void) {
#if ademu
    struct pmd *pmd = new_timer_pmd();
    pmd->ppz8 = &mock_ppz8;
    pmd->ppz_call_seg = 1;
    // @0 switches the ADPCM emulation on and hands channel 7's voice over.
    static const uint8_t part[] = {0xff, 0, 0x40, 1, 0x80};
    load_empty_module(pmd);
    load_part(pmd, 9, 24, part, sizeof(part));
    pmd->mmlbuf[22] = 0x60;
    pmd->mmlbuf[23] = 0;
    start_playback(pmd);
    ctx.ppz_adpcm_calls = 0;
    run_timer_b(pmd);
    struct pmd_qq *di = &pmd->parts[test_part_pcm];
    CHECK_EQ(di->voicenum, 0);
    CHECK_EQ(pmd->adpcm_emulate, 1);
    CHECK_EQ(ctx.ppz_adpcm_calls, 1);
    CHECK(ctx.ppz_adpcm_emulate);
    CHECK_EQ(ctx.ppz_keyons[7], 1);
    CHECK_EQ(ctx.ppz_keyon_voice[7], 0x8080);
    free_pmd(pmd);
#else
    struct pmd *pmd = new_timer_pmd();
    pmd->pcm_gs_flag = 0;
    pmd->pcm_table.adpcm_data.pcmadrs[0].start = 0x0100;
    pmd->pcm_table.adpcm_data.pcmadrs[0].stop = 0x0200;
    static const uint8_t part[] = {0xff, 0, 0x40, 1, 0x80};
    load_empty_module(pmd);
    load_part(pmd, 9, 24, part, sizeof(part));
    pmd->mmlbuf[22] = 0x60;
    pmd->mmlbuf[23] = 0;
    ctx.write_count = 0;
    start_playback(pmd);
    run_timer_b(pmd);
    struct pmd_qq *di = &pmd->parts[test_part_pcm];
    CHECK_EQ(di->voicenum, 0);
    CHECK_EQ(pmd->pcmstart, 0x0100);
    CHECK_EQ(pmd->pcmstop, 0x0200);
    CHECK_EQ(pmd->pcmrelease, 0x8000);
    // volsetm's ADPCM volume, and the keyon register sequence.
    CHECK(has_reg_write(pmd->fm2_port1, pmd->fm2_port2, 0x0b, 128));
    CHECK(has_reg_write(pmd->fm2_port1, pmd->fm2_port2, 0x01, 0x02));
    CHECK(has_reg_write(pmd->fm2_port1, pmd->fm2_port2, 0x00, 0xa0));
    CHECK(has_reg_write(pmd->fm2_port1, pmd->fm2_port2, 0x02, 0x00));
    CHECK(has_reg_write(pmd->fm2_port1, pmd->fm2_port2, 0x03, 0x01));
    free_pmd(pmd);
#endif
}
#endif

#if board2 && adpcm && !ademu
// Phase 8: a 0ceh whose third value is 8000h disables the release, and the
// original stores that value rather than leaving the previous one
// (PCMDRV.ASM:371).
static void test_pcmrepeat_release(void) {
    struct pmd *pmd = new_timer_pmd();
    pmd->pcm_gs_flag = 0;
    // Two repeat commands separated by a note, so they land on different ticks.
    static const uint8_t part[] = {
        0xff, 0,
        0xce, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x40, 1,
        0xce, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
        0x80,
    };
    load_empty_module(pmd);
    load_part(pmd, 9, 24, part, sizeof(part));
    pmd->mmlbuf[22] = 0x60;
    pmd->mmlbuf[23] = 0;
    pmd->pcm_table.adpcm_data.pcmadrs[0].start = 0x0100;
    pmd->pcm_table.adpcm_data.pcmadrs[0].stop = 0x0200;
    start_playback(pmd);

    run_timer_b(pmd); // @0, then the first 0ce: release = pcmstart
    CHECK_EQ(pmd->pcmrepeat1, 0x0100);
    CHECK_EQ(pmd->pcmrelease, 0x0100);

    run_timer_b(pmd); // the second 0ce: release disabled again
    CHECK_EQ(pmd->pcmrelease, 0x8000);
    free_pmd(pmd);
}
#endif

#if board2 && adpcm
// Phase 8: the PCM pitch adds the detune with saturation. A negative detune
// subtracts from the tone instead of clearing it (PCMDRV.ASM:745,
// PCMDRVE.ASM:684).
static void test_pcm_detune(void) {
    struct pmd *pmd = new_timer_pmd();
#if ademu
    pmd->ppz8 = &mock_ppz8;
    pmd->ppz_call_seg = 1;
#else
    pmd->pcm_gs_flag = 0;
    pmd->pcm_table.adpcm_data.pcmadrs[0].start = 0x0100;
    pmd->pcm_table.adpcm_data.pcmadrs[0].stop = 0x0200;
#endif
    // @0, D -16, then the note.
    static const uint8_t part[] = {0xff, 0, 0xfa, 0xf0, 0xff, 0x40, 8, 0x80};
    load_empty_module(pmd);
    load_part(pmd, 9, 24, part, sizeof(part));
    pmd->mmlbuf[22] = 0x60;
    pmd->mmlbuf[23] = 0;
    ctx.write_count = 0;
    start_playback(pmd);
    run_timer_b(pmd);

    struct pmd_qq *di = &pmd->parts[test_part_pcm];
    CHECK_EQ(di->detune, -16);
    // o4 C is pcm_tune_data[0] / 2.
    CHECK_EQ(di->fnum, 0x3132);
#if ademu
    CHECK_EQ(ctx.ppz_tones[7], 1);
    CHECK_EQ(ctx.ppz_tone[7], 0x3122);
#else
    CHECK(has_reg_write(pmd->fm2_port1, pmd->fm2_port2, 0x09, 0x22));
    CHECK(has_reg_write(pmd->fm2_port1, pmd->fm2_port2, 0x0a, 0x31));
#endif
    free_pmd(pmd);
}
#endif

#if board2 && !pcm
// Phase 8: the extended envelope divides the product by 16 and rounds up on the
// bit the fourth shift drops (PCMDRV.ASM:551). Volume 1 against (volume + 1) 8
// leaves 8/16, which rounds to 1; the bit above would leave 0.
static void test_pcm_extended_envelope(void) {
    struct pmd *pmd = new_timer_pmd();
#if ademu
    pmd->ppz8 = &mock_ppz8;
    pmd->ppz_call_seg = 1;
    static const uint8_t part[] = {0xff, 0, 0x40, 8, 0x80};
#else
    pmd->pcm_gs_flag = 0;
    pmd->pcm_table.adpcm_data.pcmadrs[0].start = 0x0100;
    pmd->pcm_table.adpcm_data.pcmadrs[0].stop = 0x0200;
    static const uint8_t part[] = {0xff, 0, 0x40, 8, 0x80};
#endif
    load_empty_module(pmd);
    load_part(pmd, 9, 24, part, sizeof(part));
    pmd->mmlbuf[22] = 0x60;
    pmd->mmlbuf[23] = 0;
    start_playback(pmd);
    run_timer_b(pmd);

    struct pmd_qq *di = &pmd->parts[test_part_pcm];
    di->envf = 0xff; // the extended envelope
    di->eenv_volume = 7;
    di->volume = 1;
    pmd->fadeout_speed = 1; // reach volsetm from the sustain path
    ctx.write_count = 0;
    run_timer_b(pmd);

#if ademu
    CHECK_EQ(ctx.ppz_volume[7], 1);
#else
    CHECK(has_reg_write(pmd->fm2_port1, pmd->fm2_port2, 0x0b, 0x01));
#endif
    free_pmd(pmd);
}
#endif

#if board2 && !pcm
// Phase 8: the YM2608 ADPCM volume squares the fadeout before applying it
// (PCMDRV.ASM:514, PCMDRVE.ASM:604). A fadeout of 2 leaves 126 from volume
// 128, where the unsquared fade would leave 127.
static void test_pcm_fadeout_square(void) {
    struct pmd *pmd = new_timer_pmd();
#if ademu
    pmd->ppz8 = &mock_ppz8;
    pmd->ppz_call_seg = 1;
    static const uint8_t part[] = {0xff, 0, 0x40, 8, 0x80};
#else
    pmd->pcm_gs_flag = 0;
    pmd->pcm_table.adpcm_data.pcmadrs[0].start = 0x0100;
    pmd->pcm_table.adpcm_data.pcmadrs[0].stop = 0x0200;
    static const uint8_t part[] = {0xff, 0, 0x40, 8, 0x80};
#endif
    load_empty_module(pmd);
    load_part(pmd, 9, 24, part, sizeof(part));
    pmd->mmlbuf[22] = 0x60;
    pmd->mmlbuf[23] = 0;
    start_playback(pmd);
    run_timer_b(pmd);

    // play_init gave the PCM part volume 128.
    struct pmd_qq *di = &pmd->parts[test_part_pcm];
    CHECK_EQ(di->volume, 128);
    pmd->fadeout_volume = 2;
    pmd->fadeout_speed = 1; // reach volsetm from the sustain path
    ctx.write_count = 0;
    run_timer_b(pmd);

#if ademu
    CHECK_EQ(ctx.ppz_volume[7], 126);
#else
    CHECK(has_reg_write(pmd->fm2_port1, pmd->fm2_port2, 0x0b, 126));
#endif
    free_pmd(pmd);
}
#endif

#if board2 && pcm
// Phase 8: the PCM86 command table replaces the extended lfo commands with
// parameter skips (PCMDRV86.ASM:263-274). 0bbh discards its byte, where the
// YM2608 tables run _lfo_extend and set the part's extend mode.
static void test_pcm86_command_table(void) {
    struct pmd *pmd = new_timer_pmd();
    pmd->p86 = &mock_p86;
    static const uint8_t part[] = {0x40, 1, 0xbb, 0x01, 0x40, 1, 0x80};
    load_empty_module(pmd);
    load_part(pmd, 9, 24, part, sizeof(part));
    pmd->mmlbuf[22] = 0x60;
    pmd->mmlbuf[23] = 0;
    start_playback(pmd);
    run_timer_b(pmd); // the first note
    run_timer_b(pmd); // 0bbh, then the note behind its parameter byte

    struct pmd_qq *di = &pmd->parts[test_part_pcm];
    CHECK_EQ(di->extendmode, 0);
    CHECK_EQ(di->onkai, 0x40);
    CHECK_EQ(di->fnum, 0xff00u | pmd->p86_freqtable[48 * 3]);
    CHECK(di->address == pmd->mmlbuf + 24 + 6);
    free_pmd(pmd);
}

// Phase 8: 0dah has no portamento in the PCM86 driver; its command table entry
// is jump1 (PCMDRV86.ASM:246), so the parameter byte is the only thing it
// consumes. Reading it as portam would swallow the note behind it.
static void test_pcm86_portamento_skip(void) {
    struct pmd *pmd = new_timer_pmd();
    pmd->p86 = &mock_p86;
    static const uint8_t part[] = {0x40, 1, 0xda, 0x55, 0x40, 1, 0x80};
    load_empty_module(pmd);
    load_part(pmd, 9, 24, part, sizeof(part));
    pmd->mmlbuf[22] = 0x60;
    pmd->mmlbuf[23] = 0;
    start_playback(pmd);
    run_timer_b(pmd); // the first note
    run_timer_b(pmd); // 0dah skips one byte, then the second note

    struct pmd_qq *di = &pmd->parts[test_part_pcm];
    CHECK_EQ(di->lfoswi & 8, 0); // no portamento
    CHECK_EQ(di->porta_num2, 0);
    CHECK_EQ(di->porta_num3, 0);
    CHECK_EQ(di->onkai, 0x40);
    CHECK(di->address == pmd->mmlbuf + 24 + 6);
    free_pmd(pmd);
}

// Phase 8: /S (pcm86_vol == 1) plays every note from 65h up one octave lower
// (PCMDRV86.ASM:823-830); the note below the threshold is untouched.
static void test_pcm86_s_octave(void) {
    struct pmd *pmd = new_timer_pmd();
    pmd->p86 = &mock_p86;
    pmd->_pcm86_vol = 1; // the /S option's stored value, applied by opn_init
    static const uint8_t part[] = {0x64, 1, 0x65, 1, 0x70, 1, 0x80};
    load_empty_module(pmd);
    load_part(pmd, 9, 24, part, sizeof(part));
    pmd->mmlbuf[22] = 0x60;
    pmd->mmlbuf[23] = 0;
    start_playback(pmd);
    CHECK_EQ(pmd->pcm86_vol, 1);

    struct pmd_qq *di = &pmd->parts[test_part_pcm];
    run_timer_b(pmd);
    CHECK_EQ(di->onkai, 0x64); // below the threshold
    run_timer_b(pmd);
    CHECK_EQ(di->onkai, 0x55);
    CHECK_EQ(di->fnum, 0xff00u | pmd->p86_freqtable[65 * 3]);
    run_timer_b(pmd);
    CHECK_EQ(di->onkai, 0x60);
    CHECK_EQ(di->fnum, 0xff00u | pmd->p86_freqtable[72 * 3]);
    free_pmd(pmd);
}
#endif

#if board2
// Phase 8: the volume LFO clamps at zero. A swing below the current volume
// silences the part instead of wrapping (PCMDRV.ASM:628, PCMDRV86.ASM:657).
static void test_pcm_volume_lfo(void) {
    struct pmd *pmd = new_timer_pmd();
#if pcm
    pmd->p86 = &mock_p86;
    static const uint8_t part[] = {0x40, 8, 0x80};
#elif ademu
    pmd->ppz8 = &mock_ppz8;
    pmd->ppz_call_seg = 1;
    // @0 hands the voice over and leaves partb on channel 7.
    static const uint8_t part[] = {0xff, 0, 0x40, 8, 0x80};
#else
    pmd->pcm_gs_flag = 0;
    pmd->pcm_table.adpcm_data.pcmadrs[0].start = 0x0100;
    pmd->pcm_table.adpcm_data.pcmadrs[0].stop = 0x0200;
    static const uint8_t part[] = {0xff, 0, 0x40, 8, 0x80};
#endif
    load_empty_module(pmd);
    load_part(pmd, 9, 24, part, sizeof(part));
    pmd->mmlbuf[22] = 0x60;
    pmd->mmlbuf[23] = 0;
    start_playback(pmd);
    run_timer_b(pmd);

    struct pmd_qq *di = &pmd->parts[test_part_pcm];
    di->volume = 8;
    di->lfoswi = 2;    // volume LFO
    di->lfodat = -16;  // a swing deeper than the volume
    pmd->fadeout_speed = 1; // reach volsetm from the sustain path
    ctx.write_count = 0;
    ctx.ppz_volumes[7] = 0;
    run_timer_b(pmd);

    // 8 - 16 clamps to 0; the wrapped low byte would be 0xf8.
    CHECK_EQ(di->lfodat, -16); // the LFO did not run
#if pcm
    // The PCM86 volume register runs inverted: 0 is 0a0h | 0f0h.
    CHECK(has_write(0xa466, 0xaf));
    CHECK(!has_write(0xa466, 0xa0));
#elif ademu
    CHECK_EQ(ctx.ppz_volumes[7], 1);
    CHECK_EQ(ctx.ppz_volume[7], 0);
#else
    CHECK(has_reg_write(pmd->fm2_port1, pmd->fm2_port2, 0x0b, 0x00));
#endif
    free_pmd(pmd);
}
#endif

#if ppz
// Phase 8: a negative 0ceh offset is relative to the end of the data, and the
// sign-extended value is added to the size (PPZDRV.ASM:379).
static void test_ppz8_loop_offsets(void) {
    static const struct pmd_ppz8_voice voice = {
        .start = 0x100,
        .size = 0x0400,
        .loop_start = 0x40,
        .loop_end = 0x1f0,
        .rate = 16000,
    };
    struct pmd *pmd = new_timer_pmd();
    pmd->ppz8 = &mock_ppz8;
    pmd->ppz_call_seg = 1;
    ctx.ppz_voice = &voice;

    // A one-count note, then 0ceh with start -4 and end -8 of the data size.
    static const uint8_t pcm_part[] = {
        0xb4, 0x30, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0x80,
    };
    static const uint8_t ppz_part[] = {
        0x40, 1,
        0xce, 0xfc, 0xff, 0xf8, 0xff, 0x00, 0x00,
        0x80,
    };
    load_empty_module(pmd);
    load_part(pmd, 9, 24, pcm_part, sizeof(pcm_part));
    memcpy(pmd->mmlbuf + 0x30, ppz_part, sizeof(ppz_part));
    pmd->mmlbuf[22] = 0x60;
    pmd->mmlbuf[23] = 0;
    start_playback(pmd);

    run_timer_b(pmd); // the note
    run_timer_b(pmd); // 0ceh
    // 0x400 - 4 and 0x400 - 8, relative to the data size.
    CHECK_EQ(ctx.ppz_loops[0], 1);
    CHECK_EQ(ctx.ppz_loop_start[0], 0x3fc);
    CHECK_EQ(ctx.ppz_loop_end[0], 0x3f8);
    free_pmd(pmd);

    // A positive value is already an absolute offset (PPZDRV.ASM:377 returns
    // it unchanged), so the data size must not be added.
    pmd = new_timer_pmd();
    pmd->ppz8 = &mock_ppz8;
    pmd->ppz_call_seg = 1;
    ctx.ppz_voice = &voice;
    static const uint8_t positive_part[] = {
        0x40, 1,
        0xce, 0x10, 0x00, 0x20, 0x00, 0x00, 0x00,
        0x80,
    };
    load_empty_module(pmd);
    load_part(pmd, 9, 24, pcm_part, sizeof(pcm_part));
    memcpy(pmd->mmlbuf + 0x30, positive_part, sizeof(positive_part));
    pmd->mmlbuf[22] = 0x60;
    pmd->mmlbuf[23] = 0;
    start_playback(pmd);

    run_timer_b(pmd); // the note
    run_timer_b(pmd); // 0ceh
    CHECK_EQ(ctx.ppz_loops[0], 1);
    CHECK_EQ(ctx.ppz_loop_start[0], 0x10);
    CHECK_EQ(ctx.ppz_loop_end[0], 0x20);
    free_pmd(pmd);
}

// Phase 8: 0b4h is a plain parameter skip in the PPZ8 command table
// (PPZDRV.ASM:284); only the PCM tables route it to ppz_extpartset.
static void test_ppz8_extpart_skip(void) {
    struct pmd *pmd = new_timer_pmd();
    pmd->ppz8 = &mock_ppz8;
    pmd->ppz_call_seg = 1;

    static const uint8_t pcm_part[] = {
        0xb4, 0x30, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0x80,
    };
    // A note, then 0b4h with eight nonzero part offsets, then a note.
    static const uint8_t ppz_part[] = {
        0x40, 1,
        0xb4, 0x38, 0x00, 0x40, 0x00, 0x48, 0x00, 0x50,
        0x00, 0x58, 0x00, 0x60, 0x00, 0x68, 0x00, 0x70, 0x00,
        0x40, 1,
        0x80,
    };
    load_empty_module(pmd);
    load_part(pmd, 9, 24, pcm_part, sizeof(pcm_part));
    memcpy(pmd->mmlbuf + 0x30, ppz_part, sizeof(ppz_part));
    pmd->mmlbuf[22] = 0x60;
    pmd->mmlbuf[23] = 0;
    start_playback(pmd);

    run_timer_b(pmd); // the first note
    run_timer_b(pmd); // 0b4h, then the second note

    // The eight offsets were skipped, so no sibling PPZ8 part was started.
    for (int i = 1; i < 8; i++) {
        const struct pmd_qq *sibling = &pmd->parts[test_part_ppz1 + i];
        CHECK(sibling->address == NULL);
        CHECK_EQ(sibling->volume, 0);
        CHECK_EQ(sibling->fmpan, 0);
    }
    struct pmd_qq *di = &pmd->parts[test_part_ppz1];
    CHECK_EQ(di->onkai, 0x40);
    CHECK(di->address == pmd->mmlbuf + 0x45);
    free_pmd(pmd);
}
#endif

#if board2 && adpcm && ademu
// Phase 8: the ADPCM emulation's 0ceh offsets are 1/64ths of the voice size
// and are scaled by 32 for PPZ8, in 16-bit arithmetic (PCMDRVE.ASM:305).
static void test_pcm_loop_offsets(void) {
    static const struct pmd_ppz8_voice voice = {
        .start = 0x100,
        .size = 0x0400,
        .loop_start = 0x40,
        .loop_end = 0x1f0,
        .rate = 16000,
    };
    struct pmd *pmd = new_timer_pmd();
    pmd->ppz8 = &mock_ppz8;
    pmd->ppz_call_seg = 1;
    ctx.ppz_voice = &voice;

    // @0 hands the voice over, then 0ceh with start -4 and end -8.
    static const uint8_t part[] = {
        0xff, 0,
        0xce, 0xfc, 0xff, 0xf8, 0xff, 0x00, 0x00,
        0x80,
    };
    load_empty_module(pmd);
    load_part(pmd, 9, 24, part, sizeof(part));
    pmd->mmlbuf[22] = 0x60;
    pmd->mmlbuf[23] = 0;
    start_playback(pmd);

    ctx.ppz_loops[7] = 0;
    run_timer_b(pmd);
    // @0 loads the voice's own loop first, then 0ceh replaces it.
    CHECK_EQ(ctx.ppz_loops[7], 2);
    // The voice is 0x400 bytes, i.e. 16 in 1/64ths: start 12, end 8, x32.
    CHECK_EQ(ctx.ppz_loop_start[7], 12u * 32u);
    CHECK_EQ(ctx.ppz_loop_end[7], 8u * 32u);
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
    test_playback_no_board();
    test_playback_start_stop();
    test_playback_note();
    test_playback_note_repeat();
    test_playback_program_voice();
    test_playback_detune_block();
    test_playback_commands();
    test_playback_loop();
    test_playback_fade();
    test_playback_keys();
    test_playback_voice();
    test_playback_fb_relative();
    test_playback_rhythm();
#if board2
    test_playback_rhythm_volume();
#endif
    test_playback_psg_envelope();
    test_playback_rewind_key();
    test_playback_gate();
    test_playback_porta();
    test_playback_lfo();
    test_playback_key_mask();
    test_int60_no_board();
    test_int60_board();
    test_int60_fm_effect();
    test_int60_pause();
    test_int60_part_mask();
    test_int60_get_memo();
    test_int60_get_joy();
    test_int60_callbacks();
    test_int60_reentrancy();
    test_int60_ff_music();
    test_int60_ff_music_skip();
    test_resident_cut();
    test_change_remove();
    test_int60_ssg_effect();
    test_int60_ssg_effect_pps();
#if ppz
    test_ppz8_start();
    test_ppz8_part();
    test_ppz8_mml_mask();
    test_ppz8_loop_offsets();
    test_ppz8_extpart_skip();
#endif
#if board2 && !pcm
    test_pcm_part();
#endif
#if board2 && pcm
    test_pcm86_part();
    test_pcm86_freq_table();
    test_pcm86_command_table();
    test_pcm86_portamento_skip();
    test_pcm86_s_octave();
#endif
#if board2 && adpcm && !ademu
    test_adpcm_effect();
    test_pcmrepeat_release();
#endif
#if board2 && adpcm && ademu
    test_pcm_loop_offsets();
#endif
#if board2 && adpcm
    test_pcm_detune();
#endif
#if board2 && !pcm
    test_pcm_extended_envelope();
    test_pcm_fadeout_square();
#endif
#if board2
    test_pcm_volume_lfo();
#endif
    test_ymfm_vtable();
    test_ymfm_probe();
#if !va && !board2
    test_ymfm_ym2203();
    test_ymfm_ymf288();
#endif
    test_ymfm_timer_install();
    test_ymfm_timer_remove();
    test_ymfm_keys();
#if !sync
    test_ymfm_playback();
    test_ymfm_retrigger();
#endif
    test_ymfm_ssg_gain();
    test_ymfm_rom();
    test_timer_a();
    test_timer_b();
    test_timer_ab();
    test_timer_status_zero();
    test_timer_nesting();
    test_timer_callbacks();
#if sync
    test_timer_sync();
#endif
#if vsync
    test_timer_vsync();
    test_timer_vsync_restore();
#endif

    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
