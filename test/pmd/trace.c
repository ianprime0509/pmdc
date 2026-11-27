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

// The 88VA's YM2608 sits at 044h; the PC-98 boards at 088h-388h.
static const uint16_t candidate_bases[] = {0x044, 0x088, 0x188, 0x288, 0x388};

static const uint16_t timer_a_step = 72;
static const uint16_t timer_b_step = 1152;

// Register 0Eh bits 6-7 select the FM interrupt. The fake board reports 00h,
// which is IRQ 3 and INT 0Bh (vector_table in PMD.ASM).
static const uint8_t board_0eh = 0x00;

// The 88VA relocates the 8259 PICs to 184h-18Ah (PMD.ASM:265).
static bool va_ports;

// Host ports the translated driver leaves to the host: the two PC-98 8259 PIC
// banks and 8254 PIT, and the 88VA's port 32h, where int_init clears the OPN
// interrupt request. On the 88VA the PICs move to 184h-18Ah (PMD.ASM:265), so
// those four ports are host-owned there and the FM board sits at 044h instead.
// repro.py does not record these ports either.
static bool is_host_port(uint16_t port) {
    switch (port) {
    case 0x00:
    case 0x02:
    case 0x08:
    case 0x0A:
    case 0x32:
    case 0x71:
    case 0x77:
        return true;
    default:
        break;
    }
    if (va_ports) {
        switch (port) {
        case 0x184:
        case 0x186:
        case 0x188:
        case 0x18A:
            return true;
        default:
            break;
        }
    }
    return false;
}

struct timer {
    uint16_t reload;
    bool running;
    uint64_t deadline;
};

