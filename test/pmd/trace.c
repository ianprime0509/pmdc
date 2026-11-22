// The trace-driven PMD test host.
//
// Executes a .RUN script against the translated driver, recording the same
// console text and event stream that test/pmd/repro.py captures from the
// original binary under Unicorn. The board and timer models here are mirrored
// one-for-one in repro.py; keep the two in step.

#define _POSIX_C_SOURCE 200809L

#include "pmd.h"

#include <ctype.h>
#include <inttypes.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef trace_variant
#error "trace_variant must name the PMD variant this host was built for"
#endif

enum {
    max_actions = 1024,
    // The original's 8259 PIC reads back all masked.
    host_pic_mask = 0xFF,
};

static const uint16_t candidate_bases[] = {0x088, 0x188, 0x288, 0x388};

static const uint16_t timer_a_step = 72;
static const uint16_t timer_b_step = 1152;

// Register 0Eh bits 6-7 select the FM interrupt. The fake board reports 00h,
// which is IRQ 3 and INT 0Bh (vector_table in PMD.ASM).
static const uint8_t board_0eh = 0x00;

// PC-98 host ports the translated driver leaves to the host: the two 8259 PIC
// banks and the 8254 PIT. repro.py does not record them either.
static bool is_host_port(uint16_t port) {
    switch (port) {
    case 0x00:
    case 0x02:
    case 0x08:
    case 0x0A:
    case 0x71:
    case 0x77:
        return true;
    default:
        return false;
    }
}

struct timer {
    uint16_t reload;
    bool running;
    uint64_t deadline;
};

struct board {
    enum { BOARD_NONE, BOARD_26K, BOARD_86B } kind;
    uint16_t base;
    bool opna;
    // The address register and last data write, per candidate port pair. The
    // 0AAh probe reads the last data write back.
    uint8_t addr[4];
    uint8_t latch[0x200];
    uint8_t flags;
    uint8_t control;
    struct timer timer_a;
    struct timer timer_b;
    // check_86b switches the OPNA off, probes, then switches it on. Its
    // second-chip status port reads FFh while off and not busy while on.
    bool opna_on;
};

struct action {
    char *verb;
    char *rest;
};

struct script {
    const char *name;
    char *driver;
    char *board;
    char *chip;
    char *pmdopt;
    bool reads;
    struct action actions[max_actions];
    size_t action_count;
};

struct trace {
    struct pmd *pmd;
    struct board board;
    FILE *log;
    FILE *out;
    uint64_t now;
    const char *cases_dir;
    const char *name;
    void (*handler)(struct pmd *);
};

// --replay turns the golden's timer lines into the delivery schedule, so a
// wrong timer register write shows up as the register difference it is rather
// than as a cascade of moved interrupts. The comparison still fails.
struct replay_event {
    uint64_t clock;
    unsigned flags;
};

static struct {
    struct replay_event events[8192];
    size_t count;
    size_t pos;
} replay;
static bool replay_mode;

static struct trace trace;
static struct script script;

// pmd_resident_main reports fatal errors through pmd_sys.exit, which the
// original turns into a DOS exit. The host records it like the COM's AH=31h
// or AH=4Ch exit and stops the script.
static jmp_buf exit_jmp;
static bool exit_requested;
static int exit_status;

static void fail(const char *format, ...) __attribute__((noreturn, format(printf, 1, 2)));
static void fail(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "%s: ", script.name != NULL ? script.name : "trace");
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
    va_end(args);
    exit(EXIT_FAILURE);
}

static void *xmalloc(size_t size) {
    void *p = malloc(size);
    if (p == NULL) fail("out of memory");
    return p;
}

static char *xstrdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = xmalloc(len);
    memcpy(copy, s, len);
    return copy;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return s;
}

// -- Script ---------------------------------------------------------------

static bool read_file(const char *path, char **data, size_t *len) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) return false;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    char *buffer = xmalloc((size_t)size + 1);
    size_t read = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    if (read != (size_t)size) {
        free(buffer);
        return false;
    }
    buffer[read] = '\0';
    *data = buffer;
    *len = read;
    return true;
}

