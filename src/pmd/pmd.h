#ifndef PMDC_PMD_H
#define PMDC_PMD_H

#include <stdint.h>

#ifdef __GNUC__
// The standard C noreturn attribute is not a property of function types and
// can't be used on function pointers, so we can't use it here.
#define PMDC_NORETURN __attribute__((noreturn))
// TODO: PMDC_UNUSED is defined solely for the benefit of implementing the
// plan phase by phase, so things used only in later phases aren't flagged
// by the compiler. It will be removed at the end of the plan.
#define PMDC_UNUSED __attribute__((unused))
#else
#define PMDC_NORETURN
#define PMDC_UNUSED
#endif

struct pmd;

// The vtable for system functions.
// Reference for DOS INT 21 functions: https://www.stanislavs.org/helppc/int_21.html
//
// All system interactions are done via these functions, which mostly correspond
// to DOS system functions. An implementation of this vtable using the C
// standard library I/O functionality is available in pmd_stdio.c, but other
// implementations are possible if desired.
//
// To increase the flexibility of possible system function implementations, all
// vtable functions accept a user_data parameter, which is passed as the
// user_data set in the pmd "global" structure.
struct pmd_sys {
    // DOS INT 21,2
    void (*putc)(char c, void *user_data);
    // DOS INT 21,9
    // But mes is expected to be null-terminated, not $-terminated.
    void (*print)(const char *mes, void *user_data);
    // DOS INT 21,4C
    PMDC_NORETURN void (*exit)(int status, void *user_data);
    // Takes a null-terminated environment variable name and returns its
    // null-terminated value, or NULL. The returned pointer must be stable
    // across multiple calls to this function.
    char *(*getenv)(const char *name, void *user_data);
};

#define PMD_KEY_SHIFT 0x01
#define PMD_KEY_CAPS  0x02
#define PMD_KEY_KANA  0x04
#define PMD_KEY_GRPH  0x08
#define PMD_KEY_CTRL  0x10
#define PMD_KEY_ESC   0x20

// The vtable for hardware interaction functions.
struct pmd_hw {
    // Raw port I/O. The driver accesses the configured FM chip ports
    // (fm1_port1/2, fm2_port1/2 and the derived fm_port1/2), including the PSG
    // registers 0Eh/0Fh that INT 60H function 16h reads the joystick through,
    // the 0AAh probe value that port_check writes and reads back through the FM
    // data port, and the OPNA ID port 0A460h, which it reads and switches off
    // and on. An implementation must model the FM chip and the OPNA ID port;
    // other control ports may be recorded or ignored.
    uint8_t (*inb)(uint16_t port, void *user_data);
    void (*outb)(uint16_t port, uint8_t v, void *user_data);

    // Installs the PMD FM timer interrupt handler. The host calls handler(pmd)
    // whenever the FM interrupt fires; the host owns the interrupt source and
    // the exclusion that the original obtained through cli and the PIC.
    void (*install_fm_timer_handler)(void (*handler)(struct pmd *), void *user_data);
    // Synchronously stops callbacks; no callback may be active after return,
    // and it is harmless when no handler is installed.
    void (*remove_fm_timer_handler)(void *user_data);

    // Optional normalized PMD_KEY_* state. A NULL hook means no keys pressed.
    // The driver translates these bits to the original PC-98 or VA
    // representations instead of exposing BIOS addresses or VA ports.
    uint8_t (*read_key_state)(void *user_data);
};

// The vtable for PPSDRV resident functionality (INT 64h).
struct pmd_drv_pps {
    void (*TODO)(void); // TODO: figure out what functions need to be exposed here
};

// The vtable for PPZ8 resident functionality (INT 7fh).
struct pmd_drv_ppz8 {
    void (*TODO)(void); // TODO: figure out what functions need to be exposed here
};

// The vtable for P86DRV resident functionality (INT 65h).
struct pmd_drv_p86 {
    void (*TODO)(void); // TODO: figure out what functions need to be exposed here
};

// struct pmd contains the global state of the PMD process. The original
// assembly implementation simply used global variables, but this translation
// moves all global state into this struct to improve its flexibility of use.
struct pmd {
    // :7975
    uint16_t fm_port1;
    uint16_t fm_port2;

    // :8011
    // 0 = YM2203
    // 1 = YM2608
    uint8_t ongen;

    // :8031
    uint8_t *mmlbuf;
    uint8_t *tondat;
    uint8_t *efcdat;

    // :8034
    uint16_t fm1_port1;
    uint16_t fm1_port2;
    uint16_t fm2_port1;
    uint16_t fm2_port2;

    // :8337
    uint8_t dataarea[1 + 40 * 1024];
    // :10567
    // Mutable copy of the original's resident mes_int/int_num/port_num run:
    // the interrupt number (:8937) and the FM port (:8860) are written into it
    // before it is printed. Sized for the longest variant's string, including
    // its null terminator.
    char mes_int[17];
    // :10578
    // Mutable copy of the original's mes_ongen1: the ADPCM RAM check overwrites
    // its "2608" with "F288" to report a YMF288 (ADRAMCHK.ASM:62). Sized for the
    // longest variant's string, including its null terminator.
    char mes_ongen1[7];
    // :10853
    int8_t port_sel;

    const struct pmd_sys *sys;
    const struct pmd_hw *hw;
    // Checks for PPSDRV resident (e.g. :6564, :9091) check whether this is non-null.
    const struct pmd_drv_pps *pps;
    // Checks for PPZ8 resident (e.g. :9118) check whether this is non-null.
    const struct pmd_drv_ppz8 *ppz8;
    // Checks for P86DRV resident (e.g. PCMDRV86.ASM:922) check whether this is non-null.
    const struct pmd_drv_p86 *p86;
    void *user_data;
};

// Initializes the global state of the program.
void pmd_init(struct pmd *pmd);
// Initializes the program as resident.
void pmd_resident_main(struct pmd *pmd, char *cmdline);
// Handles updated options for an already resident program.
void pmd_change_main(struct pmd *pmd, char *cmdline);
// TODO: figure out what parameters these need to receive and whether they return anything.
// :6366
void pmd_mstart_f(struct pmd *pmd);
void pmd_mstop_f(struct pmd *pmd);
// etc.

#endif