struct board {
    enum { BOARD_NONE, BOARD_26K, BOARD_86B, BOARD_VA } kind;
    uint16_t base;
    // The first and second chip's address and data ports. The PC-98 boards
    // use word pairs (base, base+2, base+4, base+6); the 88VA drives the
    // YM2608 through four adjacent byte ports.
    uint16_t fm1_addr;
    uint16_t fm1_data;
    uint16_t fm2_addr;
    uint16_t fm2_data;
    bool opna;
    // Each chip's address register and the last data write. The 0AAh probe
    // reads the last data write back.
    uint8_t addr1;
    uint8_t addr2;
    uint8_t latch[0x200];
    uint8_t flags;
    uint8_t control;
    struct timer timer_a;
    struct timer timer_b;
    // check_86b switches the OPNA off, probes, then switches it on. Its
    // second-chip status port reads FFh while off and not busy while on.
    bool opna_on;
    // The second chip's ADPCM-B external memory bank (registers 0x00-0x0F in
    // the high register bank, 0x110 for the flag control). ADRAMCHK.ASM reads
    // back what it wrote and only needs BRDY (bit 3) to be set; EOS is not
    // checked. The address is masked into the modeled memory the way the
    // 8-bit/ROM modes shift the register value by five bits.
    uint8_t adpcm_regs[0x20];
    uint8_t adpcm_mem[0x10000];
    uint16_t adpcm_addr;
    uint8_t adpcm_dummy;
    bool adpcm_external;
    bool adpcm_record;
    // The ADPCM-A sample memory, which holds the rhythm ROM image the chip
    // reads when the rhythm part keys an instrument.
    uint8_t rhythm_rom[0x10000];
    // The PCM86 card's FIFO register. Bit 4 is the interrupt flag the driver
    // polls at 0A468h; bits 5 and 7 are the interrupt enable and playback
    // start it sets. The card raises the flag once per timer delivery while
    // playback runs, and any write that clears bit 4 clears it.
    uint8_t fifo_control;
    bool fifo_pending;
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
    bool pps;
    bool ppz8;
    bool p86;
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
    // First-seen order aliases for pointers the two hosts cannot compare.
    const void *aliases[16];
    size_t alias_count;
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

// The driver reports fatal errors through pmd_sys.exit, which the original
// turns into a DOS exit. The host records it like the COM's AH=31h or AH=4Ch
// exit; a `main` that exits ends the script, while a `change` that exits
// leaves the resident running.
static jmp_buf exit_jmp;
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
    } else if (strcmp(key, "drivers") == 0) {
        for (char *token = strtok(value, " "); token != NULL; token = strtok(NULL, " ")) {
            if (strcmp(token, "pps") == 0) {
                script.pps = true;
            } else if (strcmp(token, "ppz8") == 0) {
                script.ppz8 = true;
            } else if (strcmp(token, "p86") == 0) {
                script.p86 = true;
            } else {
                fail("unsupported driver '%s'", token);
            }
        }
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

// Whether `port` belongs to the chip that would sit at `base`. A candidate
// base that is not the declared board's answers FFh, which is how the probes
// discover an absent chip.
static bool candidate_port(uint16_t base, uint16_t port) {
    if (base == 0x044) return port >= 0x044 && port <= 0x047;
    return port >= base && port < base + 8 && (port - base) % 2 == 0;
}

// The base of the candidate chip a port belongs to, or 0 if it is not one.
static uint16_t candidate_base(uint16_t port) {
    for (unsigned i = 0; i < sizeof(candidate_bases) / sizeof(candidate_bases[0]); i++) {
        if (candidate_port(candidate_bases[i], port)) return candidate_bases[i];
    }
    return 0;
}

static void set_ports(struct board *board) {
    if (board->kind == BOARD_VA) {
        board->fm1_addr = 0x044;
        board->fm1_data = 0x045;
        board->fm2_addr = 0x046;
        board->fm2_data = 0x047;
    } else {
        board->fm1_addr = board->base;
        board->fm1_data = (uint16_t)(board->base + 2);
        board->fm2_addr = (uint16_t)(board->base + 4);
        board->fm2_data = (uint16_t)(board->base + 6);
    }
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

// The ADPCM-B address register pair in the second chip's high register bank,
// shifted by the memory mode: the 8-bit and ROM/RAM modes address in 32-byte
// units (ymfm_adpcm.cpp:687).
static uint16_t adpcm_address(const struct board *board) {
    unsigned shift = (board->adpcm_regs[0x01] & 0x03) != 0 ? 5 : 2;
    unsigned address = board->adpcm_regs[0x02] | ((unsigned)board->adpcm_regs[0x03] << 8);
    return (uint16_t)((address << shift) & 0xFFFF);
}

static void adpcm_write_register(struct board *board, uint8_t reg, uint8_t value) {
    board->adpcm_regs[reg] = value;
    switch (reg) {
    case 0x00:
        board->adpcm_external = (value & 0x20) != 0;
        board->adpcm_record = (value & 0x40) != 0;
        // A read transfer consumes two dummy reads before data; a record
        // transfer clears them on its first write (ymfm_adpcm.cpp:575).
        board->adpcm_dummy = board->adpcm_external && !board->adpcm_record ? 2 : 0;
        break;
    case 0x03:
        board->adpcm_addr = adpcm_address(board);
        break;
    case 0x08:
        if (board->adpcm_record && board->adpcm_external) {
            if (board->adpcm_dummy != 0) {
                board->adpcm_dummy = 0;
                board->adpcm_addr = adpcm_address(board);
            }
            board->adpcm_mem[board->adpcm_addr++] = value;
        }
        break;
    default:
        break;
    }
}

// The data port reads back the stored ADPCM bytes in read mode; the two dummy
// reads do not advance. Other modes leave the last write latched.
static uint8_t adpcm_read_data(struct board *board) {
    if (!board->adpcm_external || board->adpcm_record) return 0xFF;
    if (board->adpcm_dummy != 0) {
        board->adpcm_dummy--;
        board->adpcm_addr = adpcm_address(board);
        return 0;
    }
    return board->adpcm_mem[board->adpcm_addr++];
}

static uint8_t board_inb(uint16_t port) {
    struct board *board = &trace.board;
    if (is_host_port(port)) return host_pic_mask;
    uint16_t base = candidate_base(port);
    if (base != 0) {
        if (!board_present(base)) return 0xFF;
        if (port == board->fm1_addr) return board->flags;
        if (port == board->fm1_data) {
            uint8_t addr = board->addr1;
            if (addr == 0x0E || addr == 0x0F) return board_0eh;
            // The 86 board's OPNA answers register FFh with 1, which is what
            // check_86b probes for; an absent chip echoes the probe.
            if (board->opna && addr == 0xFF) return 0x01;
            return board->latch[port >> 1];
        }
        // The second chip: FFh is the probe value check_86b looks for, and
        // the driver's rdychk needs BUSY clear once the OPNA is on. The 88VA
        // has no OPNA gate. BRDY (bit 3) reports the ADPCM-B bank as ready,
        // which is what ADRAMCHK.ASM polls.
        if (port == board->fm2_addr) {
            if (board->adpcm_external) return 0x08;
            return board->kind == BOARD_VA ? 0x00 : board->opna_on ? 0x00 : 0xFF;
        }
        if (port == board->fm2_data) return adpcm_read_data(board);
        return 0xFF;
    }
    switch (port) {
    case 0xA460:
        return board->opna ? 0x00 : 0xFF;
    case 0xA468:
        // The PCM86 FIFO register reads back its control bits; bit 4 is the
        // interrupt flag rather than the value last written there.
        return (uint8_t)((board->fifo_control & (uint8_t)~0x10) |
                         (board->fifo_pending ? 0x10 : 0x00));
    case 0xA66E:
        return 0x00;
    default:
        fail("unmodelled port read %#06x", port);
    }
}

static void board_outb(uint16_t port, uint8_t value, uint64_t now) {
    struct board *board = &trace.board;
    uint16_t base = candidate_base(port);
    if (base != 0) {
        if (port == board->fm1_addr) {
            board->addr1 = value;
        } else if (port == board->fm1_data) {
            board->latch[port >> 1] = value;
            if (board_present(base)) board_write_register(board->addr1, value, now);
        } else if (port == board->fm2_addr) {
            board->addr2 = value;
        } else if (port == board->fm2_data) {
            board->latch[port >> 1] = value;
            // Registers 00h-1Fh of the high bank are the ADPCM-B interface;
            // the rest are the FM6/rhythm registers (ymfm_opn.cpp:1224).
            if (board_present(base) && board->addr2 < 0x20) {
                adpcm_write_register(board, board->addr2, value);
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
    if (port == 0xA468) {
        board->fifo_control = value;
        if ((value & 0x10) == 0) board->fifo_pending = false;
        return;
    }
    if (port == 0x5F || port == 0xA466 || port == 0xA46A || port == 0xA46C ||
        port == 0xA66E) return;
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

// -- External driver mocks ------------------------------------------------

// repro.py's fake resident PPSDRV and PPZ8 record the same named operations
// from the original's INT 64h/7Fh register contracts, so the two traces are
// comparable.

static void pps_keyoff(void *user_data) {
    struct trace *t = user_data;
    fprintf(t->log, "%" PRIu64 " pps keyoff\n", t->now);
}

static void pps_keyon(struct pmd_pps_keyon keyon, void *user_data) {
    struct trace *t = user_data;
    fprintf(t->log, "%" PRIu64 " pps keyon %02x %02x %02x\n", t->now, keyon.num,
            keyon.detune, keyon.volume);
}

static void pps_pdr_switch(uint8_t value, void *user_data) {
    struct trace *t = user_data;
    fprintf(t->log, "%" PRIu64 " pps pdr %02x %02x\n", t->now, value >> 1,
            (unsigned)(value & 1));
}

static const struct pmd_drv_pps trace_pps = {
    .keyoff = pps_keyoff,
    .keyon = pps_keyon,
    .pdr_switch = pps_pdr_switch,
};

#if ppz
static void ppz8_init(void *user_data) {
    struct trace *t = user_data;
    fprintf(t->log, "%" PRIu64 " ppz8 init\n", t->now);
}

static void ppz8_stop_all(void *user_data) {
    struct trace *t = user_data;
    fprintf(t->log, "%" PRIu64 " ppz8 stop\n", t->now);
}

static void ppz8_fifo_interrupt_stop(void *user_data) {
    struct trace *t = user_data;
    fprintf(t->log, "%" PRIu64 " ppz8 fifo\n", t->now);
}

static void ppz8_keyon(uint8_t channel, uint16_t voice, void *user_data) {
    struct trace *t = user_data;
    fprintf(t->log, "%" PRIu64 " ppz8 keyon %02x %04x\n", t->now, channel, voice);
}

static void ppz8_keyoff(uint8_t channel, void *user_data) {
    struct trace *t = user_data;
    fprintf(t->log, "%" PRIu64 " ppz8 keyoff %02x\n", t->now, channel);
}

static void ppz8_set_volume(uint8_t channel, uint8_t volume, void *user_data) {
    struct trace *t = user_data;
    fprintf(t->log, "%" PRIu64 " ppz8 volume %02x %02x\n", t->now, channel, volume);
}

static void ppz8_set_tone(uint8_t channel, uint16_t tone, void *user_data) {
    struct trace *t = user_data;
    fprintf(t->log, "%" PRIu64 " ppz8 tone %02x %04x\n", t->now, channel, tone);
}

static void ppz8_set_loop(uint8_t channel, uint32_t start, uint32_t end, void *user_data) {
    struct trace *t = user_data;
    fprintf(t->log, "%" PRIu64 " ppz8 loop %02x %x %x\n", t->now, channel, start, end);
}

static void ppz8_set_pan(uint8_t channel, uint8_t pan, void *user_data) {
    struct trace *t = user_data;
    fprintf(t->log, "%" PRIu64 " ppz8 pan %02x %02x\n", t->now, channel, pan);
}

static void ppz8_set_source_rate(uint8_t channel, uint16_t rate, void *user_data) {
    struct trace *t = user_data;
    fprintf(t->log, "%" PRIu64 " ppz8 rate %02x %04x\n", t->now, channel, rate);
}

static void ppz8_set_adpcm_emulation(bool enable, void *user_data) {
    struct trace *t = user_data;
    fprintf(t->log, "%" PRIu64 " ppz8 adpcm %02x\n", t->now, (unsigned)enable);
}

static void ppz8_set_removal_protection(bool protect, void *user_data) {
    struct trace *t = user_data;
    fprintf(t->log, "%" PRIu64 " ppz8 protect %02x\n", t->now, (unsigned)protect);
}

// The fake board reports IRQ 3, so PPZ8 answers the same level the driver
// expects from its own 010h register read.
static uint8_t ppz8_get_irq_level(void *user_data) {
    struct trace *t = user_data;
    uint8_t level = (uint8_t)(t->pmd->int_level + 8);
    fprintf(t->log, "%" PRIu64 " ppz8 irq %02x\n", t->now, level);
    return level;
}

// The fake PZI bank the same way repro.py's resident PPZ8 does: a zeroed
// entry for every voice, so the loop and rate it reports are both zero.
static const struct pmd_ppz8_voice *ppz8_get_voice(uint8_t voice, void *user_data) {
    static const struct pmd_ppz8_voice zero_voice;
    (void)voice;
    (void)user_data;
    return &zero_voice;
}

static void ppz8_fm_int(void *user_data) {
    struct trace *t = user_data;
    fprintf(t->log, "%" PRIu64 " ppz8 fmint\n", t->now);
}

static const struct pmd_drv_ppz8 trace_ppz8 = {
    .init = ppz8_init,
    .stop_all = ppz8_stop_all,
    .fifo_interrupt_stop = ppz8_fifo_interrupt_stop,
    .keyon = ppz8_keyon,
    .keyoff = ppz8_keyoff,
    .set_volume = ppz8_set_volume,
    .set_tone = ppz8_set_tone,
    .set_loop = ppz8_set_loop,
    .set_pan = ppz8_set_pan,
    .set_source_rate = ppz8_set_source_rate,
    .set_adpcm_emulation = ppz8_set_adpcm_emulation,
    .set_removal_protection = ppz8_set_removal_protection,
    .get_irq_level = ppz8_get_irq_level,
    .get_voice = ppz8_get_voice,
    .fm_int = ppz8_fm_int,
};
#endif

#if board2 && pcm
// repro.py's fake resident P86DRV (INT 65h). It holds the loaded .P86 sample
// data in one 64 KiB area and resolves the driver's 24-bit offsets into it.
enum { pcm86_area_size = 0x10000 };

static uint8_t pcm86_area[pcm86_area_size];

static uint8_t p86_get_version(void *user_data) {
    struct trace *t = user_data;
    fprintf(t->log, "%" PRIu64 " p86 version 10\n", t->now);
    return 0x10;
}

static void p86_push_ems(void *user_data) {
    struct trace *t = user_data;
    fprintf(t->log, "%" PRIu64 " p86 push\n", t->now);
}

static void p86_pop_ems(void *user_data) {
    struct trace *t = user_data;
    fprintf(t->log, "%" PRIu64 " p86 pop\n", t->now);
}

// The translated driver treats NULL as the end of the data; the original has
// no such check, so a case must keep its offsets inside the area.
static const uint8_t *p86_get_data_offset(uint32_t offset, void *user_data) {
    struct trace *t = user_data;
    fprintf(t->log, "%" PRIu64 " p86 data %" PRIx32 "\n", t->now, offset);
    if (offset >= pcm86_area_size) return NULL;
    return pcm86_area + offset;
}

static const struct pmd_drv_p86 trace_p86 = {
    .get_version = p86_get_version,
    .push_ems = p86_push_ems,
    .pop_ems = p86_pop_ems,
    .get_data_offset = p86_get_data_offset,
};
#endif

// -- Actions --------------------------------------------------------------

static void event_ret(const char *name, uint8_t first, uint8_t second, bool two) {
    if (two) {
        fprintf(trace.log, "%" PRIu64 " ret %s %02x %02x\n", trace.now, name, first, second);
    } else {
        fprintf(trace.log, "%" PRIu64 " ret %s %02x\n", trace.now, name, first);
    }
}

// A pointer inside the driver is interned as p1, p2, ... in first-seen order:
// the translated struct is not the original's byte layout, so only the alias
// pattern is comparable.
static void event_pointer(const char *name, const void *pointer) {
    if (pointer == NULL) {
        fprintf(trace.log, "%" PRIu64 " ret %s none\n", trace.now, name);
        return;
    }
    size_t index = 0;
    while (index < trace.alias_count && trace.aliases[index] != pointer) index++;
    if (index == trace.alias_count) {
        if (trace.alias_count == sizeof(trace.aliases) / sizeof(trace.aliases[0])) {
            fail("too many pointer aliases");
        }
        trace.aliases[trace.alias_count++] = pointer;
    }
    fprintf(trace.log, "%" PRIu64 " ret %s p%zu\n", trace.now, name, index + 1);
}

// A pointer into the data area is compared as an offset from its base: that
// layout is the original's and both hosts share it.
static void event_data_offset(const char *name, const void *pointer) {
    ptrdiff_t offset = (const uint8_t *)pointer - pmd_get_musdat_adr(trace.pmd);
    fprintf(trace.log, "%" PRIu64 " ret %s %zx\n", trace.now, name, (size_t)offset);
}

static uint8_t result_flag(bool ok) {
    return ok ? 0x00 : 0xFF;
}

// `load` writes into the music buffer; `loadvoice` and `loadeffect` write
// into the voice and effect buffers the driver returns through INT 60h.
static void load_area(uint8_t *address, size_t size, const char *name) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", trace.cases_dir, name);
    FILE *file = fopen(path, "rb");
    if (file == NULL) fail("cannot open fixture %s", path);
    memset(address, 0, size);
    size_t read = fread(address, 1, size, file);
    bool ok = ferror(file) == 0 && fgetc(file) == EOF;
    fclose(file);
    if (!ok) fail("%s does not fit in the %zu-byte buffer", name, size);
    (void)read;
}

static void do_load(const char *name) {
    load_area(pmd_get_musdat_adr(trace.pmd), (size_t)trace.pmd->mmldat_lng * 1024,
              name);
}

static void do_loadvoice(const char *name) {
    load_area(pmd_get_tondat_adr(trace.pmd), (size_t)trace.pmd->voicedat_lng * 1024,
              name);
}

static void do_loadeffect(const char *name) {
    load_area(pmd_get_efcdat_adr(trace.pmd), (size_t)trace.pmd->effecdat_lng * 1024,
              name);
}

// `loadpcm` fills the PCM86 voice table the way P86DRV does after reading a
// .P86 file: the fixture holds the file header, the 256 six-byte entries, and
// the sample data that get_data_offset maps.
static void do_loadpcm(const char *name) {
#if board2 && pcm
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", trace.cases_dir, name);
    char *data;
    size_t len;
    if (!read_file(path, &data, &len)) fail("cannot open fixture %s", path);

    enum { pcm86_header = 16, pcm86_voices = 256, pcm86_entry = 6 };
    const size_t table_len = (size_t)pcm86_voices * pcm86_entry;
    if (len < pcm86_header + table_len) fail("%s: truncated PCM86 data file", name);
    if (memcmp(data, "PCM86 DATA\n", 11) != 0) fail("%s: not a PCM86 data file", name);
    uint32_t total = (uint32_t)(uint8_t)data[13] | ((uint32_t)(uint8_t)data[14] << 8) |
                     ((uint32_t)(uint8_t)data[15] << 16);
    size_t sample_len = len - (pcm86_header + table_len);
    if (total != sample_len) {
        fail("%s: header size %" PRIu32 " does not match the %zu data bytes", name,
             total, sample_len);
    }

    union pmd_pcm_table *table = pmd_get_pcm_adr(trace.pmd);
    if (table == NULL) fail("%s: this variant has no PCM86 voice table", name);
    memset(&table->pcm86, 0, sizeof(table->pcm86));
    for (size_t i = 0; i < pcm86_voices; i++) {
        const uint8_t *entry = (const uint8_t *)data + pcm86_header + i * pcm86_entry;
        table->pcm86.pcmadrs[i].offset =
            (uint32_t)entry[0] | ((uint32_t)entry[1] << 8) | ((uint32_t)entry[2] << 16);
        table->pcm86.pcmadrs[i].size =
            (uint32_t)entry[3] | ((uint32_t)entry[4] << 8) | ((uint32_t)entry[5] << 16);
    }
    memset(pcm86_area, 0, sizeof(pcm86_area));
    memcpy(pcm86_area, data + pcm86_header + table_len, sample_len);
    table->pcm86.base = pcm86_area;
    free(data);
#else
    (void)name;
    fail("this variant has no PCM86 driver");
#endif
}

// `loadadpcm` fills the ADPCM voice table and the board's ADPCM-B memory the
// way an external loader would after reading an .ADP file: the fixture holds
// the 16-byte header, the 256 four-byte entries, and the sample image.
static void do_loadadpcm(const char *name) {
#if board2 && adpcm && !ademu
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", trace.cases_dir, name);
    char *data;
    size_t len;
    if (!read_file(path, &data, &len)) fail("cannot open fixture %s", path);

    enum { adpcm_header = 16, adpcm_voices = 256, adpcm_entry = 4 };
    const size_t table_len = (size_t)adpcm_voices * adpcm_entry;
    if (len < adpcm_header + table_len) fail("%s: truncated ADPCM data file", name);
    if (memcmp(data, "ADPCM DATA\n", 11) != 0) fail("%s: not an ADPCM data file", name);
    uint32_t total = (uint32_t)(uint8_t)data[13] | ((uint32_t)(uint8_t)data[14] << 8) |
                     ((uint32_t)(uint8_t)data[15] << 16);
    size_t sample_len = len - (adpcm_header + table_len);
    if (total != sample_len) {
        fail("%s: header size %" PRIu32 " does not match the %zu data bytes", name,
             total, sample_len);
    }
    if (sample_len > sizeof(trace.board.adpcm_mem)) {
        fail("%s: sample data is too large", name);
    }

    union pmd_pcm_table *table = pmd_get_pcm_adr(trace.pmd);
    if (table == NULL) fail("%s: this variant has no ADPCM voice table", name);
    for (size_t i = 0; i < adpcm_voices; i++) {
        const uint8_t *entry = (const uint8_t *)data + adpcm_header + i * adpcm_entry;
        table->adpcm_data.pcmadrs[i].start =
            (uint16_t)(entry[0] | ((uint16_t)entry[1] << 8));
        table->adpcm_data.pcmadrs[i].stop =
            (uint16_t)(entry[2] | ((uint16_t)entry[3] << 8));
    }
    memset(trace.board.adpcm_mem, 0, sizeof(trace.board.adpcm_mem));
    memcpy(trace.board.adpcm_mem, data + adpcm_header + table_len, sample_len);
    free(data);
#else
    (void)name;
    fail("this variant has no ADPCM driver");
#endif
}

// `loadrom` fills the board's ADPCM-A memory with the rhythm ROM image.
static void do_loadrom(const char *name) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", trace.cases_dir, name);
    char *data;
    size_t len;
    if (!read_file(path, &data, &len)) fail("cannot open fixture %s", path);
    if (len > sizeof(trace.board.rhythm_rom)) fail("%s: rhythm ROM image is too large", name);
    memset(trace.board.rhythm_rom, 0, sizeof(trace.board.rhythm_rom));
    memcpy(trace.board.rhythm_rom, data, len);
    free(data);
}

static unsigned long parse_number(const char *text) {
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 0);
    if (end == text || *trim(end) != '\0') fail("bad number '%s'", text);
    return value;
}

// `rom OFFSET COUNT` records bytes from the board's ADPCM-A memory, which is
// host data no driver call returns. It is how a rhythm ROM fixture's contents
// are compared.
static void do_rom(const char *rest) {
    char offset_text[32];
    char count_text[16];
    if (sscanf(rest, "%31s %15s", offset_text, count_text) != 2) {
        fail("rom takes 'OFFSET COUNT'");
    }
    unsigned long offset = parse_number(offset_text);
    unsigned long count = parse_number(count_text);
    if (offset > sizeof(trace.board.rhythm_rom) ||
        count > sizeof(trace.board.rhythm_rom) - offset) {
        fail("rom range is outside the ADPCM-A memory");
    }
    fprintf(trace.log, "%" PRIu64 " ret rom %lx", trace.now, offset);
    for (unsigned long i = 0; i < count; i++) {
        fprintf(trace.log, " %02x", trace.board.rhythm_rom[offset + i]);
    }
    fputc('\n', trace.log);
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
    // The card raises its FIFO interrupt on the sample clock. One delivery per
    // timer interrupt is the model: the driver polls the flag from inside the
    // FM handler.
    if ((trace.board.fifo_control & 0xA0) == 0xA0) trace.board.fifo_pending = true;
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
            if (setjmp(exit_jmp) == 0) {
                pmd_resident_main(trace.pmd, (char *)rest);
                fprintf(trace.log, "%" PRIu64 " ret exit 00\n", trace.now);
            } else {
                fprintf(trace.log, "%" PRIu64 " ret exit %02x\n", trace.now, exit_status);
                return;
            }
        } else if (strcmp(verb, "change") == 0) {
            if (setjmp(exit_jmp) == 0) {
                enum pmd_change_result result = pmd_change_main(trace.pmd, (char *)rest);
                fprintf(trace.log, "%" PRIu64 " ret change %02x\n", trace.now,
                        (unsigned)result);
            } else {
                // A rejected option ends only this change; the resident keeps
                // running, so the script continues.
                fprintf(trace.log, "%" PRIu64 " ret exit %02x\n", trace.now, exit_status);
            }
        } else if (strcmp(verb, "load") == 0) {
            do_load(rest);
        } else if (strcmp(verb, "loadvoice") == 0) {
            do_loadvoice(rest);
        } else if (strcmp(verb, "loadeffect") == 0) {
            do_loadeffect(rest);
        } else if (strcmp(verb, "loadpcm") == 0) {
            do_loadpcm(rest);
        } else if (strcmp(verb, "loadadpcm") == 0) {
            do_loadadpcm(rest);
        } else if (strcmp(verb, "loadrom") == 0) {
            do_loadrom(rest);
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
        } else if (strcmp(verb, "musdat") == 0) {
            event_data_offset("musdat", pmd_get_musdat_adr(trace.pmd));
        } else if (strcmp(verb, "tondat") == 0) {
            event_data_offset("tondat", pmd_get_tondat_adr(trace.pmd));
        } else if (strcmp(verb, "efcdat") == 0) {
            event_data_offset("efcdat", pmd_get_efcdat_adr(trace.pmd));
        } else if (strcmp(verb, "pcm") == 0) {
            event_pointer("pcm", pmd_get_pcm_adr(trace.pmd));
        } else if (strcmp(verb, "workadr") == 0) {
            event_pointer("workadr", pmd_get_workadr(trace.pmd));
        } else if (strcmp(verb, "memo") == 0) {
            uint8_t *memo = pmd_get_memo(trace.pmd, (uint8_t)parse_number(rest));
            if (memo == NULL) {
                event_pointer("memo", NULL);
            } else {
                event_data_offset("memo", memo);
            }
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
        } else if (strcmp(verb, "rom") == 0) {
            do_rom(rest);
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
    } else if (strcmp(script.board, "VA") == 0) {
        board->kind = BOARD_VA;
        board->base = 0x044;
        board->opna = strcmp(script.chip, "YM2608") == 0;
        va_ports = true;
    } else if (strcmp(script.board, "none") == 0) {
        board->kind = BOARD_NONE;
        board->base = 0;
        board->opna = false;
    } else {
        fail("unsupported board '%s'", script.board);
    }
    set_ports(board);
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
    // Cases live in the directory named after their driver, so a mismatch is a
    // misplaced case rather than a reason to skip.
    if (strcmp(script.driver, trace_variant) != 0) {
        fail("case is for driver %s, but this host was built for %s", script.driver,
             trace_variant);
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
    // The 88VA is the one board that ships with either chip (PMDVA is the
    // YM2608 build, PMDVA1 the YM2203 one).
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
    pmd.pps = script.pps ? &trace_pps : NULL;
#if ppz
    pmd.ppz8 = script.ppz8 ? &trace_ppz8 : NULL;
#else
    pmd.ppz8 = NULL;
#endif
#if board2 && pcm
    pmd.p86 = script.p86 ? &trace_p86 : NULL;
#else
    pmd.p86 = NULL;
#endif
    pmd.user_data = &trace;
    trace.pmd = &pmd;
    trace.now = 0;

    pmd_init(&pmd);
    run_actions();

    if (fclose(trace.out) != 0 || fclose(trace.log) != 0) fail("cannot write output files");
    return EXIT_SUCCESS;
}