static void script_header(const char *key, char *value) {
    if (strcmp(key, "driver") == 0) {
        free(script.driver);
        script.driver = xstrdup(value);
    } else if (strcmp(key, "board") == 0) {
        free(script.board);
        script.board = xstrdup(value);
    } else if (strcmp(key, "chip") == 0) {
        free(script.chip);
        script.chip = xstrdup(value);
    } else if (strcmp(key, "pmdopt") == 0) {
        free(script.pmdopt);
        script.pmdopt = xstrdup(value);
    } else if (strcmp(key, "reads") == 0) {
        script.reads = strcmp(value, "on") == 0;
    } else {
        fail("unknown header '%s'", key);
    }
}

static void parse_script(const char *path) {
    char *data;
    size_t len;
    if (!read_file(path, &data, &len)) fail("cannot read %s", path);

    // Header comments come first; every other line is an action.
    char *line = data;
    bool seen_action = false;
    while (line != NULL && *line != '\0') {
        char *next = strchr(line, '\n');
        if (next != NULL) *next++ = '\0';
        char *current = line;
        line = next;
        if (*current == '\r') continue;
        if (current[0] == ';') {
            if (seen_action) continue;
            char *colon = strchr(current + 1, ':');
            if (colon == NULL) continue;
            *colon = '\0';
            char *key = trim(current + 1);
            for (char *p = key; *p != '\0'; p++) *p = (char)tolower((unsigned char)*p);
            script_header(key, trim(colon + 1));
            continue;
        }
        char *text = trim(current);
        if (*text == '\0') continue;
        seen_action = true;
        if (script.action_count == max_actions) fail("too many actions");
        char *space = strchr(text, ' ');
        struct action *action = &script.actions[script.action_count++];
        if (space == NULL) {
            action->verb = xstrdup(text);
            action->rest = xstrdup("");
        } else {
            *space = '\0';
            action->verb = xstrdup(text);
            action->rest = xstrdup(trim(space + 1));
        }
    }
    free(data);
}

// -- Board and timer model ------------------------------------------------

static int pair_index(uint16_t base) {
    for (unsigned i = 0; i < sizeof(candidate_bases) / sizeof(candidate_bases[0]); i++) {
        if (candidate_bases[i] == base) return (int)i;
    }
    return -1;
}

// The base of the port pair a port belongs to, or 0 if it is not a chip port.
static uint16_t pair_base(uint16_t port) {
    for (unsigned i = 0; i < sizeof(candidate_bases) / sizeof(candidate_bases[0]); i++) {
        uint16_t base = candidate_bases[i];
        if (port >= base && port < base + 8 && (port - base) % 2 == 0) return base;
    }
    return 0;
}

static bool board_present(uint16_t base) {
    return trace.board.base == base && trace.board.kind != BOARD_NONE;
}

static uint32_t timer_a_period(void) {
    return (uint32_t)((1024 - (trace.board.timer_a.reload & 0x3FF)) * timer_a_step);
}

static uint32_t timer_b_period(void) {
    return (uint32_t)((256 - (trace.board.timer_b.reload & 0xFF)) * timer_b_step);
}

static void start_stop(struct timer *timer, bool load, uint64_t now, uint32_t period) {
    if (load && !timer->running) {
        timer->running = true;
        timer->deadline = now + period;
    } else if (!load && timer->running) {
        timer->running = false;
    }
}

static void board_write_register(uint8_t reg, uint8_t value, uint64_t now) {
    struct board *board = &trace.board;
    switch (reg) {
    case 0x24:
        board->timer_a.reload = (uint16_t)((board->timer_a.reload & 0x300) | value);
        break;
    case 0x25:
        board->timer_a.reload = (uint16_t)((board->timer_a.reload & 0x0FF) | ((value & 0x03) << 8));
        break;
    case 0x26:
        board->timer_b.reload = value;
        break;
    case 0x27:
        board->control = value;
        if ((value & 0x10) != 0) board->flags &= (uint8_t)~1;
        if ((value & 0x20) != 0) board->flags &= (uint8_t)~2;
        start_stop(&board->timer_a, (value & 0x01) != 0, now, timer_a_period());
        start_stop(&board->timer_b, (value & 0x02) != 0, now, timer_b_period());
        break;
    default:
        break;
    }
}

