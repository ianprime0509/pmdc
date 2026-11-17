#ifndef PMDC_PMD_YMFM_H
#define PMDC_PMD_YMFM_H

// The pmd_hw implementation on top of YMFM. It models one OPN-family chip and
// the board it sits on, so the driver can probe and drive real emulated
// hardware instead of a scripted backend.
//
// This is a host backend: the core translation in pmd.c does not depend on it,
// and pmd_stdio stays independent of it.

#include "pmd.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum pmd_ymfm_chip {
    PMD_YMFM_YM2203,
    PMD_YMFM_YM2608,
    PMD_YMFM_YMF288,
};

// The wiring between the chip and the board. It decides which ports belong to
// the chip and which board-control ports the driver's probes touch; it does not
// tell the driver which chip to expect.
enum pmd_ymfm_board {
    // 088h-style PC-98 board: the chip's four ports are base+0/2/4/6, with the
    // NEC OPNA ID port and the mute control port. A chip at any other probed
    // base port is absent.
    PMD_YMFM_BOARD_PC98,
    // The 86B: the chip is at 188h, but its extended ports (base+4/6) answer
    // only once the driver has enabled the OPNA through the ID port. Before
    // that, and during check_86b's probe, they read as an open bus. The other
    // board-control ports are the same as PC98.
    PMD_YMFM_BOARD_PC98_86B,
    // The VA board: 44h..47h, one port per chip register offset.
    PMD_YMFM_BOARD_VA,
};

struct pmd_ymfm;

// The SSG mix level PMD-style players use. The chip's own SSG output level is
// much higher relative to FM than a PMD mix wants: PMDWin mixes it at
// SetVolumePSG(-18), and its gain formula (65536 * 10^(db / 40)) works out to
// 0.3548 of the chip's output.
#define PMD_YMFM_SSG_GAIN_DEFAULT 0.355

struct pmd_ymfm_config {
    enum pmd_ymfm_chip chip;
    enum pmd_ymfm_board board;
    // The board's chip address port, e.g. 088h.
    uint16_t base_port;
    // Bits 6-7 of the value the board returns when the driver reads register
    // 0Eh. On the PC-98 that register selects the interrupt level; the value
    // must agree with the IRQ the host delivers timers on.
    uint8_t irq_bits;
    // The chip's input clock in Hz. 0 selects the chip's PC-98 clock.
    uint32_t input_clock;
    // Mix level of the board's AC-coupled SSG output, before it joins the FM
    // outputs. 1.0 is the chip's own level, 0 silences the SSG, and
    // PMD_YMFM_SSG_GAIN_DEFAULT is what a PMD player mixes it at.
    double ssg_gain;
    // The driver whose FM timer handler the backend calls.
    struct pmd *pmd;
};

// Fills in the chip, board, base port, and SSG mix level the current PMD
// variant was written for. The caller still has to set config->pmd.
void pmd_ymfm_default_config(struct pmd_ymfm_config *config, struct pmd *pmd);

// Returns NULL when `config` is NULL, names an unknown chip type, or cannot be
// allocated.
struct pmd_ymfm *pmd_ymfm_create(const struct pmd_ymfm_config *config);
void pmd_ymfm_destroy(struct pmd_ymfm *ymfm);

// The pmd_hw vtable. `pmd->user_data` must be the struct pmd_ymfm pointer,
// because the FM timer handler is delivered through the same opaque pointer.
// read_key_state is NULL: the backend has no input device.
extern const struct pmd_hw pmd_hw_ymfm;

// Raw port access, the same operations the vtable performs. Unknown ports read
// as an open bus (0FFh) and ignore writes, which is what the driver's port
// scan expects. Each access advances the emulated clock by one sample period
// and clocks the chip, so the driver's BUSY handshake completes without host
// pacing and writes it issues back to back still reach the chip as separate
// clocks. The audio those accesses produce is queued for pmd_ymfm_generate.
uint8_t pmd_ymfm_inb(struct pmd_ymfm *ymfm, uint16_t port);
void pmd_ymfm_outb(struct pmd_ymfm *ymfm, uint16_t port, uint8_t value);

// Generates `samples` samples at the chip's sample rate, delivering timer
// interrupts to the installed handler as they expire. Interleaved stereo output
// is written to `out` when it is not NULL. Samples the chip produced while the
// driver was programming it are returned first. This is the only point at which the
// handler runs: the host calls it between driver calls and so provides the
// exclusion that the original got from cli and the PIC.
void pmd_ymfm_generate(struct pmd_ymfm *ymfm, uint32_t samples, int32_t *out);

// The chip's sample rate for the configured clock, i.e. the rate
// pmd_ymfm_generate advances at.
uint32_t pmd_ymfm_sample_rate(const struct pmd_ymfm *ymfm);

// The ADPCM-A sample store (the rhythm ROM). The chip reads it through
// ymfm_external_read; addresses past the loaded image read as zero.
void pmd_ymfm_load_adpcm_a(struct pmd_ymfm *ymfm, const uint8_t *data,
                           size_t size);
// Loads exactly `path`. When it cannot be read, the store stays silent and the
// backend warns through pmd_sys once per instance.
void pmd_ymfm_load_adpcm_a_file(struct pmd_ymfm *ymfm, const char *path);

// One byte of the ADPCM-A store, for hosts that inspect what was loaded.
uint8_t pmd_ymfm_adpcm_a_byte(const struct pmd_ymfm *ymfm, uint32_t address);

#ifdef __cplusplus
}
#endif

#endif