static uint8_t board_inb(uint16_t port) {
    struct board *board = &trace.board;
    if (is_host_port(port)) return host_pic_mask;
    uint16_t base = pair_base(port);
    if (base != 0) {
        if (!board_present(base)) return 0xFF;
        int index = pair_index(base);
        if (port == base) return board->flags;
        if (port == base + 2) {
            uint8_t addr = index >= 0 ? board->addr[index] : 0;
            if (addr == 0x0E || addr == 0x0F) return board_0eh;
            // The 86 board's OPNA answers register FFh with 1, which is what
            // check_86b probes for; an absent chip echoes the probe.
            if (board->opna && addr == 0xFF) return 0x01;
            return board->latch[port >> 1];
        }
        // The second chip: FFh is the probe value check_86b looks for, and
        // the driver's rdychk needs BUSY clear once the OPNA is on.
        if (port == base + 4) return board->opna_on ? 0x00 : 0xFF;
        return 0xFF;
    }
    switch (port) {
    case 0xA460:
        return board->opna ? 0x00 : 0xFF;
    case 0xA66E:
        return 0x00;
    case 0xA468:
        return 0x00;
    default:
        fail("unmodelled port read %#06x", port);
    }
}

static void board_outb(uint16_t port, uint8_t value, uint64_t now) {
    struct board *board = &trace.board;
    uint16_t base = pair_base(port);
    if (base != 0) {
        int index = pair_index(base);
        if (port == base) {
            if (index >= 0) board->addr[index] = value;
        } else if (port == base + 2) {
            board->latch[port >> 1] = value;
            if (board_present(base) && index >= 0) {
                board_write_register(board->addr[index], value, now);
            }
        } else {
            board->latch[port >> 1] = value;
        }
        return;
    }
    if (port == 0xA460) {
        board->opna_on = (value & 1) != 0;
        return;
    }
    if (port == 0x5F || port == 0xA66E || port == 0xA468) return;
    fail("unmodelled port write %#06x = %#04x", port, value);
}

static uint64_t next_deadline(void) {
    const struct board *board = &trace.board;
    uint64_t deadline = UINT64_MAX;
    if (board->timer_a.running && board->timer_a.deadline < deadline) {
        deadline = board->timer_a.deadline;
    }
    if (board->timer_b.running && board->timer_b.deadline < deadline) {
        deadline = board->timer_b.deadline;
    }
    return deadline;
}

static unsigned board_advance(uint64_t now) {
    struct board *board = &trace.board;
    unsigned latched = 0;
    if (board->timer_a.running && board->timer_a.deadline == now) {
        if ((board->control & 0x04) != 0) {
            board->flags |= 1;
            latched |= 1;
        }
        board->timer_a.deadline = now + timer_a_period();
    }
    if (board->timer_b.running && board->timer_b.deadline == now) {
        if ((board->control & 0x08) != 0) {
            board->flags |= 2;
            latched |= 2;
        }
        board->timer_b.deadline = now + timer_b_period();
    }
    return latched;
}

// -- pmd_sys and pmd_hw ---------------------------------------------------

static void sys_putc(char c, void *user_data) {
    struct trace *t = user_data;
    fputc((unsigned char)c, t->out);
}

static void sys_print(const char *mes, void *user_data) {
    struct trace *t = user_data;
    fputs(mes, t->out);
}

static PMDC_NORETURN void sys_exit(int status, void *user_data) {
    (void)user_data;
    exit_status = status;
    exit_requested = true;
    longjmp(exit_jmp, 1);
}

static char *sys_getenv(const char *name, void *user_data) {
    (void)user_data;
    if (script.pmdopt != NULL && strcmp(name, "PMDOPT") == 0) return script.pmdopt;
    return NULL;
}

static const struct pmd_sys trace_sys = {
    .putc = sys_putc,
    .print = sys_print,
    .exit = sys_exit,
    .getenv = sys_getenv,
};

static uint8_t hw_inb(uint16_t port, void *user_data) {
    struct trace *t = user_data;
    if (script.reads && !is_host_port(port)) {
        fprintf(t->log, "%" PRIu64 " inb %x\n", t->now, port);
    }
    return board_inb(port);
}

static void hw_outb(uint16_t port, uint8_t value, void *user_data) {
    struct trace *t = user_data;
    if (is_host_port(port)) return;
    fprintf(t->log, "%" PRIu64 " outb %x %x\n", t->now, port, value);
    board_outb(port, value, t->now);
}

static void hw_install_fm_timer_handler(void (*handler)(struct pmd *), void *user_data) {
    struct trace *t = user_data;
    t->handler = handler;
}

static void hw_remove_fm_timer_handler(void *user_data) {
    struct trace *t = user_data;
    t->handler = NULL;
}

static const struct pmd_hw trace_hw = {
    .inb = hw_inb,
    .outb = hw_outb,
    .install_fm_timer_handler = hw_install_fm_timer_handler,
    .remove_fm_timer_handler = hw_remove_fm_timer_handler,
    .read_key_state = NULL,
};

// -- Actions --------------------------------------------------------------

static void event_ret(const char *name, uint8_t first, uint8_t second, bool two) {
    if (two) {
        fprintf(trace.log, "%" PRIu64 " ret %s %02x %02x\n", trace.now, name, first, second);
    } else {
        fprintf(trace.log, "%" PRIu64 " ret %s %02x\n", trace.now, name, first);
    }
}

static uint8_t result_flag(bool ok) {
    return ok ? 0x00 : 0xFF;
}

static void do_load(const char *name) {
    size_t size = (size_t)trace.pmd->mmldat_lng * 1024;
    uint8_t *address = pmd_get_musdat_adr(trace.pmd);
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", trace.cases_dir, name);
    FILE *file = fopen(path, "rb");
    if (file == NULL) fail("cannot open fixture %s", path);
    memset(address, 0, size);
    size_t read = fread(address, 1, size, file);
    bool ok = ferror(file) == 0 && fgetc(file) == EOF;
    fclose(file);
    if (!ok) fail("%s does not fit in the %zu-byte data area", name, size);
    (void)read;
}

static unsigned long parse_number(const char *text) {
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 0);
    if (end == text || *trim(end) != '\0') fail("bad number '%s'", text);
    return value;
}

// Delivers one timer interrupt: from the replay schedule, or from the board
// model. Returns the latched flags (0 when the model's deadline latched none).
static unsigned deliver_one(void) {
    unsigned latched;
    if (replay_mode) {
        if (replay.pos >= replay.count) fail("replay: the golden has no more timer events");
        trace.now = replay.events[replay.pos].clock;
        latched = replay.events[replay.pos].flags;
        replay.pos++;
        trace.board.flags |= (uint8_t)latched;
    } else {
        uint64_t deadline = next_deadline();
        if (deadline == UINT64_MAX) fail("no timer is running");
        trace.now = deadline;
        latched = board_advance(deadline);
    }
    if (latched == 0) return 0;
    const char *name = latched == 3 ? "ab" : latched == 2 ? "b" : "a";
    fprintf(trace.log, "%" PRIu64 " %s\n", trace.now, name);
    if (trace.handler != NULL) trace.handler(trace.pmd);
    return latched;
}

static void do_run(const char *rest) {
    char kind[16];
    char count_text[32];
    if (sscanf(rest, "%15s %31s", kind, count_text) != 2 || strcmp(kind, "b") != 0) {
        fail("run takes 'b N'");
    }
    unsigned long limit = parse_number(count_text);
    unsigned long count = 0;
    unsigned long spins = 0;
    while (count < limit) {
        unsigned latched = deliver_one();
        if ((latched & 2) != 0) {
            count++;
        } else if (latched == 0) {
            if (++spins > 1000000) fail("run: no timer interrupt latched");
        }
    }
}

static void do_run_until(const char *rest) {
    char kind[16];
    char value_text[32];
    char max_text[16];
    char limit_text[32];
    if (sscanf(rest, "%15s %31s %15s %31s", kind, value_text, max_text, limit_text) != 4 ||
        strcmp(kind, "status2") != 0 || strcmp(max_text, "max") != 0) {
        fail("run-until takes 'status2 V max N'");
    }
    // A status byte: the script writes it as hex, like the trace.
    unsigned value = (unsigned)strtoul(value_text, NULL, 16);
    unsigned long limit = parse_number(limit_text);
    unsigned long count = 0;
    unsigned long spins = 0;
    for (;;) {
        unsigned latched = deliver_one();
        if (latched != 0) {
            if ((latched & 2) != 0) count++;
            if (trace.pmd->status2 == value) return;
        } else if (++spins > 1000000) {
            fail("run-until: no timer interrupt latched");
        }
        if (count >= limit) {
            fail("run-until: status2 did not reach %#04x in %lu Timer B ticks", value, limit);
        }
    }
}

static void run_actions(void) {
    for (size_t i = 0; i < script.action_count; i++) {
        const char *verb = script.actions[i].verb;
        const char *rest = script.actions[i].rest;
        if (strcmp(verb, "main") == 0) {
            exit_requested = false;
            if (setjmp(exit_jmp) == 0) {
                pmd_resident_main(trace.pmd, (char *)rest);
                fprintf(trace.log, "%" PRIu64 " ret exit 00\n", trace.now);
            } else {
                fprintf(trace.log, "%" PRIu64 " ret exit %02x\n", trace.now, exit_status);
                return;
            }
        } else if (strcmp(verb, "load") == 0) {
            do_load(rest);
        } else if (strcmp(verb, "mstart") == 0) {
            event_ret("mstart", result_flag(pmd_mstart_f(trace.pmd)), 0, false);
        } else if (strcmp(verb, "mstop") == 0) {
            event_ret("mstop", result_flag(pmd_mstop_f(trace.pmd)), 0, false);
        } else if (strcmp(verb, "status") == 0) {
            struct pmd_status status = pmd_get_status(trace.pmd);
            event_ret("status", status.status, status.status2, true);
        } else if (strcmp(verb, "drvchk") == 0) {
            struct pmd_drv_info info = pmd_drv_chk(trace.pmd);
            fprintf(trace.log, "%" PRIu64 " ret drvchk %02x %02x %02x\n", trace.now,
                    info.board, info.version, info.version_char);
        } else if (strcmp(verb, "size") == 0) {
            struct pmd_size size = pmd_get_size(trace.pmd);
            fprintf(trace.log, "%" PRIu64 " ret size %02x %02x %02x\n", trace.now,
                    size.music, size.voice, size.effect);
        } else if (strcmp(verb, "ss") == 0) {
            event_ret("ss", (uint8_t)pmd_get_ss(trace.pmd), 0, false);
        } else if (strcmp(verb, "fv") == 0) {
            event_ret("fv", pmd_get_fv(trace.pmd), 0, false);
        } else if (strcmp(verb, "fmefcnum") == 0) {
            event_ret("fmefcnum", pmd_get_fmefc_num(trace.pmd), 0, false);
        } else if (strcmp(verb, "pcmefcnum") == 0) {
            event_ret("pcmefcnum", pmd_get_pcmefc_num(trace.pmd), 0, false);
        } else if (strcmp(verb, "psgefcnum") == 0) {
            struct pmd_psgefcnum num = pmd_get_psgefcnum(trace.pmd);
            event_ret("psgefcnum", num.psgefcnum, num.effon, true);
        } else if (strcmp(verb, "ppsdrv") == 0) {
            event_ret("ppsdrv", pmd_get_ppsdrv_flag(trace.pmd), 0, false);
        } else if (strcmp(verb, "joy") == 0) {
            struct pmd_joy joy = pmd_get_joy(trace.pmd);
            event_ret("joy", joy.joystick1, joy.joystick2, true);
        } else if (strcmp(verb, "fout") == 0) {
            pmd_fout(trace.pmd, (uint8_t)parse_number(rest));
        } else if (strcmp(verb, "setfv") == 0) {
            pmd_set_fv(trace.pmd, (uint8_t)parse_number(rest));
        } else if (strcmp(verb, "partmask") == 0) {
            pmd_part_mask(trace.pmd, (uint8_t)parse_number(rest));
        } else if (strcmp(verb, "effon") == 0) {
            pmd_eff_on(trace.pmd, (uint8_t)parse_number(rest));
        } else if (strcmp(verb, "effoff") == 0) {
            pmd_effoff(trace.pmd);
        } else if (strcmp(verb, "fmefon") == 0) {
            pmd_fm_effect_on(trace.pmd, (uint8_t)parse_number(rest));
        } else if (strcmp(verb, "fmefoff") == 0) {
            pmd_fm_effect_off(trace.pmd);
        } else if (strcmp(verb, "ff") == 0) {
            event_ret("ff", pmd_ff_music(trace.pmd, (uint16_t)parse_number(rest)), 0, false);
        } else if (strcmp(verb, "pause") == 0) {
            pmd_pause_on(trace.pmd);
        } else if (strcmp(verb, "resume") == 0) {
            pmd_pause_off(trace.pmd);
        } else if (strcmp(verb, "run") == 0) {
            do_run(rest);
        } else if (strcmp(verb, "run-until") == 0) {
            do_run_until(rest);
        } else {
            fail("unknown action '%s'", verb);
        }
    }
}

// -- Setup ----------------------------------------------------------------

static void setup_board(void) {
    struct board *board = &trace.board;
    if (strcmp(script.board, "26K") == 0) {
        board->kind = BOARD_26K;
        board->base = 0x088;
        board->opna = false;
    } else if (strcmp(script.board, "86B") == 0) {
        board->kind = BOARD_86B;
        board->base = 0x188;
        board->opna = true;
    } else if (strcmp(script.board, "none") == 0) {
        board->kind = BOARD_NONE;
        board->base = 0;
        board->opna = false;
    } else {
        fail("unsupported board '%s'", script.board);
    }
}

// The golden's timer lines are the replay schedule.
static void load_replay(void) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.LOG", trace.cases_dir, trace.name);
    FILE *file = fopen(path, "r");
    if (file == NULL) fail("replay: cannot open golden %s", path);
    char line[512];
    while (fgets(line, sizeof(line), file) != NULL) {
        char clock_text[64];
        char event[16];
        if (sscanf(line, "%63s %15s", clock_text, event) != 2) continue;
        unsigned flags;
        if (strcmp(event, "a") == 0) {
            flags = 1;
        } else if (strcmp(event, "b") == 0) {
            flags = 2;
        } else if (strcmp(event, "ab") == 0) {
            flags = 3;
        } else {
            continue;
        }
        if (replay.count == sizeof(replay.events) / sizeof(replay.events[0])) {
            fail("replay: too many timer events");
        }
        replay.events[replay.count].clock = strtoull(clock_text, NULL, 10);
        replay.events[replay.count].flags = flags;
        replay.count++;
    }
    fclose(file);
}

static void check_case(void) {
    if (strcmp(script.driver, trace_variant) != 0) {
        fprintf(stderr, "%s: skipped (driver %s, built for %s)\n", script.name,
                script.driver, trace_variant);
        exit(77);
    }
    if (strcmp(script.chip, "YM2203") != 0 && strcmp(script.chip, "YM2608") != 0) {
        fail("unsupported chip '%s'", script.chip);
    }
    // The chip has to match the board it is declared with.
    if (strcmp(script.board, "26K") == 0 && strcmp(script.chip, "YM2203") != 0) {
        fail("board 26K is a YM2203 board, not %s", script.chip);
    }
    if (strcmp(script.board, "86B") == 0 && strcmp(script.chip, "YM2608") != 0) {
        fail("board 86B is a YM2608 board, not %s", script.chip);
    }
}

int main(int argc, char **argv) {
    int arg = 1;
    if (arg < argc && strcmp(argv[arg], "--replay") == 0) {
        replay_mode = true;
        arg++;
    }
    if (argc - arg != 2) {
        fprintf(stderr, "usage: trace [--replay] CASES-DIR NAME\n");
        return EXIT_FAILURE;
    }
    trace.cases_dir = argv[arg];
    trace.name = argv[arg + 1];
    script.driver = xstrdup("pmd");
    script.board = xstrdup("26K");
    script.chip = xstrdup("YM2203");
    script.pmdopt = NULL;
    script.name = trace.name;

    char run_path[1024];
    snprintf(run_path, sizeof(run_path), "%s/%s.RUN", trace.cases_dir, trace.name);
    parse_script(run_path);
    check_case();
    setup_board();
    if (replay_mode) load_replay();

    char out_path[1024];
    char log_path[1024];
    snprintf(out_path, sizeof(out_path), "%s.OUT", trace.name);
    snprintf(log_path, sizeof(log_path), "%s.LOG", trace.name);
    trace.out = fopen(out_path, "wb");
    trace.log = fopen(log_path, "wb");
    if (trace.out == NULL || trace.log == NULL) fail("cannot create output files");

    static struct pmd pmd;
    memset(&pmd, 0, sizeof(pmd));
    pmd.sys = &trace_sys;
    pmd.hw = &trace_hw;
    pmd.pps = NULL;
    pmd.ppz8 = NULL;
    pmd.p86 = NULL;
    pmd.user_data = &trace;
    trace.pmd = &pmd;
    trace.now = 0;

    pmd_init(&pmd);
    run_actions();

    if (fclose(trace.out) != 0 || fclose(trace.log) != 0) fail("cannot write output files");
    return EXIT_SUCCESS;
}
