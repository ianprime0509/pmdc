#include "pmd.h"

#include <assert.h>
#include <string.h>

// :21
#ifndef va
#define va 0
#endif
#ifndef board2
#define board2 0
#endif
#ifndef adpcm
#define adpcm 0
#endif
#ifndef ademu
#define ademu 0
#endif
#ifndef pcm
#define pcm 0
#endif
#ifndef ppz
#define ppz 0
#endif
#ifndef sync
#define sync 0
#endif
#ifndef vsync
#define vsync 0
#endif

#if lang == 256 * 'e' + 'n'
#include "strings_en.h"
#else
#include "strings_jp.h"
#endif

// Character classification helpers
//
// These functions work slightly differently from their ctype.h counterparts,
// since they must work correctly with Shift JIS and replicate quirks within the
// original code (such as DEL not being considered a control character).

static bool is_cntrl(char c) { return c >= 0 && c < ' '; }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_lower(char c) { return c >= 'a' && c <= 'z'; }
static char to_upper(char c) { return is_lower(c) ? c - 'a' + 'A' : c; }

// Data read/write helpers

static uint8_t lodsb(uint8_t **src) { return *(*src)++; }

static uint16_t lodsw(uint8_t **src) {
    uint16_t value = (uint16_t)((*src)[0] | ((*src)[1] << 8));
    *src += 2;
    return value;
}

static uint8_t inb(struct pmd *pmd, uint16_t port) {
    return pmd->hw->inb(port, pmd->user_data);
}

static void outb(struct pmd *pmd, uint16_t port, uint8_t value) {
    pmd->hw->outb(port, value, pmd->user_data);
}

// Writes one character (DOS INT 21h/AH=2 in the original).
static void print_char(struct pmd *pmd, char c) {
    pmd->sys->putc(c, pmd->user_data);
}

// Helpers for repeated logic

// True when `part` is the selected FM3 part (always the front chip).
static bool fm3_active(struct pmd *pmd, struct pmd_qq *part) {
    (void)part;
    if (pmd->partb != 3) return false;
#if board2
    return pmd->fmsel == 0;
#else
    return part != &pmd->part_e;
#endif
}

// :12
enum {
    // Module data buffer size (KiB).
    mdata_def = 16,
    // Voice data buffer size (KiB).
    voice_def = 8,
    // Effect data buffer size (KiB).
    effect_def = 4,
    key_def = 1,
};

// :47
#if va || board2
enum {
    fmvd_init = 0,
};
#else
// The PC-98 uses a lower FM volume than the 88.
enum {
    fmvd_init = 16,
};
#endif

#if board2 && pcm

// TUNEDATA.INC:9
// The default 16.54kHz PCM86 frequency table (96 notes).
static const uint8_t pcm_tune_data_default[288] = {
    0x00, 0xb7, 0x2a, 0x00, 0x41, 0x2d, 0x00, 0xf2, 0x2f, 0x00, 0xcb, 0x32,
    0x00, 0xd1, 0x35, 0x00, 0x04, 0x39, 0x00, 0x68, 0x3c, 0x00, 0xff, 0x3f,
    0x00, 0xce, 0x43, 0x00, 0xd6, 0x47, 0x00, 0x1b, 0x4c, 0x00, 0xa2, 0x50,
    0x00, 0x6e, 0x55, 0x00, 0x82, 0x5a, 0x00, 0xe4, 0x5f, 0x00, 0x97, 0x65,
    0x00, 0xa2, 0x6b, 0x00, 0x09, 0x72, 0x00, 0xd0, 0x78, 0x00, 0xff, 0x7f,
    0x00, 0x9c, 0x87, 0x00, 0xac, 0x8f, 0x00, 0x37, 0x98, 0x00, 0x45, 0xa1,
    0x00, 0xdc, 0xaa, 0x00, 0x04, 0xb5, 0x00, 0xc8, 0xbf, 0x00, 0x2f, 0xcb,
    0x00, 0x44, 0xd7, 0x00, 0x12, 0xe4, 0x00, 0xa1, 0xf1, 0x01, 0x00, 0x00,
    0x20, 0x6b, 0xcb, 0x20, 0x83, 0xd7, 0x20, 0x54, 0xe4, 0x20, 0xe7, 0xf1,
    0x40, 0xdc, 0xaa, 0x40, 0x04, 0xb5, 0x40, 0xc8, 0xbf, 0x40, 0x2f, 0xcb,
    0x40, 0x44, 0xd7, 0x40, 0x12, 0xe4, 0x40, 0xa1, 0xf1, 0x41, 0x00, 0x00,
    0x60, 0x6b, 0xcb, 0x60, 0x83, 0xd7, 0x60, 0x54, 0xe4, 0x60, 0xe7, 0xf1,
    0x80, 0xdc, 0xaa, 0x80, 0x04, 0xb5, 0x80, 0xc8, 0xbf, 0x80, 0x2f, 0xcb,
    0x80, 0x44, 0xd7, 0x80, 0x12, 0xe4, 0x80, 0xa1, 0xf1, 0x81, 0x00, 0x00,
    0xa0, 0x6b, 0xcb, 0xa0, 0x83, 0xd7, 0xa0, 0x54, 0xe4, 0xa0, 0xe7, 0xf1,
    0xc0, 0xdc, 0xaa, 0xc0, 0x04, 0xb5, 0xc0, 0xc8, 0xbf, 0xc0, 0x2f, 0xcb,
    0xc0, 0x44, 0xd7, 0xc0, 0x12, 0xe4, 0xc0, 0xa1, 0xf1, 0xc1, 0x00, 0x00,
    0xe0, 0x6b, 0xcb, 0xe0, 0x83, 0xd7, 0xe0, 0x54, 0xe4, 0xe0, 0xe7, 0xf1,
    0xe1, 0x4a, 0x00, 0xe1, 0x87, 0x0f, 0xe1, 0xac, 0x1f, 0xe1, 0xc7, 0x30,
    0xe1, 0xe7, 0x42, 0xe1, 0x1c, 0x56, 0xe1, 0x72, 0x6a, 0xe1, 0x00, 0x80,
    0xe1, 0xd6, 0x96, 0xe1, 0x06, 0xaf, 0xe1, 0xa8, 0xc8, 0xe1, 0xcf, 0xe3,
    0xe2, 0x94, 0x00, 0xe2, 0x0e, 0x1f, 0xe2, 0x59, 0x3f, 0xe2, 0x8f, 0x61,
    0xe2, 0xce, 0x85, 0xe2, 0x38, 0xac, 0xe2, 0xe5, 0xd4, 0xe3, 0x00, 0x00,
    0xe3, 0xac, 0x2d, 0xe3, 0x0d, 0x5e, 0xe3, 0x50, 0x91, 0xe3, 0x9e, 0xc7,
};

#endif

// :6360
// The INT 60h reentrancy classes: bit 0 rejects the call while Timer B's
// handler owns the state, bit 1 while Timer A's does, and bit 2 while another
// INT 60h call is active. The original's table ends at 21h, so its 22h lookup
// reads one byte past the end; the port treats that entry as unrestricted.
static const uint8_t reint_chk[0x22 + 1] = {
    4, 4, 0, 6, 6, 0, 0, 0, 0, 0, 0, 0, 7, 7, 0, 5,
    0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 5, 5, 7, 0, 7, 0,
    0, 0,
    0,
};

// :7180
// The part table: part number, partb, and sound source per part work area.
// maskon_all/maskoff_all walk it in work area order, while part_mask and
// part_on index it by part number; the two orders agree for the entries those
// paths reach. Source: 0 FM1, 1 FM2, 2 SSG, 3 PCM or rhythm, 4 rhythm, 5 PPZ8.
static const uint8_t part_table[][3] = {
#if board2 && ppz
    {0x00, 1, 0}, // A
    {0x01, 2, 0}, // B
    {0x02, 3, 0}, // C
    {0x03, 1, 1}, // D
    {0x04, 2, 1}, // E
    {0x05, 3, 1}, // F
    {0x06, 1, 2}, // G
    {0x07, 2, 2}, // H
    {0x08, 3, 2}, // I
    {0x09, 1, 3}, // J
    {0x0a, 3, 4}, // K
    {0x0b, 3, 0}, // c2
    {0x0c, 3, 0}, // c3
    {0x0d, 3, 0}, // c4
    {0xff, 0, 0xff}, // Rhythm
    {0x16, 3, 1}, // Effect
    {0x0e, 0, 5}, // PPZ1
    {0x0f, 1, 5}, // PPZ2
    {0x10, 2, 5}, // PPZ3
    {0x11, 3, 5}, // PPZ4
    {0x12, 4, 5}, // PPZ5
    {0x13, 5, 5}, // PPZ6
    {0x14, 6, 5}, // PPZ7
    {0x15, 7, 5}, // PPZ8
#elif board2
    {0x00, 1, 0}, // A
    {0x01, 2, 0}, // B
    {0x02, 3, 0}, // C
    {0x03, 1, 1}, // D
    {0x04, 2, 1}, // E
    {0x05, 3, 1}, // F
    {0x06, 1, 2}, // G
    {0x07, 2, 2}, // H
    {0x08, 3, 2}, // I
    {0x09, 1, 3}, // J
    {0x0a, 3, 4}, // K
    {0x0b, 3, 0}, // c2
    {0x0c, 3, 0}, // c3
    {0x0d, 3, 0}, // c4
    {0xff, 0, 0xff}, // Rhythm
    {0x0e, 3, 1}, // Effect
#else
    {0x00, 1, 0}, // A
    {0x01, 2, 0}, // B
    {0x02, 3, 0}, // C
    {0x03, 3, 0}, // c2
    {0x04, 3, 0}, // c3
    {0x05, 3, 0}, // c4
    {0x06, 1, 2}, // G
    {0x07, 2, 2}, // H
    {0x08, 3, 2}, // I
    {0x09, 1, 3}, // J
    {0x0a, 3, 4}, // K
    {0x03, 3, 0}, // c2
    {0x04, 3, 0}, // c3
    {0x05, 3, 0}, // c4
    {0xff, 0, 0xff}, // Rhythm
    {0x0b, 3, 0}, // Effect
#endif
};

// :7925
// FM f-number table for one octave (C..B).
static const uint16_t fnum_data[12] = {
    0x026a, // C
    0x028f, // D-
    0x02b6, // D
    0x02df, // E-
    0x030b, // E
    0x0339, // F
    0x036a, // G-
    0x039e, // G
    0x03d5, // A-
    0x0410, // A
    0x044e, // B-
    0x048f, // B
};

// :7940
// SSG tone table for one octave (C..B).
static const uint16_t psg_tune_data[12] = {
    0x0ee8, // C
    0x0e12, // D-
    0x0d48, // D
    0x0c89, // E-
    0x0bd5, // E
    0x0b2b, // F
    0x0a8a, // G-
    0x09f3, // G
    0x0964, // A-
    0x08dd, // A
    0x085e, // B-
    0x07e6, // B
};

// :7958
// The carrier masks for the 8 FM algorithms.
static const uint8_t carrier_table[16] = {
    0x80, 0x80, 0x80, 0x80,
    0xa0, 0xe0, 0xe0, 0xf0,

    0xee, 0xee, 0xee, 0xee,
    0xcc, 0x88, 0x88, 0x00,
};

// :7968
// Translated from EFFECT.INC (bigeffec = 0). Each entry is a priority
// byte followed by the 16-bit offset of its data from the start of the
// table.
static const uint8_t efftbl[] = {
    0x01, 0x78, 0x00, 0x01, 0x8f, 0x00, 0x01, 0x9b, 0x00, 0x01, 0xb2, 0x00,
    0x01, 0xc9, 0x00, 0x01, 0xe0, 0x00, 0x01, 0xec, 0x00, 0x01, 0xf8, 0x00,
    0x01, 0x04, 0x01, 0x01, 0x10, 0x01, 0x01, 0x1c, 0x01, 0x02, 0x28, 0x01,
    0x02, 0x3f, 0x01, 0x02, 0x56, 0x01, 0x02, 0x6d, 0x01, 0x02, 0x84, 0x01,
    0x02, 0x9b, 0x01, 0x02, 0xa7, 0x01, 0x02, 0xb3, 0x01, 0x02, 0xca, 0x01,
    0x02, 0xe1, 0x01, 0x02, 0xf8, 0x01, 0x02, 0x30, 0x02, 0x02, 0x3c, 0x02,
    0x02, 0x53, 0x02, 0x02, 0x6a, 0x02, 0x02, 0x76, 0x02, 0x02, 0x8d, 0x02,
    0x02, 0x99, 0x02, 0x02, 0xb0, 0x02, 0x02, 0xbc, 0x02, 0x02, 0xc8, 0x02,
    0x02, 0xdf, 0x02, 0x02, 0xf6, 0x02, 0x02, 0x0d, 0x03, 0x02, 0x3a, 0x03,
    0x02, 0x46, 0x03, 0x02, 0x5d, 0x03, 0x02, 0x74, 0x03, 0x02, 0x80, 0x03,
    0x01, 0xdc, 0x05, 0x1f, 0x36, 0x0f, 0x00, 0x00, 0x00, 0x7f, 0x00, 0x08,
    0xa4, 0x06, 0x00, 0x3e, 0x10, 0xb0, 0x04, 0x00, 0x7f, 0x00, 0xff, 0x0e,
    0x90, 0x01, 0x07, 0x36, 0x10, 0xb8, 0x0b, 0x00, 0x5d, 0xf2, 0xff, 0x02,
    0xbc, 0x02, 0x00, 0x36, 0x0f, 0x00, 0x00, 0x00, 0x64, 0x00, 0x0e, 0x84,
    0x03, 0x00, 0x36, 0x10, 0xc4, 0x09, 0x00, 0x64, 0x00, 0xff, 0x02, 0xf4,
    0x01, 0x05, 0x36, 0x0f, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x0e, 0x6c, 0x02,
    0x00, 0x36, 0x10, 0xc4, 0x09, 0x00, 0x3c, 0x00, 0xff, 0x02, 0x2c, 0x01,
    0x00, 0x36, 0x0f, 0x00, 0x00, 0x00, 0x32, 0x00, 0x0e, 0x90, 0x01, 0x00,
    0x36, 0x10, 0xc4, 0x09, 0x00, 0x32, 0x00, 0xff, 0x02, 0x37, 0x00, 0x00,
    0x3e, 0x10, 0x2c, 0x01, 0x00, 0x64, 0x00, 0xff, 0x10, 0x00, 0x00, 0x0f,
    0x37, 0x10, 0xb8, 0x0b, 0x00, 0x00, 0xf1, 0xff, 0x06, 0x27, 0x00, 0x00,
    0x36, 0x10, 0xf4, 0x01, 0x00, 0x00, 0x00, 0xff, 0x20, 0x27, 0x00, 0x00,
    0x36, 0x10, 0x88, 0x13, 0x00, 0x00, 0x00, 0xff, 0x1f, 0x28, 0x00, 0x1f,
    0x36, 0x10, 0x88, 0x13, 0x00, 0x00, 0xf1, 0xff, 0x1f, 0x1e, 0x00, 0x00,
    0x36, 0x10, 0x88, 0x13, 0x00, 0x00, 0x00, 0xff, 0x03, 0xdd, 0x01, 0x0f,
    0x37, 0x10, 0xe8, 0x03, 0x00, 0x00, 0x71, 0x02, 0xdd, 0x01, 0x00, 0x37,
    0x10, 0xe8, 0x03, 0x00, 0x00, 0x00, 0xff, 0x01, 0x2c, 0x01, 0x00, 0x3e,
    0x10, 0x2c, 0x01, 0x0d, 0x00, 0x00, 0x06, 0x2c, 0x01, 0x00, 0x3e, 0x10,
    0x10, 0x27, 0x00, 0x50, 0x00, 0xff, 0x04, 0xdd, 0x01, 0x00, 0x37, 0x0e,
    0x10, 0x27, 0x00, 0x00, 0x51, 0x04, 0xdd, 0x01, 0x0a, 0x37, 0x10, 0xd0,
    0x07, 0x00, 0x00, 0xf1, 0xff, 0x03, 0xdd, 0x01, 0x00, 0x37, 0x10, 0xf4,
    0x01, 0x0d, 0x00, 0x00, 0x08, 0xdd, 0x01, 0x0f, 0x37, 0x10, 0xd0, 0x07,
    0x00, 0x00, 0x00, 0xff, 0x03, 0xdd, 0x01, 0x0a, 0x37, 0x10, 0x64, 0x00,
    0x0d, 0x00, 0x00, 0x10, 0xdd, 0x01, 0x05, 0x37, 0x10, 0x10, 0x27, 0x00,
    0x00, 0x00, 0xff, 0x02, 0x90, 0x01, 0x00, 0x3e, 0x10, 0xf4, 0x01, 0x00,
    0x00, 0x00, 0xff, 0x04, 0xdd, 0x01, 0x0f, 0x37, 0x10, 0xe8, 0x03, 0x00,
    0x00, 0x00, 0xff, 0x02, 0xdd, 0x01, 0x1f, 0x37, 0x0f, 0x10, 0x27, 0x00,
    0x00, 0x00, 0x0c, 0xdd, 0x01, 0x00, 0x37, 0x10, 0x88, 0x13, 0x00, 0x00,
    0x11, 0xff, 0x02, 0x90, 0x01, 0x00, 0x3e, 0x10, 0xe8, 0x03, 0x00, 0x00,
    0x00, 0x02, 0xc8, 0x00, 0x00, 0x3e, 0x10, 0xe8, 0x03, 0x00, 0x00, 0x00,
    0xff, 0x04, 0x90, 0x01, 0x00, 0x3e, 0x10, 0xd0, 0x07, 0x00, 0x00, 0x00,
    0x08, 0xc8, 0x00, 0x00, 0x3e, 0x10, 0xb8, 0x0b, 0x00, 0x00, 0x00, 0xff,
    0x03, 0x90, 0x01, 0x00, 0x3e, 0x10, 0xd0, 0x07, 0x00, 0x00, 0x00, 0x03,
    0x64, 0x00, 0x00, 0x3e, 0x10, 0xd0, 0x07, 0x00, 0x00, 0x00, 0x03, 0xc8,
    0x00, 0x00, 0x3e, 0x10, 0xd0, 0x07, 0x00, 0x00, 0x00, 0x03, 0x90, 0x01,
    0x00, 0x3e, 0x10, 0xd0, 0x07, 0x00, 0x00, 0x00, 0x08, 0x64, 0x00, 0x00,
    0x3e, 0x10, 0xb8, 0x0b, 0x00, 0x00, 0x00, 0xff, 0x10, 0xd0, 0x07, 0x00,
    0x3e, 0x0f, 0x10, 0x27, 0x00, 0x00, 0x00, 0xff, 0x04, 0xdd, 0x01, 0x1f,
    0x37, 0x10, 0x88, 0x13, 0x00, 0x00, 0x00, 0x08, 0xdd, 0x01, 0x1f, 0x36,
    0x10, 0xb8, 0x0b, 0x00, 0x7f, 0xf1, 0xff, 0x04, 0xdd, 0x01, 0x19, 0x37,
    0x10, 0xd0, 0x07, 0x00, 0x00, 0x00, 0x20, 0xdd, 0x01, 0x14, 0x37, 0x10,
    0x70, 0x17, 0x00, 0x00, 0x13, 0xff, 0x06, 0xc8, 0x00, 0x00, 0x36, 0x10,
    0x88, 0x13, 0x00, 0x14, 0x00, 0xff, 0x04, 0x28, 0x00, 0x14, 0x36, 0x10,
    0x10, 0x27, 0x00, 0x14, 0x00, 0x10, 0x14, 0x00, 0x05, 0x36, 0x10, 0x88,
    0x13, 0x00, 0x00, 0x00, 0xff, 0x06, 0x58, 0x02, 0x00, 0x3e, 0x10, 0xe8,
    0x03, 0x00, 0x00, 0x00, 0xff, 0x04, 0xe8, 0x03, 0x00, 0x3e, 0x10, 0x10,
    0x27, 0x00, 0x7f, 0x00, 0x10, 0xdd, 0x01, 0x00, 0x36, 0x10, 0x10, 0x27,
    0x00, 0x40, 0x00, 0xff, 0x04, 0xe8, 0x03, 0x1f, 0x36, 0x0f, 0x10, 0x27,
    0x00, 0x00, 0x00, 0xff, 0x04, 0xff, 0x0f, 0x1f, 0x36, 0x0f, 0x10, 0x27,
    0x00, 0x00, 0x00, 0xff, 0x04, 0xdd, 0x01, 0x00, 0x3e, 0x10, 0xe8, 0x03,
    0x00, 0xce, 0x00, 0x10, 0xf2, 0x00, 0x00, 0x3e, 0x10, 0x70, 0x17, 0x00,
    0xf8, 0x00, 0xff, 0x04, 0x64, 0x00, 0x00, 0x3e, 0x10, 0xf4, 0x01, 0x00,
    0x00, 0x00, 0x04, 0x0a, 0x00, 0x00, 0x36, 0x10, 0xe8, 0x03, 0x00, 0x00,
    0x00, 0xff, 0x08, 0xdd, 0x01, 0x05, 0x37, 0x10, 0xf4, 0x01, 0x0d, 0x00,
    0x00, 0x18, 0x1e, 0x00, 0x00, 0x36, 0x10, 0x10, 0x27, 0x00, 0x00, 0x00,
    0xff, 0x04, 0x2c, 0x01, 0x00, 0x3e, 0x10, 0x88, 0x13, 0x00, 0x00, 0x00,
    0x04, 0xb4, 0x00, 0x00, 0x3e, 0x10, 0x88, 0x13, 0x00, 0x00, 0x00, 0x04,
    0xc8, 0x00, 0x00, 0x3e, 0x10, 0x88, 0x13, 0x00, 0x00, 0x00, 0x18, 0x96,
    0x00, 0x00, 0x3e, 0x10, 0x88, 0x13, 0x00, 0x00, 0x00, 0xff, 0x03, 0xee,
    0x00, 0x00, 0x3e, 0x0e, 0xd0, 0x07, 0x00, 0x00, 0x00, 0xff, 0x04, 0xc8,
    0x00, 0x00, 0x3e, 0x10, 0x88, 0x13, 0x00, 0x00, 0x00, 0x10, 0x64, 0x00,
    0x00, 0x3e, 0x10, 0x88, 0x13, 0x00, 0x00, 0x00, 0xff, 0x10, 0x00, 0x00,
    0x00, 0x36, 0x10, 0xf4, 0x01, 0x0d, 0x01, 0x11, 0x10, 0x10, 0x00, 0x10,
    0x36, 0x10, 0x7c, 0x15, 0x00, 0x01, 0x11, 0xff, 0x01, 0xc8, 0x00, 0x00,
    0x3e, 0x0e, 0xe8, 0x03, 0x00, 0x00, 0x00, 0xff, 0x02, 0xc8, 0x00, 0x00,
    0x3e, 0x10, 0x20, 0x03, 0x00, 0x00, 0x00, 0x02, 0x64, 0x00, 0x00, 0x3e,
    0x10, 0x20, 0x03, 0x00, 0x00, 0x00, 0x02, 0x32, 0x00, 0x00, 0x3e, 0x10,
    0x20, 0x03, 0x00, 0x00, 0x00, 0x02, 0x19, 0x00, 0x00, 0x3e, 0x10, 0x20,
    0x03, 0x00, 0x00, 0x00, 0xff,
};


#if board2
// :8014
// Rhythm data: PT, pan/volume, and the keyon bit for each rhythm instrument.
static const uint8_t rhydat[11][3] = {
    {0x18, 0xdf, 0x01}, // bass drum
    {0x19, 0xdf, 0x02}, // snare drum
    {0x1c, 0x5f, 0x10}, // low tom
    {0x1c, 0xdf, 0x10}, // middle tom
    {0x1c, 0x9f, 0x10}, // high tom
    {0x1d, 0xd3, 0x20}, // rim shot
    {0x19, 0xdf, 0x02}, // clap
    {0x1b, 0x9c, 0x88}, // closed hi-hat
    {0x1a, 0x9d, 0x04}, // open hi-hat
    {0x1a, 0xdf, 0x04}, // cymbal
    {0x1a, 0x5e, 0x04}, // ride cymbal
};
#endif

// :8263
enum {
#if ppz
    max_part1 = 14 + 8,
#elif board2
    max_part1 = 14,
#else
    max_part1 = 11,
#endif
    // :8266
    max_part2 = 11,
};

// struct pmd has a fixed layout, so its part array must cover max_part1 for
// every variant. Equality holds for the largest (PPZ8) one.
static_assert(PMD_MAX_PARTS >= max_part1, "PMD_MAX_PARTS must cover max_part1");

// :8275
enum {
    fm = 0,
    fm2 = 1,
    psg = 2,
    rhythm = 3,
};

// :8318
// Indices into pmd->parts. The part work areas are one contiguous array
// because the original advances from part1 through adjacent areas.
enum {
    part1 = 0,
    part2,
    part3,
#if board2
    part4,
    part5,
    part6,
#else
    part3b,
    part3c,
    part3d,
#endif
    part7,
    part8,
    part9,
    part10,
    part11,
#if board2
    part3b,
    part3c,
    part3d,
#endif
#if ppz
    part10a,
    part10b,
    part10c,
    part10d,
    part10e,
    part10f,
    part10g,
    part10h,
#endif
};

// :8377
// The empty module image: the version/system byte, 12 empty part offsets, and
// the end-of-data marker.
static const uint8_t empty_dataarea[] = {
    0x00,
    0x18, 0x00, 0x18, 0x00, 0x18, 0x00, 0x18, 0x00,
    0x18, 0x00, 0x18, 0x00, 0x18, 0x00, 0x18, 0x00,
    0x18, 0x00, 0x18, 0x00, 0x18, 0x00, 0x18, 0x00,
    0x80,
};

#if board2 && pcm
// :10844
// Translated from HZDATA.INC: /HZ frequency tables 0-7 for the PCM86.
static const uint8_t hzdata[8][288] = {
    {
        0x00, 0xb7, 0x2a, 0x00, 0x41, 0x2d, 0x00, 0xf2, 0x2f, 0x00, 0xcb, 0x32,
        0x00, 0xd1, 0x35, 0x00, 0x04, 0x39, 0x00, 0x68, 0x3c, 0x00, 0xff, 0x3f,
        0x00, 0xce, 0x43, 0x00, 0xd6, 0x47, 0x00, 0x1b, 0x4c, 0x00, 0xa2, 0x50,
        0x00, 0x6e, 0x55, 0x00, 0x82, 0x5a, 0x00, 0xe4, 0x5f, 0x00, 0x97, 0x65,
        0x00, 0xa2, 0x6b, 0x00, 0x09, 0x72, 0x00, 0xd0, 0x78, 0x00, 0xff, 0x7f,
        0x00, 0x9c, 0x87, 0x00, 0xac, 0x8f, 0x00, 0x37, 0x98, 0x00, 0x45, 0xa1,
        0x00, 0xdc, 0xaa, 0x00, 0x04, 0xb5, 0x00, 0xc8, 0xbf, 0x00, 0x2f, 0xcb,
        0x00, 0x44, 0xd7, 0x00, 0x12, 0xe4, 0x00, 0xa1, 0xf1, 0x01, 0x00, 0x00,
        0x01, 0x39, 0x0f, 0x01, 0x59, 0x1f, 0x01, 0x6f, 0x30, 0x01, 0x8a, 0x42,
        0x01, 0xb8, 0x55, 0x01, 0x09, 0x6a, 0x01, 0x90, 0x7f, 0x01, 0x5f, 0x96,
        0x01, 0x89, 0xae, 0x01, 0x25, 0xc8, 0x01, 0x43, 0xe3, 0x02, 0x00, 0x00,
        0x02, 0x72, 0x1e, 0x02, 0xb3, 0x3e, 0x02, 0xdf, 0x60, 0x02, 0x14, 0x85,
        0x02, 0x70, 0xab, 0x02, 0x13, 0xd4, 0x02, 0x21, 0xff, 0x03, 0xbf, 0x2c,
        0x03, 0x12, 0x5d, 0x03, 0x4a, 0x90, 0x03, 0x87, 0xc6, 0x04, 0x00, 0x00,
        0x04, 0xe5, 0x3c, 0x04, 0x67, 0x7d, 0x04, 0xbf, 0xc1, 0x05, 0x28, 0x0a,
        0x05, 0xe0, 0x56, 0x05, 0x26, 0xa8, 0x05, 0x42, 0xfe, 0x06, 0x7e, 0x59,
        0x06, 0x25, 0xba, 0x07, 0x94, 0x20, 0x07, 0x0e, 0x8d, 0x07, 0xff, 0xff,
        0x08, 0xcb, 0x79, 0x08, 0xcf, 0xfa, 0x09, 0x7f, 0x83, 0x0a, 0x50, 0x14,
        0x0a, 0xc1, 0xad, 0x0b, 0x4d, 0x50, 0x0b, 0x84, 0xfc, 0x0c, 0xfc, 0xb2,
        0x0d, 0x4b, 0x74, 0x0e, 0x29, 0x41, 0x0f, 0x1d, 0x1a, 0x0f, 0xfe, 0xff,
        0x10, 0x97, 0xf3, 0x11, 0x9e, 0xf5, 0x13, 0xfe, 0x06, 0x14, 0xa1, 0x28,
        0x15, 0x82, 0x5b, 0x16, 0x9b, 0xa0, 0x17, 0x09, 0xf9, 0x19, 0xf9, 0x65,
        0x1a, 0x97, 0xe8, 0x1c, 0x53, 0x82, 0x1e, 0x3a, 0x34, 0x1f, 0xfd, 0xff,
        0x1f, 0xff, 0xff, 0x1f, 0xff, 0xff, 0x1f, 0xff, 0xff, 0x1f, 0xff, 0xff,
    },
    {
        0x20, 0x09, 0x20, 0x20, 0xf0, 0x21, 0x20, 0xf5, 0x23, 0x20, 0x18, 0x26,
        0x20, 0x5c, 0x28, 0x20, 0xc3, 0x2a, 0x20, 0x4e, 0x2d, 0x20, 0x00, 0x30,
        0x20, 0xda, 0x32, 0x20, 0xe0, 0x35, 0x20, 0x15, 0x39, 0x20, 0x79, 0x3c,
        0x20, 0x12, 0x40, 0x20, 0xe1, 0x43, 0x20, 0xeb, 0x47, 0x20, 0x31, 0x4c,
        0x20, 0xb9, 0x50, 0x20, 0x87, 0x55, 0x20, 0x9c, 0x5a, 0x20, 0x00, 0x60,
        0x20, 0xb5, 0x65, 0x20, 0xc1, 0x6b, 0x20, 0x2a, 0x72, 0x20, 0xf3, 0x78,
        0x20, 0x25, 0x80, 0x20, 0xc3, 0x87, 0x20, 0xd6, 0x8f, 0x20, 0x63, 0x98,
        0x20, 0x73, 0xa1, 0x20, 0x0e, 0xab, 0x20, 0x39, 0xb5, 0x20, 0x00, 0xc0,
        0x20, 0x6b, 0xcb, 0x20, 0x83, 0xd7, 0x20, 0x54, 0xe4, 0x20, 0xe7, 0xf1,
        0x21, 0x4a, 0x00, 0x21, 0x87, 0x0f, 0x21, 0xac, 0x1f, 0x21, 0xc7, 0x30,
        0x21, 0xe7, 0x42, 0x21, 0x1c, 0x56, 0x21, 0x72, 0x6a, 0x21, 0x00, 0x80,
        0x21, 0xd6, 0x96, 0x21, 0x06, 0xaf, 0x21, 0xa8, 0xc8, 0x21, 0xcf, 0xe3,
        0x22, 0x94, 0x00, 0x22, 0x0e, 0x1f, 0x22, 0x59, 0x3f, 0x22, 0x8f, 0x61,
        0x22, 0xce, 0x85, 0x22, 0x38, 0xac, 0x22, 0xe5, 0xd4, 0x23, 0x00, 0x00,
        0x23, 0xac, 0x2d, 0x23, 0x0d, 0x5e, 0x23, 0x50, 0x91, 0x23, 0x9e, 0xc7,
        0x24, 0x28, 0x01, 0x24, 0x1d, 0x3e, 0x24, 0xb2, 0x7e, 0x24, 0x1f, 0xc3,
        0x25, 0x9c, 0x0b, 0x25, 0x70, 0x58, 0x25, 0xcb, 0xa9, 0x26, 0x00, 0x00,
        0x26, 0x59, 0x5b, 0x26, 0x1b, 0xbc, 0x27, 0xa0, 0x22, 0x27, 0x3d, 0x8f,
        0x28, 0x51, 0x02, 0x28, 0x3b, 0x7c, 0x28, 0x64, 0xfd, 0x29, 0x3e, 0x86,
        0x2a, 0x39, 0x17, 0x2a, 0xe0, 0xb0, 0x2b, 0x96, 0x53, 0x2c, 0x00, 0x00,
        0x2c, 0xb2, 0xb6, 0x2d, 0x37, 0x78, 0x2e, 0x40, 0x45, 0x2f, 0x7a, 0x1e,
        0x30, 0xa3, 0x04, 0x30, 0x76, 0xf8, 0x31, 0xc8, 0xfa, 0x33, 0x7c, 0x0c,
        0x34, 0x73, 0x2e, 0x35, 0xc0, 0x61, 0x36, 0x2d, 0xa7, 0x38, 0x00, 0x00,
        0x39, 0x64, 0x6d, 0x3a, 0x6f, 0xf0, 0x3c, 0x80, 0x8a, 0x3e, 0xf4, 0x3c,
    },
    {
        0x40, 0x5b, 0x15, 0x40, 0xa0, 0x16, 0x40, 0xf9, 0x17, 0x40, 0x65, 0x19,
        0x40, 0xe8, 0x1a, 0x40, 0x82, 0x1c, 0x40, 0x34, 0x1e, 0x40, 0x00, 0x20,
        0x40, 0xe7, 0x21, 0x40, 0xeb, 0x23, 0x40, 0x0e, 0x26, 0x40, 0x51, 0x28,
        0x40, 0xb7, 0x2a, 0x40, 0x41, 0x2d, 0x40, 0xf2, 0x2f, 0x40, 0xcb, 0x32,
        0x40, 0xd1, 0x35, 0x40, 0x04, 0x39, 0x40, 0x68, 0x3c, 0x40, 0x00, 0x40,
        0x40, 0xce, 0x43, 0x40, 0xd6, 0x47, 0x40, 0x1c, 0x4c, 0x40, 0xa2, 0x50,
        0x40, 0x6e, 0x55, 0x40, 0x82, 0x5a, 0x40, 0xe4, 0x5f, 0x40, 0x97, 0x65,
        0x40, 0xa2, 0x6b, 0x40, 0x09, 0x72, 0x40, 0xd0, 0x78, 0x40, 0x00, 0x80,
        0x40, 0x9c, 0x87, 0x40, 0xac, 0x8f, 0x40, 0x38, 0x98, 0x40, 0x45, 0xa1,
        0x40, 0xdc, 0xaa, 0x40, 0x04, 0xb5, 0x40, 0xc8, 0xbf, 0x40, 0x2f, 0xcb,
        0x40, 0x44, 0xd7, 0x40, 0x12, 0xe4, 0x40, 0xa1, 0xf1, 0x41, 0x00, 0x00,
        0x41, 0x39, 0x0f, 0x41, 0x59, 0x1f, 0x41, 0x70, 0x30, 0x41, 0x8a, 0x42,
        0x41, 0xb8, 0x55, 0x41, 0x09, 0x6a, 0x41, 0x90, 0x7f, 0x41, 0x5f, 0x96,
        0x41, 0x89, 0xae, 0x41, 0x25, 0xc8, 0x41, 0x43, 0xe3, 0x42, 0x00, 0x00,
        0x42, 0x73, 0x1e, 0x42, 0xb3, 0x3e, 0x42, 0xe0, 0x60, 0x42, 0x14, 0x85,
        0x42, 0x70, 0xab, 0x42, 0x13, 0xd4, 0x42, 0x21, 0xff, 0x43, 0xbf, 0x2c,
        0x43, 0x13, 0x5d, 0x43, 0x4a, 0x90, 0x43, 0x87, 0xc6, 0x44, 0x00, 0x00,
        0x44, 0xe6, 0x3c, 0x44, 0x67, 0x7d, 0x44, 0xc0, 0xc1, 0x45, 0x28, 0x0a,
        0x45, 0xe1, 0x56, 0x45, 0x27, 0xa8, 0x45, 0x42, 0xfe, 0x46, 0x7e, 0x59,
        0x46, 0x26, 0xba, 0x47, 0x95, 0x20, 0x47, 0x0f, 0x8d, 0x48, 0x00, 0x00,
        0x48, 0xcc, 0x79, 0x48, 0xcf, 0xfa, 0x49, 0x80, 0x83, 0x4a, 0x51, 0x14,
        0x4a, 0xc2, 0xad, 0x4b, 0x4e, 0x50, 0x4b, 0x85, 0xfc, 0x4c, 0xfd, 0xb2,
        0x4d, 0x4c, 0x74, 0x4e, 0x2b, 0x41, 0x4f, 0x1e, 0x1a, 0x50, 0x00, 0x00,
        0x50, 0x98, 0xf3, 0x51, 0x9f, 0xf5, 0x53, 0x00, 0x07, 0x54, 0xa3, 0x28,
    },
    {
        0x60, 0x04, 0x10, 0x60, 0xf8, 0x10, 0x60, 0xfa, 0x11, 0x60, 0x0c, 0x13,
        0x60, 0x2e, 0x14, 0x60, 0x61, 0x15, 0x60, 0xa7, 0x16, 0x60, 0x00, 0x18,
        0x60, 0x6d, 0x19, 0x60, 0xf0, 0x1a, 0x60, 0x8a, 0x1c, 0x60, 0x3c, 0x1e,
        0x60, 0x09, 0x20, 0x60, 0xf0, 0x21, 0x60, 0xf5, 0x23, 0x60, 0x18, 0x26,
        0x60, 0x5c, 0x28, 0x60, 0xc3, 0x2a, 0x60, 0x4e, 0x2d, 0x60, 0x00, 0x30,
        0x60, 0xda, 0x32, 0x60, 0xe0, 0x35, 0x60, 0x15, 0x39, 0x60, 0x79, 0x3c,
        0x60, 0x12, 0x40, 0x60, 0xe1, 0x43, 0x60, 0xeb, 0x47, 0x60, 0x31, 0x4c,
        0x60, 0xb9, 0x50, 0x60, 0x87, 0x55, 0x60, 0x9c, 0x5a, 0x60, 0x00, 0x60,
        0x60, 0xb5, 0x65, 0x60, 0xc1, 0x6b, 0x60, 0x2a, 0x72, 0x60, 0xf3, 0x78,
        0x60, 0x25, 0x80, 0x60, 0xc3, 0x87, 0x60, 0xd6, 0x8f, 0x60, 0x63, 0x98,
        0x60, 0x73, 0xa1, 0x60, 0x0e, 0xab, 0x60, 0x39, 0xb5, 0x60, 0x00, 0xc0,
        0x60, 0x6b, 0xcb, 0x60, 0x83, 0xd7, 0x60, 0x54, 0xe4, 0x60, 0xe7, 0xf1,
        0x61, 0x4a, 0x00, 0x61, 0x87, 0x0f, 0x61, 0xac, 0x1f, 0x61, 0xc7, 0x30,
        0x61, 0xe7, 0x42, 0x61, 0x1c, 0x56, 0x61, 0x72, 0x6a, 0x61, 0x00, 0x80,
        0x61, 0xd6, 0x96, 0x61, 0x06, 0xaf, 0x61, 0xa8, 0xc8, 0x61, 0xcf, 0xe3,
        0x62, 0x94, 0x00, 0x62, 0x0e, 0x1f, 0x62, 0x59, 0x3f, 0x62, 0x8f, 0x61,
        0x62, 0xce, 0x85, 0x62, 0x38, 0xac, 0x62, 0xe5, 0xd4, 0x63, 0x00, 0x00,
        0x63, 0xac, 0x2d, 0x63, 0x0d, 0x5e, 0x63, 0x50, 0x91, 0x63, 0x9e, 0xc7,
        0x64, 0x28, 0x01, 0x64, 0x1d, 0x3e, 0x64, 0xb2, 0x7e, 0x64, 0x1f, 0xc3,
        0x65, 0x9c, 0x0b, 0x65, 0x70, 0x58, 0x65, 0xcb, 0xa9, 0x66, 0x00, 0x00,
        0x66, 0x59, 0x5b, 0x66, 0x1b, 0xbc, 0x67, 0xa0, 0x22, 0x67, 0x3d, 0x8f,
        0x68, 0x51, 0x02, 0x68, 0x3b, 0x7c, 0x68, 0x64, 0xfd, 0x69, 0x3e, 0x86,
        0x6a, 0x39, 0x17, 0x6a, 0xe0, 0xb0, 0x6b, 0x96, 0x53, 0x6c, 0x00, 0x00,
        0x6c, 0xb2, 0xb6, 0x6d, 0x37, 0x78, 0x6e, 0x40, 0x45, 0x6f, 0x7a, 0x1e,
    },
    {
        0x80, 0xad, 0x0a, 0x80, 0x50, 0x0b, 0x80, 0xfc, 0x0b, 0x80, 0xb2, 0x0c,
        0x80, 0x74, 0x0d, 0x80, 0x41, 0x0e, 0x80, 0x1a, 0x0f, 0x80, 0x00, 0x10,
        0x80, 0xf3, 0x10, 0x80, 0xf5, 0x11, 0x80, 0x07, 0x13, 0x80, 0x28, 0x14,
        0x80, 0x5b, 0x15, 0x80, 0xa0, 0x16, 0x80, 0xf9, 0x17, 0x80, 0x65, 0x19,
        0x80, 0xe8, 0x1a, 0x80, 0x82, 0x1c, 0x80, 0x34, 0x1e, 0x80, 0x00, 0x20,
        0x80, 0xe7, 0x21, 0x80, 0xeb, 0x23, 0x80, 0x0e, 0x26, 0x80, 0x51, 0x28,
        0x80, 0xb7, 0x2a, 0x80, 0x41, 0x2d, 0x80, 0xf2, 0x2f, 0x80, 0xcb, 0x32,
        0x80, 0xd1, 0x35, 0x80, 0x04, 0x39, 0x80, 0x68, 0x3c, 0x80, 0x00, 0x40,
        0x80, 0xce, 0x43, 0x80, 0xd6, 0x47, 0x80, 0x1c, 0x4c, 0x80, 0xa2, 0x50,
        0x80, 0x6e, 0x55, 0x80, 0x82, 0x5a, 0x80, 0xe4, 0x5f, 0x80, 0x97, 0x65,
        0x80, 0xa2, 0x6b, 0x80, 0x09, 0x72, 0x80, 0xd0, 0x78, 0x80, 0x00, 0x80,
        0x80, 0x9c, 0x87, 0x80, 0xac, 0x8f, 0x80, 0x38, 0x98, 0x80, 0x45, 0xa1,
        0x80, 0xdc, 0xaa, 0x80, 0x04, 0xb5, 0x80, 0xc8, 0xbf, 0x80, 0x2f, 0xcb,
        0x80, 0x44, 0xd7, 0x80, 0x12, 0xe4, 0x80, 0xa1, 0xf1, 0x81, 0x00, 0x00,
        0x81, 0x39, 0x0f, 0x81, 0x59, 0x1f, 0x81, 0x70, 0x30, 0x81, 0x8a, 0x42,
        0x81, 0xb8, 0x55, 0x81, 0x09, 0x6a, 0x81, 0x90, 0x7f, 0x81, 0x5f, 0x96,
        0x81, 0x89, 0xae, 0x81, 0x25, 0xc8, 0x81, 0x43, 0xe3, 0x82, 0x00, 0x00,
        0x82, 0x73, 0x1e, 0x82, 0xb3, 0x3e, 0x82, 0xe0, 0x60, 0x82, 0x14, 0x85,
        0x82, 0x70, 0xab, 0x82, 0x13, 0xd4, 0x82, 0x21, 0xff, 0x83, 0xbf, 0x2c,
        0x83, 0x13, 0x5d, 0x83, 0x4a, 0x90, 0x83, 0x87, 0xc6, 0x84, 0x00, 0x00,
        0x84, 0xe6, 0x3c, 0x84, 0x67, 0x7d, 0x84, 0xc0, 0xc1, 0x85, 0x28, 0x0a,
        0x85, 0xe1, 0x56, 0x85, 0x27, 0xa8, 0x85, 0x42, 0xfe, 0x86, 0x7e, 0x59,
        0x86, 0x26, 0xba, 0x87, 0x95, 0x20, 0x87, 0x0f, 0x8d, 0x88, 0x00, 0x00,
        0x88, 0xcc, 0x79, 0x88, 0xcf, 0xfa, 0x89, 0x80, 0x83, 0x8a, 0x51, 0x14,
    },
    {
        0xa0, 0x02, 0x08, 0xa0, 0x7c, 0x08, 0xa0, 0xfd, 0x08, 0xa0, 0x86, 0x09,
        0xa0, 0x17, 0x0a, 0xa0, 0xb0, 0x0a, 0xa0, 0x53, 0x0b, 0xa0, 0x00, 0x0c,
        0xa0, 0xb6, 0x0c, 0xa0, 0x78, 0x0d, 0xa0, 0x45, 0x0e, 0xa0, 0x1e, 0x0f,
        0xa0, 0x04, 0x10, 0xa0, 0xf8, 0x10, 0xa0, 0xfa, 0x11, 0xa0, 0x0c, 0x13,
        0xa0, 0x2e, 0x14, 0xa0, 0x61, 0x15, 0xa0, 0xa7, 0x16, 0xa0, 0x00, 0x18,
        0xa0, 0x6d, 0x19, 0xa0, 0xf0, 0x1a, 0xa0, 0x8a, 0x1c, 0xa0, 0x3c, 0x1e,
        0xa0, 0x09, 0x20, 0xa0, 0xf0, 0x21, 0xa0, 0xf5, 0x23, 0xa0, 0x18, 0x26,
        0xa0, 0x5c, 0x28, 0xa0, 0xc3, 0x2a, 0xa0, 0x4e, 0x2d, 0xa0, 0x00, 0x30,
        0xa0, 0xda, 0x32, 0xa0, 0xe0, 0x35, 0xa0, 0x15, 0x39, 0xa0, 0x79, 0x3c,
        0xa0, 0x12, 0x40, 0xa0, 0xe1, 0x43, 0xa0, 0xeb, 0x47, 0xa0, 0x31, 0x4c,
        0xa0, 0xb9, 0x50, 0xa0, 0x87, 0x55, 0xa0, 0x9c, 0x5a, 0xa0, 0x00, 0x60,
        0xa0, 0xb5, 0x65, 0xa0, 0xc1, 0x6b, 0xa0, 0x2a, 0x72, 0xa0, 0xf3, 0x78,
        0xa0, 0x25, 0x80, 0xa0, 0xc3, 0x87, 0xa0, 0xd6, 0x8f, 0xa0, 0x63, 0x98,
        0xa0, 0x73, 0xa1, 0xa0, 0x0e, 0xab, 0xa0, 0x39, 0xb5, 0xa0, 0x00, 0xc0,
        0xa0, 0x6b, 0xcb, 0xa0, 0x83, 0xd7, 0xa0, 0x54, 0xe4, 0xa0, 0xe7, 0xf1,
        0xa1, 0x4a, 0x00, 0xa1, 0x87, 0x0f, 0xa1, 0xac, 0x1f, 0xa1, 0xc7, 0x30,
        0xa1, 0xe7, 0x42, 0xa1, 0x1c, 0x56, 0xa1, 0x72, 0x6a, 0xa1, 0x00, 0x80,
        0xa1, 0xd6, 0x96, 0xa1, 0x06, 0xaf, 0xa1, 0xa8, 0xc8, 0xa1, 0xcf, 0xe3,
        0xa2, 0x94, 0x00, 0xa2, 0x0e, 0x1f, 0xa2, 0x59, 0x3f, 0xa2, 0x8f, 0x61,
        0xa2, 0xce, 0x85, 0xa2, 0x38, 0xac, 0xa2, 0xe5, 0xd4, 0xa3, 0x00, 0x00,
        0xa3, 0xac, 0x2d, 0xa3, 0x0d, 0x5e, 0xa3, 0x50, 0x91, 0xa3, 0x9e, 0xc7,
        0xa4, 0x28, 0x01, 0xa4, 0x1d, 0x3e, 0xa4, 0xb2, 0x7e, 0xa4, 0x1f, 0xc3,
        0xa5, 0x9c, 0x0b, 0xa5, 0x70, 0x58, 0xa5, 0xcb, 0xa9, 0xa6, 0x00, 0x00,
        0xa6, 0x59, 0x5b, 0xa6, 0x1b, 0xbc, 0xa7, 0xa0, 0x22, 0xa7, 0x3d, 0x8f,
    },
    {
        0xc0, 0x56, 0x05, 0xc0, 0xa8, 0x05, 0xc0, 0xfe, 0x05, 0xc0, 0x59, 0x06,
        0xc0, 0xba, 0x06, 0xc0, 0x20, 0x07, 0xc0, 0x8d, 0x07, 0xc0, 0x00, 0x08,
        0xc0, 0x79, 0x08, 0xc0, 0xfa, 0x08, 0xc0, 0x83, 0x09, 0xc0, 0x14, 0x0a,
        0xc0, 0xad, 0x0a, 0xc0, 0x50, 0x0b, 0xc0, 0xfc, 0x0b, 0xc0, 0xb2, 0x0c,
        0xc0, 0x74, 0x0d, 0xc0, 0x41, 0x0e, 0xc0, 0x1a, 0x0f, 0xc0, 0x00, 0x10,
        0xc0, 0xf3, 0x10, 0xc0, 0xf5, 0x11, 0xc0, 0x07, 0x13, 0xc0, 0x28, 0x14,
        0xc0, 0x5b, 0x15, 0xc0, 0xa0, 0x16, 0xc0, 0xf9, 0x17, 0xc0, 0x65, 0x19,
        0xc0, 0xe8, 0x1a, 0xc0, 0x82, 0x1c, 0xc0, 0x34, 0x1e, 0xc0, 0x00, 0x20,
        0xc0, 0xe7, 0x21, 0xc0, 0xeb, 0x23, 0xc0, 0x0e, 0x26, 0xc0, 0x51, 0x28,
        0xc0, 0xb7, 0x2a, 0xc0, 0x41, 0x2d, 0xc0, 0xf2, 0x2f, 0xc0, 0xcb, 0x32,
        0xc0, 0xd1, 0x35, 0xc0, 0x04, 0x39, 0xc0, 0x68, 0x3c, 0xc0, 0x00, 0x40,
        0xc0, 0xce, 0x43, 0xc0, 0xd6, 0x47, 0xc0, 0x1c, 0x4c, 0xc0, 0xa2, 0x50,
        0xc0, 0x6e, 0x55, 0xc0, 0x82, 0x5a, 0xc0, 0xe4, 0x5f, 0xc0, 0x97, 0x65,
        0xc0, 0xa2, 0x6b, 0xc0, 0x09, 0x72, 0xc0, 0xd0, 0x78, 0xc0, 0x00, 0x80,
        0xc0, 0x9c, 0x87, 0xc0, 0xac, 0x8f, 0xc0, 0x38, 0x98, 0xc0, 0x45, 0xa1,
        0xc0, 0xdc, 0xaa, 0xc0, 0x04, 0xb5, 0xc0, 0xc8, 0xbf, 0xc0, 0x2f, 0xcb,
        0xc0, 0x44, 0xd7, 0xc0, 0x12, 0xe4, 0xc0, 0xa1, 0xf1, 0xc1, 0x00, 0x00,
        0xc1, 0x39, 0x0f, 0xc1, 0x59, 0x1f, 0xc1, 0x70, 0x30, 0xc1, 0x8a, 0x42,
        0xc1, 0xb8, 0x55, 0xc1, 0x09, 0x6a, 0xc1, 0x90, 0x7f, 0xc1, 0x5f, 0x96,
        0xc1, 0x89, 0xae, 0xc1, 0x25, 0xc8, 0xc1, 0x43, 0xe3, 0xc2, 0x00, 0x00,
        0xc2, 0x73, 0x1e, 0xc2, 0xb3, 0x3e, 0xc2, 0xe0, 0x60, 0xc2, 0x14, 0x85,
        0xc2, 0x70, 0xab, 0xc2, 0x13, 0xd4, 0xc2, 0x21, 0xff, 0xc3, 0xbf, 0x2c,
        0xc3, 0x13, 0x5d, 0xc3, 0x4a, 0x90, 0xc3, 0x87, 0xc6, 0xc4, 0x00, 0x00,
        0xc4, 0xe6, 0x3c, 0xc4, 0x67, 0x7d, 0xc4, 0xc0, 0xc1, 0xc5, 0x28, 0x0a,
    },
    {
        0xe0, 0x01, 0x04, 0xe0, 0x3e, 0x04, 0xe0, 0x7e, 0x04, 0xe0, 0xc3, 0x04,
        0xe0, 0x0b, 0x05, 0xe0, 0x58, 0x05, 0xe0, 0xa9, 0x05, 0xe0, 0x00, 0x06,
        0xe0, 0x5b, 0x06, 0xe0, 0xbc, 0x06, 0xe0, 0x22, 0x07, 0xe0, 0x8f, 0x07,
        0xe0, 0x02, 0x08, 0xe0, 0x7c, 0x08, 0xe0, 0xfd, 0x08, 0xe0, 0x86, 0x09,
        0xe0, 0x17, 0x0a, 0xe0, 0xb0, 0x0a, 0xe0, 0x53, 0x0b, 0xe0, 0x00, 0x0c,
        0xe0, 0xb6, 0x0c, 0xe0, 0x78, 0x0d, 0xe0, 0x45, 0x0e, 0xe0, 0x1e, 0x0f,
        0xe0, 0x04, 0x10, 0xe0, 0xf8, 0x10, 0xe0, 0xfa, 0x11, 0xe0, 0x0c, 0x13,
        0xe0, 0x2e, 0x14, 0xe0, 0x61, 0x15, 0xe0, 0xa7, 0x16, 0xe0, 0x00, 0x18,
        0xe0, 0x6d, 0x19, 0xe0, 0xf0, 0x1a, 0xe0, 0x8a, 0x1c, 0xe0, 0x3c, 0x1e,
        0xe0, 0x09, 0x20, 0xe0, 0xf0, 0x21, 0xe0, 0xf5, 0x23, 0xe0, 0x18, 0x26,
        0xe0, 0x5c, 0x28, 0xe0, 0xc3, 0x2a, 0xe0, 0x4e, 0x2d, 0xe0, 0x00, 0x30,
        0xe0, 0xda, 0x32, 0xe0, 0xe0, 0x35, 0xe0, 0x15, 0x39, 0xe0, 0x79, 0x3c,
        0xe0, 0x12, 0x40, 0xe0, 0xe1, 0x43, 0xe0, 0xeb, 0x47, 0xe0, 0x31, 0x4c,
        0xe0, 0xb9, 0x50, 0xe0, 0x87, 0x55, 0xe0, 0x9c, 0x5a, 0xe0, 0x00, 0x60,
        0xe0, 0xb5, 0x65, 0xe0, 0xc1, 0x6b, 0xe0, 0x2a, 0x72, 0xe0, 0xf3, 0x78,
        0xe0, 0x25, 0x80, 0xe0, 0xc3, 0x87, 0xe0, 0xd6, 0x8f, 0xe0, 0x63, 0x98,
        0xe0, 0x73, 0xa1, 0xe0, 0x0e, 0xab, 0xe0, 0x39, 0xb5, 0xe0, 0x00, 0xc0,
        0xe0, 0x6b, 0xcb, 0xe0, 0x83, 0xd7, 0xe0, 0x54, 0xe4, 0xe0, 0xe7, 0xf1,
        0xe1, 0x4a, 0x00, 0xe1, 0x87, 0x0f, 0xe1, 0xac, 0x1f, 0xe1, 0xc7, 0x30,
        0xe1, 0xe7, 0x42, 0xe1, 0x1c, 0x56, 0xe1, 0x72, 0x6a, 0xe1, 0x00, 0x80,
        0xe1, 0xd6, 0x96, 0xe1, 0x06, 0xaf, 0xe1, 0xa8, 0xc8, 0xe1, 0xcf, 0xe3,
        0xe2, 0x94, 0x00, 0xe2, 0x0e, 0x1f, 0xe2, 0x59, 0x3f, 0xe2, 0x8f, 0x61,
        0xe2, 0xce, 0x85, 0xe2, 0x38, 0xac, 0xe2, 0xe5, 0xd4, 0xe3, 0x00, 0x00,
        0xe3, 0xac, 0x2d, 0xe3, 0x0d, 0x5e, 0xe3, 0x50, 0x91, 0xe3, 0x9e, 0xc7,
    },
};

#endif

#if board2 && adpcm && !ademu
// ADRAMCHK.ASM:385
// The ADPCM RAM test pattern. The original's buffer is exactly 32 bytes with no
// terminator; the extra byte here is never sent.
static const char check_code[] = "*+=-PMD ADPCM RAM Check Data-=+*";
#endif

// :10568
// Byte offsets of int_num and port_num inside pmd->mes_int, which is the
// original's mutable resident message.
enum {
    mes_int_int_num = 4,
    mes_int_port_num = 11,
};

// :10749
// The interrupt number and level for each value of the FM register 0Eh
// interrupt bits. The original also stores a DOS vector offset at +2 of each
// entry; the host owns vector installation, so it is omitted.
static const struct {
    char num;
    uint8_t level;
} vector_table[4] = {
    {'0', 3},  // int0
    {'6', 13}, // int6
    {'4', 10}, // int4
    {'5', 12}, // int5
};

// An option handler. `cmd` is the original's DS:SI and from_env is set while
// parsing PMDOPT, where a usage error abandons parsing instead of exiting.
// Returns false when option parsing must stop.
typedef bool (*pmd_option_fn)(struct pmd *pmd, const char **cmd, bool from_env);

// :10759 one entry of the original's 3-byte option tables
struct pmd_option {
    char name;
    pmd_option_fn fn;
};

// :1713 The MML command tables jump into the caller's part loop when a command
// only needs the part restarted or resumed. The original does that by popping
// the call's return address and jumping to a label inside fmmain/psgmain; the
// translation returns the destination to the main loop instead.
enum pmd_cmd_result {
    PMD_CMD_RETURN,
    // fmmain's mp1 (read the next command/note).
    PMD_CMD_MP1,
    // fmmain_nonplay's fmmnp_1.
    PMD_CMD_FMMNP_1,
    // psgmain's mp1p.
    PMD_CMD_MP1P,
    // psgmain_nonplay's psgmnp_1.
    PMD_CMD_PSGMNP_1,
    // fmmain's porta_return and psgmain's porta_returnp.
    PMD_CMD_PORTA_RETURN,
    PMD_CMD_PORTA_RETURNP,
    // ppzmain's mp1z and porta_returnz.
    PMD_CMD_MP1Z,
    PMD_CMD_PORTA_RETURNZ,
    // ppzmain_nonplay's ppzmnp_1.
    PMD_CMD_PPZMNP_1,
    // pcmmain's mp1m and porta_returnm.
    PMD_CMD_MP1M,
    PMD_CMD_PORTA_RETURNM,
    // pcmmain_nonplay's pcmmnp_1.
    PMD_CMD_PCMMNP_1,
};

typedef enum pmd_cmd_result (*pmd_cmd_fn)(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);

// :151
PMDC_NORETURN static void error_exit(struct pmd *pmd, int status);
// :173
static void print_mes(struct pmd *pmd, const char *mes);
// :218
static void _wait(struct pmd *pmd);
// :223
static void _waitP(struct pmd *pmd);
#if board2
// :230
static void _rwait(struct pmd *pmd);
#endif
// :242
static uint8_t rdychk(struct pmd *pmd, uint16_t port);
// :308
// Counts one INT 60h call and applies the reint_chk class of table entry
// `entry` (the original AH value). Returns false when the call is rejected, in
// which case the caller must not change state.
static bool int60_enter(struct pmd *pmd, uint8_t entry);
// :315
static void int60_exit(struct pmd *pmd);
#if board2
// PCMDRV.ASM:7, PCMDRVE.ASM:7, PCMDRV86.ASM:7
static void pcmmain(struct pmd *pmd, struct pmd_qq *part);
// PCMDRV.ASM:821, PCMDRV86.ASM:1347
static void pcm_effect(struct pmd *pmd, uint8_t efcnum, uint16_t deltan, uint8_t pan, uint8_t volume);
#endif
#if board2 && (pcm || adpcm)
// PCMDRV.ASM:185, PCMDRVE.ASM:173, PCMDRV86.ASM:180
static enum pmd_cmd_result commandsm(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// The PCM command table (PCMDRV.ASM:188, PCMDRV86.ASM:183).
static const pmd_cmd_fn cmdtblm[0xff - 0xb1 + 1];
// PCMDRV.ASM:291, PCMDRVE.ASM:279, PCMDRV86.ASM:281
static enum pmd_cmd_result pcm_mml_part_mask(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// PCMDRV.ASM:317, PCMDRVE.ASM:305, PCMDRV86.ASM:304
static enum pmd_cmd_result pcmrepeat_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// PCMDRV.ASM:355, PCMDRVE.ASM:359. PCMDRV86.ASM:246 has no portamento: its
// 0dah entry is jump1.
#if !pcm
static enum pmd_cmd_result portam(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
#endif
// PCMDRV.ASM:401, PCMDRVE.ASM:405, PCMDRV86.ASM:426
static enum pmd_cmd_result comvolupm(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// PCMDRV.ASM:411, PCMDRVE.ASM:415, PCMDRV86.ASM:436
static enum pmd_cmd_result comvolupm2(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// PCMDRV.ASM:418, PCMDRVE.ASM:422, PCMDRV86.ASM:444
static enum pmd_cmd_result comvoldownm(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// PCMDRV.ASM:425, PCMDRVE.ASM:429, PCMDRV86.ASM:451
static enum pmd_cmd_result comvoldownm2(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// PCMDRV.ASM:437, PCMDRVE.ASM:441, PCMDRV86.ASM:467
static enum pmd_cmd_result pansetm(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// PCMDRV.ASM:449, PCMDRVE.ASM:465, PCMDRV86.ASM:491
static enum pmd_cmd_result pansetm_ex(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// PCMDRV.ASM:467, PCMDRVE.ASM:483, PCMDRV86.ASM:531
static enum pmd_cmd_result com_at_m(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// PCMDRV.ASM:489, PCMDRVE.ASM:512, PCMDRV86.ASM:564
static void volsetm(struct pmd *pmd, struct pmd_qq *part);
// PCMDRV.ASM:606, PCMDRVE.ASM:630, PCMDRV86.ASM:707
static void keyonm(struct pmd *pmd, struct pmd_qq *part);
// PCMDRV.ASM:676, PCMDRVE.ASM:650, PCMDRV86.ASM:722
static void keyoffm(struct pmd *pmd, struct pmd_qq *part);
// PCMDRV.ASM:718, PCMDRVE.ASM:665, PCMDRV86.ASM:752
static void otodasim(struct pmd *pmd, struct pmd_qq *part);
// PCMDRV.ASM:771, PCMDRVE.ASM:711, PCMDRV86.ASM:817
static void fnumsetm(struct pmd *pmd, struct pmd_qq *part, uint8_t note);
#endif
#if board2 && pcm
// PCMDRV86.ASM:412
static uint16_t pcm86vol_chk(struct pmd *pmd, uint16_t value);
// PCMDRV86.ASM:1162
static const uint8_t *get_data_offset(struct pmd *pmd);
// PCMDRV86.ASM:789
static void tone_set2(struct pmd *pmd, uint16_t freq, uint8_t rate);
// PCMDRV86.ASM:534
static void neiro_set(struct pmd *pmd, uint8_t voice);
// PCMDRV86.ASM:671
static void pcm86_vol_out(struct pmd *pmd, uint8_t volume);
// PCMDRV86.ASM:495
static void set_pcm_pan(struct pmd *pmd, struct pmd_qq *part, uint8_t pan, uint8_t rev);
// PCMDRV86.ASM:498
static void set_pcm_pan2(struct pmd *pmd, uint8_t pan, uint8_t rev);
// PCMDRV86.ASM:1386
enum { trans_size = 256 };
// PCMDRV86.ASM:859
static void fifo_main(struct pmd *pmd);
// PCMDRV86.ASM:910
static void pcm_trans(struct pmd *pmd, uint16_t count);
// PCMDRV86.ASM:1205
static void play_86pcm(struct pmd *pmd);
// PCMDRV86.ASM:1300
static void stop_86pcm(struct pmd *pmd);
#endif
#if ppz
// PPZDRV.ASM:4
static const struct pmd_drv_ppz8 *ppz8_ops(struct pmd *pmd);
// PPZDRV.ASM:11
static void ppzmain(struct pmd *pmd, struct pmd_qq *part);
// PPZDRV.ASM:185
static enum pmd_cmd_result commandsz(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// PPZDRV.ASM:188
static const pmd_cmd_fn cmdtblz[0xff - 0xb1 + 1];
// PPZDRV.ASM:287
static enum pmd_cmd_result ppz_extpartset(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// PPZDRV.ASM:320
static enum pmd_cmd_result ppz_mml_part_mask(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// PPZDRV.ASM:352
static enum pmd_cmd_result ppzrepeat_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// PPZDRV.ASM:392
static const struct pmd_ppz8_voice *ppz_voicetable_calc(struct pmd *pmd, struct pmd_qq *part);
// PPZDRV.ASM:415
static enum pmd_cmd_result portaz(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// PPZDRV.ASM:475
static enum pmd_cmd_result pansetz(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// PPZDRV.ASM:494
static enum pmd_cmd_result pansetz_ex(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// PPZDRV.ASM:514
static enum pmd_cmd_result com_at_z(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// PPZDRV.ASM:542
static void ppz_neiro_reset(struct pmd *pmd, struct pmd_qq *part);
// PPZDRV.ASM:571
static void volsetz(struct pmd *pmd, struct pmd_qq *part);
// PPZDRV.ASM:699
static void keyonz(struct pmd *pmd, struct pmd_qq *part);
// PPZDRV.ASM:720
static void keyoffz(struct pmd *pmd, struct pmd_qq *part);
// PPZDRV.ASM:735
static void otodasiz(struct pmd *pmd, struct pmd_qq *part);
// PPZDRV.ASM:801
static void fnumsetz(struct pmd *pmd, struct pmd_qq *part, uint8_t note);
#endif
#if board2 && (pcm || adpcm)
// The volume calculation shared by the PPZ8 and PCM volume setters
// (PPZDRV.ASM:578, PCMDRV.ASM:496).
static uint8_t pcm_vol_calc(struct pmd *pmd, struct pmd_qq *part, uint8_t voldown, bool square_fade);
#endif
// EFCDRV.ASM:9
// `efcnum` is the effect number. `part` is the part whose detune and volume the
// PPSDRV path corrects; eff_on passes NULL because its hosei_flag is 0.
static void effgo(struct pmd *pmd, struct pmd_qq *part, uint8_t efcnum);
// EFCDRV.ASM:22
static void eff_on2(struct pmd *pmd, struct pmd_qq *part, uint8_t efcnum);
// EFCDRV.ASM:25
static void eff_on(struct pmd *pmd, uint8_t efcnum);
// EFCDRV.ASM:27
static void eff_main(struct pmd *pmd, struct pmd_qq *part, uint8_t efcnum);
// EFCDRV.ASM:118
static void effplay(struct pmd *pmd);
// EFCDRV.ASM:125
static void efffor(struct pmd *pmd, const uint8_t **src);
// EFCDRV.ASM:178
static void efsnd(struct pmd *pmd, const uint8_t **src, uint8_t *reg, uint8_t *value);
// EFCDRV.ASM:184
static void effend(struct pmd *pmd);
// EFCDRV.ASM:210
static void effsweep(struct pmd *pmd);
// :392
static void fm_efcplay(struct pmd *pmd);
// :440
static void mstart(struct pmd *pmd);
// :505
static void play_init(struct pmd *pmd);
// :626
static void data_init(struct pmd *pmd);
// :632
static void data_init2(struct pmd *pmd);
// :718
static void opn_init(struct pmd *pmd);
// :883
static void mstop(struct pmd *pmd);
// :900
static void mmain(struct pmd *pmd);
#if board2
// :1029
static void sel46(struct pmd *pmd);
// :1039
static void sel44(struct pmd *pmd);
#endif
// :1053
static uint8_t *fmmain(struct pmd *pmd, struct pmd_qq *part);
#if ppz
// :250
static void ppz_int_hook(struct pmd *pmd);
#endif
// :1306
static void rest_note(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :1204
static void mnp_ret(struct pmd *pmd, struct pmd_qq *part);
// :1215
static void calc_q(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :1318
static void fmmnp_4(struct pmd *pmd, struct pmd_qq *part);
// :1329
static void psgmain(struct pmd *pmd, struct pmd_qq *part);
// :1508
static bool ssgdrum_check(struct pmd *pmd, struct pmd_qq *part, uint8_t note);
// :1539
static void rhythmmain(struct pmd *pmd, struct pmd_qq *part);
// :1713
static enum pmd_cmd_result commands(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :1717
static enum pmd_cmd_result commandsr(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :1721
static enum pmd_cmd_result commandsp(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :1724
static enum pmd_cmd_result command00(struct pmd *pmd, struct pmd_qq *part, uint8_t **src, const pmd_cmd_fn *table);
// :1745
static const pmd_cmd_fn cmdtbl[0xff - 0xb1 + 1];
// :1843
static const pmd_cmd_fn cmdtblp[0xff - 0xb1 + 1];
// :1939
static const pmd_cmd_fn cmdtblr[0xff - 0xb1 + 1];
// :2035
static enum pmd_cmd_result jump16(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2037
static enum pmd_cmd_result jump6(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2039
static enum pmd_cmd_result jump5(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2041
static enum pmd_cmd_result jump4(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2043
static enum pmd_cmd_result jump3(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2045
static enum pmd_cmd_result jump2(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2047
static enum pmd_cmd_result jump1(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2049
static enum pmd_cmd_result jump0(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2055
static enum pmd_cmd_result special_0c0h(struct pmd *pmd, struct pmd_qq *part, uint8_t **src, uint8_t cmd);
// :2082
static enum pmd_cmd_result pmd86_s(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2091
static enum pmd_cmd_result vd_fm(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2092
static enum pmd_cmd_result vd_main(struct pmd *pmd, struct pmd_qq *part, uint8_t **src, uint8_t *vol, uint8_t *base, bool add);
// :2097
static enum pmd_cmd_result vd_ssg(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2100
static enum pmd_cmd_result vd_pcm(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2103
static enum pmd_cmd_result vd_rhythm(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2107
static enum pmd_cmd_result vd_ppz(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2111
static enum pmd_cmd_result _vd_fm(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2136
static enum pmd_cmd_result _vd_ssg(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2141
static enum pmd_cmd_result _vd_pcm(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2146
static enum pmd_cmd_result _vd_rhythm(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2151
static enum pmd_cmd_result _vd_ppz(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2159
static enum pmd_cmd_result slot_delay(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2176
static enum pmd_cmd_result fb_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2186
static void fb_set3(struct pmd *pmd, struct pmd_qq *part, uint8_t fb);
// :2213
static void _fb_set(struct pmd *pmd, struct pmd_qq *part, uint8_t fb);
// :2248
static enum pmd_cmd_result tl_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2357
static enum pmd_cmd_result fm_mml_part_mask(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2378
static enum pmd_cmd_result ssg_mml_part_mask(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2402
static enum pmd_cmd_result rhythm_mml_part_mask(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2417
static void neiro_reset(struct pmd *pmd, struct pmd_qq *part);
// :2478
static enum pmd_cmd_result pdrswitch(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2493
static enum pmd_cmd_result volmask_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2510
static enum pmd_cmd_result _volmask_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2528
static bool ch3_setting(struct pmd *pmd, struct pmd_qq *part);
// :2549
static enum pmd_cmd_result fm3_extpartset(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2576
static void fm3_partinit(struct pmd *pmd, struct pmd_qq *part, uint8_t *address);
// :2597
static enum pmd_cmd_result detune_extend(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2607
static enum pmd_cmd_result lfo_extend(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2618
static enum pmd_cmd_result envelope_extend(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2630
static enum pmd_cmd_result lfowave_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2638
static enum pmd_cmd_result extend_psgenvset(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2674
static enum pmd_cmd_result slotdetune_set2(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2704
static enum pmd_cmd_result slotdetune_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2745
static void ch3mode_set(struct pmd *pmd, struct pmd_qq *part);
// :2823
static void otodasi_cm(struct pmd *pmd, struct pmd_qq *part);
// :2839
static enum pmd_cmd_result slotmask_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2945
static void keyon_sm(struct pmd *pmd, struct pmd_qq *part);
// :2957
static enum pmd_cmd_result ssg_efct_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :2981
static enum pmd_cmd_result fm_efct_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3029
static enum pmd_cmd_result fade_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3037
static enum pmd_cmd_result mdepth_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3045
static enum pmd_cmd_result mdepth_count(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3067
static void porta_calc(struct pmd *pmd, struct pmd_qq *part);
// :3085
static enum pmd_cmd_result porta(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3155
static enum pmd_cmd_result portap(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3200
static enum pmd_cmd_result status_write(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3208
static enum pmd_cmd_result status_add(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3218
static enum pmd_cmd_result vol_one_up_fm(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3229
static enum pmd_cmd_result vol_one_up_psg(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
#if (board2 && (pcm || adpcm)) || ppz
// :3237
static enum pmd_cmd_result vol_one_up_pcm(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
#endif
// :3248
static enum pmd_cmd_result vol_one_down(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3260
static enum pmd_cmd_result hlfo_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3304
static enum pmd_cmd_result hlfo_onoff(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3318
static enum pmd_cmd_result hlfo_delay(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3328
static enum pmd_cmd_result syousetu_lng_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3334
static enum pmd_cmd_result com_at(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3381
static enum pmd_cmd_result comq(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3386
static enum pmd_cmd_result comq3(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3390
static enum pmd_cmd_result comq4(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3398
static enum pmd_cmd_result comq2(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3406
static enum pmd_cmd_result comv(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3417
static enum pmd_cmd_result comt(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3480
static void calc_tb_tempo(struct pmd *pmd);
// :3502
static void calc_tempo_tb(struct pmd *pmd);
// :3524
static enum pmd_cmd_result comtie(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3531
static enum pmd_cmd_result comd(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3538
static enum pmd_cmd_result comdd(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3545
static enum pmd_cmd_result comstloop(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3561
static enum pmd_cmd_result comedloop(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3590
static enum pmd_cmd_result comexloop(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3613
static enum pmd_cmd_result comlopset(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3620
static enum pmd_cmd_result comshift(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3628
static enum pmd_cmd_result comshift2(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3637
static enum pmd_cmd_result comshift_master(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3646
static enum pmd_cmd_result comvolup(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3657
static enum pmd_cmd_result comvolup2(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3663
static enum pmd_cmd_result comvolupp(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3673
static enum pmd_cmd_result comvolupp2(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3682
static enum pmd_cmd_result comvoldown(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3690
static enum pmd_cmd_result comvoldown2(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3700
static enum pmd_cmd_result comvoldownp(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3708
static enum pmd_cmd_result comvoldownp2(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3720
static enum pmd_cmd_result _lfoset(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3733
static enum pmd_cmd_result _mdepth_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3737
static enum pmd_cmd_result _lfowave_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3741
static enum pmd_cmd_result _lfo_extend(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3745
static enum pmd_cmd_result _lfoset_delay(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3749
static enum pmd_cmd_result _lfoswitch(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3762
static enum pmd_cmd_result _lfoswitch_f(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3769
static void lfo_change(struct pmd_qq *part);
// :3813
static enum pmd_cmd_result lfoset(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3827
static enum pmd_cmd_result lfoset_delay(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3836
static enum pmd_cmd_result lfoswitch(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3847
static enum pmd_cmd_result lfoswitch_f(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3854
static enum pmd_cmd_result psgenvset(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3876
static enum pmd_cmd_result comy(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3885
static enum pmd_cmd_result psgnoise(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3889
static enum pmd_cmd_result psgnoise_move(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3906
static enum pmd_cmd_result psgsel(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3914
static enum pmd_cmd_result panset(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3917
static void panset_main(struct pmd *pmd, struct pmd_qq *part, uint8_t pan);
#if board2
// :3961
static void calc_panout(struct pmd *pmd, struct pmd_qq *part, uint8_t *pan);
#endif
// :3972
static enum pmd_cmd_result panset_ex(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :3994
static enum pmd_cmd_result rhykey(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
#if board2
// :4045
static void rflag_inc(struct pmd *pmd, uint8_t mask, uint8_t *flags);
#endif
// :4064
static enum pmd_cmd_result rhyvs(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
#if board2
// :4077
static uint8_t *rdat_adr(struct pmd *pmd, uint8_t index);
#endif
#if board2
// :4064
static void rhyvs_write(struct pmd *pmd, uint8_t cmd, uint8_t volume);
#endif
// :4097
static enum pmd_cmd_result rhyvs_sft(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :4136
static enum pmd_cmd_result rpnset(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :4155
static enum pmd_cmd_result rmsvs(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
#if board2
// :4166
static void volset2r(struct pmd *pmd, uint8_t volume);
#endif
#if board2
// :4169
static void volset2rf(struct pmd *pmd, uint8_t volume);
#endif
// :4184
static enum pmd_cmd_result rmsvs_sft(struct pmd *pmd, struct pmd_qq *part, uint8_t **src);
// :4208
static uint8_t oshift(struct pmd_qq *part, uint8_t note);
// :4266
static void fnumset(struct pmd *pmd, struct pmd_qq *part, uint8_t note);
// :4302
static void fnumsetp(struct pmd *pmd, struct pmd_qq *part, uint8_t note);
// :4335
static void otodasi(struct pmd *pmd, struct pmd_qq *part);
// :4549
static void fm_block_calc(uint16_t *block, uint16_t *fnum);
// :4588
static void otodasip(struct pmd *pmd, struct pmd_qq *part);
// :4726
static void volset(struct pmd *pmd, struct pmd_qq *part);
// :4864
static void fmlfo_sub(uint8_t delta, uint8_t mask, uint8_t *vol);
// :4890
static void volsetp(struct pmd *pmd, struct pmd_qq *part);
// :4998
static void keyon(struct pmd *pmd, struct pmd_qq *part);
// :5039
static void keyonp(struct pmd *pmd, struct pmd_qq *part);
// :5074
static void psgmsk(struct pmd *pmd, uint8_t *mix, uint8_t *channel);
// :5091
static void keyoff(struct pmd *pmd, struct pmd_qq *part);
// :5095
static void keyoff_main(struct pmd *pmd, struct pmd_qq *part);
// :5125
static void keyoffp(struct pmd *pmd, struct pmd_qq *part);
// :5144
static void neiroset(struct pmd *pmd, struct pmd_qq *part, uint8_t voice);
// :5258
static void neiroset_tl(struct pmd_qq *part, const uint8_t *tl);
// :5274
static void toneadr_calc(struct pmd *pmd, struct pmd_qq *part, uint8_t voice, uint8_t **tone);
// :5307
static bool silence_fmpart(struct pmd *pmd, struct pmd_qq *part);
// :5341
static bool lfo(struct pmd *pmd, struct pmd_qq *part);
// :5375
static void lfo_main(struct pmd *pmd, struct pmd_qq *part);
// :5499
static void md_inc(struct pmd_qq *part);
// :5548
static uint16_t rnd(struct pmd *pmd, uint16_t range);
// :5568
static uint8_t lfoinitp(struct pmd *pmd, struct pmd_qq *part, uint8_t note);
// :5645
static uint8_t lfoinit(struct pmd *pmd, struct pmd_qq *part, uint8_t note);
// :5661
static void lfo_exit_common(struct pmd *pmd, struct pmd_qq *part);
// :5684
static void lfin1(struct pmd *pmd, struct pmd_qq *part);
// :5737
static void lfoinit_main(struct pmd_qq *part);
// :5761
static bool soft_env(struct pmd *pmd, struct pmd_qq *part);
// :5778
static bool soft_env_main(struct pmd_qq *part);
// :5789
static void soft_env_sub(struct pmd_qq *part, uint8_t *value);
// :5835
static bool ext_ssgenv_main(struct pmd_qq *part);
// :5849
static void esm_sub(struct pmd_qq *part, uint8_t *count, uint8_t *value);
// :5967
static void fadeout(struct pmd *pmd);
// :6005
static void setint(struct pmd *pmd);
// :6040
static void silence(struct pmd *pmd);
// :6173
static void opnset44(struct pmd *pmd, uint16_t cmd);
#if board2
// :6200
static void opnset46(struct pmd *pmd, uint16_t cmd);
#endif
// :6226
static void opnset(struct pmd *pmd, uint16_t cmd);
// :6253
static uint8_t get07(struct pmd *pmd);
// :6592
static struct pmd_joy get_joy(struct pmd *pmd);
// :6741
static uint8_t *get_memo(struct pmd *pmd, uint8_t n);
// :6820
static uint8_t ff_music_main(struct pmd *pmd, uint16_t bar);
// :6902
static void maskon_all(struct pmd *pmd);
// :6935
static void maskoff_all(struct pmd *pmd);
// :6967
static void part_mask(struct pmd *pmd, uint8_t n);
// :7001
static void maskon_main(struct pmd *pmd, struct pmd_qq *part, uint8_t partb, uint8_t source);
// :7100
static void rhythm_mask(struct pmd *pmd);
// :7111
static void part_on(struct pmd *pmd, uint8_t n);
// :7135
static void maskoff_main(struct pmd *pmd, struct pmd_qq *part, uint8_t partb, uint8_t source);
// :7354
static void fm_effect_on(struct pmd *pmd, uint8_t efcnum);
// :7419
static void fm_effect_off(struct pmd *pmd);
// :7492
static void opnint(struct pmd *pmd);
// :7663
static void FM_Timer_main(struct pmd *pmd);
// :7725
static void TimerB_main(struct pmd *pmd);
// :7764
static void TimerA_main(struct pmd *pmd);
// :7803
static bool check_esc(struct pmd *pmd);
// :7808
// Reads the host's normalized key state.
static uint8_t read_key_state(struct pmd *pmd);
#if va
// :7808
static uint8_t key_port8(uint8_t state);
#endif
// :7831
static void syousetu_count(struct pmd *pmd);
// :7844
static void settempo_b(struct pmd *pmd);
// :7852
static void stb_ff(struct pmd *pmd, uint8_t tempo);
// :7864
static void rew(struct pmd *pmd);
// :7887
static bool check_grph(struct pmd *pmd, uint8_t mask);
// :8385
static void resident_exit_end(struct pmd *pmd);
// :8555
static bool _set_option(struct pmd *pmd, const struct pmd_option *table, const char *cmd);
// :8604
static void memchk_init(struct pmd *pmd);
#if !va
// :8707
static void opninit_loop(struct pmd *pmd);
#endif
// :8845
static void fm_check(struct pmd *pmd);
// :8899
static void int_init(struct pmd *pmd);
// :9083
static bool ppsdrv_check(struct pmd *pmd);
#if ppz
// :9110
static bool ppz8_check(struct pmd *pmd);
#endif
// :9136
static void opnint_start(struct pmd *pmd);
#if !va
// :9174
static void opnset_fmc(struct pmd *pmd, uint16_t port, uint16_t cmd);
#endif
// :9196
static bool port_check(struct pmd *pmd);
#if !va
// :9314
static bool check_86b(struct pmd *pmd);
#if !pcm && !ppz
// :9406
static bool check_spb(struct pmd *pmd);
#endif
// :9468
static void set_port(struct pmd *pmd, uint16_t base);
#endif
// :9511
static void put_status(struct pmd *pmd);
// :9598
static uint8_t sp_key_display(uint8_t key);
// :9715
static void put8(struct pmd *pmd, uint8_t value);
// :9726
static void p8_oneset(struct pmd *pmd, uint8_t *value, uint8_t digit, bool *printed);
// :9753
static bool resident_cut_main(struct pmd *pmd, const char **cmd, bool from_env);
// :9927
static bool set_option(struct pmd *pmd, const struct pmd_option *table, const char *cmd, bool from_env);
// :9958
static bool muslng_get(struct pmd *pmd, const char **cmd, bool from_env);
// :9967
static bool voilng_get(struct pmd *pmd, const char **cmd, bool from_env);
// :9976
static bool efclng_get(struct pmd *pmd, const char **cmd, bool from_env);
// :9985
static bool fmvd_set(struct pmd *pmd, const char **cmd, bool from_env);
// :10046
static bool keycheck(struct pmd *pmd, const char **cmd, bool from_env);
// :10067
static bool grph_special_set(struct pmd *pmd, const char **cmd, bool from_env);
// :10081
static bool rew_special_set(struct pmd *pmd, const char **cmd, bool from_env);
// :10095
static bool esc_special_set(struct pmd *pmd, const char **cmd, bool from_env);
// :10109
static bool disint_set(struct pmd *pmd, const char **cmd, bool from_env);
// :10122
static bool notshot_set(struct pmd *pmd, const char **cmd, bool from_env);
// :10135
static bool notstop_set(struct pmd *pmd, const char **cmd, bool from_env);
// :10148
static bool ppsdrv_reset(struct pmd *pmd, const char **cmd, bool from_env);
// :10164
static bool ppz_reset(struct pmd *pmd, const char **cmd, bool from_env);
// :10228
static bool waitclk_set(struct pmd *pmd, const char **cmd, bool from_env);
// :10245
static bool fftempo_set(struct pmd *pmd, const char **cmd, bool from_env);
// :10259
static bool adpcmwait_set(struct pmd *pmd, const char **cmd, bool from_env);
// :10269
static bool pcm86vol_set(struct pmd *pmd, const char **cmd, bool from_env);
// :10284
static bool portsel_set(struct pmd *pmd, const char **cmd, bool from_env);
// :10304
static bool message_clear_set(struct pmd *pmd, const char **cmd, bool from_env);
// :10314
static bool get_comline_number(const char **cmd, uint8_t *out);
// :10350
static void print_16(struct pmd *pmd, uint16_t value);
// :10366
static void p16_oneset(struct pmd *pmd, uint16_t *value, uint16_t digit, bool *printed);
// :10440
PMDC_NORETURN static bool pmderr_5(struct pmd *pmd, const char **cmd, bool from_env);
// :10444
PMDC_NORETURN static bool pmderr_6(struct pmd *pmd, const char **cmd, bool from_env);
// :10452
PMDC_NORETURN static void pmderr_main(struct pmd *pmd, const char *mes);
// :10462
static bool hz_usage(struct pmd *pmd, const char **cmd, bool from_env);
// :10467
static bool usage(struct pmd *pmd, bool from_env);
// :10485
static bool hz_set(struct pmd *pmd, const char **cmd, bool from_env);
// :10513
static void wait_check(struct pmd *pmd);
#if board2 && adpcm && !ademu
// ADRAMCHK.ASM:11
static void adpcm_ram_check(struct pmd *pmd);
// ADRAMCHK.ASM:72
static bool pcmstore(struct pmd *pmd, const uint8_t *data);
// ADRAMCHK.ASM:237
static void pcmst_exit(struct pmd *pmd);
// ADRAMCHK.ASM:284
static void pcmread(struct pmd *pmd, uint8_t *data);
// ADRAMCHK.ASM:354
static void pget(struct pmd *pmd);
// ADRAMCHK.ASM:378
static bool pcmcompare(const uint8_t *data);
#endif

// :10759
// The options accepted by pmd_resident_main.
static const struct pmd_option resident_option[] = {
    {'H', hz_usage},
    {'M', muslng_get},
    {'V', voilng_get},
    {'E', efclng_get},
    {'K', keycheck},
    {'D', fmvd_set},
    {'I', disint_set},
    {'N', notshot_set},
    {'F', notstop_set},
    {'P', ppsdrv_reset},
    {'Z', ppz_reset},
    {'W', waitclk_set},
    {'G', fftempo_set},
    {'A', adpcmwait_set},
    {'S', pcm86vol_set},
    {'O', portsel_set},
    {'R', pmderr_5},
    {'C', message_clear_set},
    {0, NULL},
};

// :10798
// The options accepted by pmd_change_main.
static const struct pmd_option status_option[] = {
    {'H', hz_usage},
    {'R', resident_cut_main},
    {'K', keycheck},
    {'D', fmvd_set},
    {'I', disint_set},
    {'N', notshot_set},
    {'F', notstop_set},
    {'P', ppsdrv_reset},
    {'Z', ppz_reset},
    {'W', waitclk_set},
    {'G', fftempo_set},
    {'A', adpcmwait_set},
    {'S', pcm86vol_set},
    {'M', pmderr_6},
    {'V', pmderr_6},
    {'E', pmderr_6},
    {'Y', pmderr_6},
    {'O', pmderr_6},
    {'C', message_clear_set},
    {0, NULL},
};

// :8080
void pmd_init(struct pmd *pmd) {
#define lengthof(arr) (sizeof((arr)) / sizeof((arr)[0]))

    // :10567
    static_assert(lengthof(default_mes_int) <= lengthof(pmd->mes_int), "mes_int is too small for default_mes_int");
    // :10578
    static_assert(lengthof(default_mes_ongen1) <= lengthof(pmd->mes_ongen1), "mes_ongen1 is too small for default_mes_ongen1");

    // :8080
    pmd->rhyvol = 0x3c;
    // :8082
    pmd->ssgefcdat = efftbl;
    // :8083 ssgefclen = efftblend - efftbl
    pmd->ssgefclen = sizeof(efftbl);
    // :8088
    pmd->TimerBflag = 0;
    pmd->TimerAflag = 0;
    pmd->int60flag = 0;
    // :8103
    pmd->pcm_access = 0;
    // :8110
    pmd->int5_flag = 0;
    pmd->port22h = 0;
    // :8122
    pmd->mstart_flag = 0;
    memset(pmd->mus_filename, 0, sizeof(pmd->mus_filename));
    // :8145
    pmd->p86_freq = 8;
    // :8147
#if board2 && pcm
    pmd->p86_freqtable = pmd->pcm_tune_data;
    memcpy(pmd->pcm_tune_data, pcm_tune_data_default, sizeof(pmd->pcm_tune_data));
#else
    pmd->p86_freqtable = NULL;
#endif
    // :8151
    pmd->adpcm_emulate = 0;

    // :8283
    for (int i = 0; i < max_part1; i++) {
        pmd->part_data_table[i] = &pmd->parts[i];
    }
    pmd->part_data_table[max_part1] = &pmd->part_e;

#if board2 && adpcm && !ademu
    // :8357
    memset(&pmd->pcm_table.adpcm_data, 0, sizeof(pmd->pcm_table.adpcm_data));
    pmd->pcm_table.adpcm_data.pcmends = 0x26;
#endif
#if board2 && pcm
    // :8363
    memset(&pmd->pcm_table.pcm86, 0, sizeof(pmd->pcm_table.pcm86));
    // PCMDRV86.ASM:1387
    pmd->play86_flag = 0;
    pmd->trans_flag = 0;
    pmd->start_ofs = 0;
    pmd->start_ofs2 = 0;
    pmd->size1 = 0;
    pmd->size2 = 0;
    pmd->_start_ofs = 0;
    pmd->_start_ofs2 = 0;
    pmd->_size1 = 0;
    pmd->_size2 = 0;
    pmd->addsize1 = 0;
    pmd->addsize2 = 0;
    pmd->addsizew = 0;
    pmd->repeat_ofs = 0;
    pmd->repeat_ofs2 = 0;
    pmd->repeat_size1 = 0;
    pmd->repeat_size2 = 0;
    pmd->release_ofs = 0;
    pmd->release_ofs2 = 0;
    pmd->release_size1 = 0;
    pmd->release_size2 = 0;
    pmd->repeat_flag = 0;
    pmd->release_flag1 = 0;
    pmd->release_flag2 = 0;
    pmd->pcm86_pan_flag = 0;
    pmd->pcm86_pan_dat = 0;
#endif

    // :8481
    // Option defaults.
    pmd->mmldat_lng = mdata_def;
    pmd->voicedat_lng = voice_def;
    pmd->effecdat_lng = effect_def;
    pmd->key_check = key_def;
    pmd->fm_voldown = fmvd_init;
    pmd->_fm_voldown = fmvd_init;
    pmd->ssg_voldown = 0;
    pmd->_ssg_voldown = 0;
    pmd->pcm_voldown = 0;
    pmd->_pcm_voldown = 0;
    pmd->ppz_voldown = 0;
    pmd->_ppz_voldown = 0;
    pmd->rhythm_voldown = 0;
    pmd->_rhythm_voldown = 0;
    pmd->kp_rhythm_flag = -1;

    // :8497
    pmd->rshot_bd = 0;
    pmd->rshot_sd = 0;
    pmd->rshot_sym = 0;
    pmd->rshot_hh = 0;
    pmd->rshot_tom = 0;
    pmd->rshot_rim = 0;
    pmd->rdump_bd = 0;
    pmd->rdump_sd = 0;
    pmd->rdump_sym = 0;
    pmd->rdump_hh = 0;
    pmd->rdump_tom = 0;
    pmd->rdump_rim = 0;

    // :8501
    memset(pmd->parts, 0, max_part1 * sizeof(pmd->parts[0]));

    // :8505
    pmd->disint = 0;
    pmd->rescut_cant = 0;
    pmd->adpcm_wait = 0;
    pmd->pcm86_vol = 0;
    pmd->_pcm86_vol = 0;
    // :8510
    pmd->fade_stop_flag = 1;
    pmd->ppsdrv_flag = -1;
    // :8512
#if va
    pmd->grph_sp_key = 0x80;
    pmd->rew_sp_key = 0x40;
    pmd->esc_sp_key = 0x80;
#else
    pmd->grph_sp_key = 0x10;
    pmd->rew_sp_key = 0x01;
    pmd->esc_sp_key = 0x10;
    pmd->port_sel = -1;
#endif
    // :8522
    pmd->ff_tempo = 250;
    pmd->music_flag = 0;
    pmd->message_flag = 1;

    // :8639
    // Module, voice, and effect data area layout.
    pmd->mmlbuf = pmd->dataarea + 1;
    memcpy(pmd->dataarea, empty_dataarea, sizeof(empty_dataarea));
    pmd->tondat = pmd->dataarea + pmd->mmldat_lng * 1024;
    pmd->efcdat = pmd->tondat + pmd->voicedat_lng * 1024;

    // :8663
    pmd->fmint = (struct pmd_int_handler){0};
    pmd->efcint = (struct pmd_int_handler){0};
    pmd->intfook_flag = 0;
    pmd->skip_flag = 0;
    pmd->effon = 0;
    // :8063 The effect driver starts enabled. The original relies on the
    // resident image being zeroed here.
    pmd->effflag = 0;
    pmd->effadr = NULL;
    pmd->effcnt = 0;
    pmd->eswthz = 0;
    pmd->eswtst = 0;
    pmd->eswnhz = 0;
    pmd->eswnst = 0;
    pmd->eswnct = 0;
    pmd->hosei_flag = 0;
    pmd->last_shot_data = 0;
    pmd->fm_effec_flag = 0;
    pmd->pcmflag = 0;
    // :8673
    pmd->psgefcnum = -1;
    pmd->fm_effec_num = -1;
    pmd->pcm_effec_num = -1;

    // :10567
    memcpy(pmd->mes_int, default_mes_int, sizeof(default_mes_int));
    // :10578
    memcpy(pmd->mes_ongen1, default_mes_ongen1, sizeof(default_mes_ongen1));

#undef lengthof
}

// :151
PMDC_NORETURN static void error_exit(struct pmd *pmd, int status) {
    pmd->sys->exit(status, pmd->user_data);
}

// :173
static void print_mes(struct pmd *pmd, const char *mes) {
    pmd->sys->print(mes, pmd->user_data);
}

// The delay loops are calibrated CPU loops (WAIT.INC) that are intentionally
// not implemented and remain no-delay points.
// :218
static void _wait(struct pmd *pmd) {
    (void)pmd;
    // NOTE: WAIT.INC wait_clock calibration intentionally not implemented.
}

// :223
static void _waitP(struct pmd *pmd) {
    (void)pmd;
    // NOTE: WAIT.INC wait_clock calibration intentionally not implemented.
}

#if board2
// :230
static void _rwait(struct pmd *pmd) {
    (void)pmd;
    // NOTE: WAIT.INC wait_clock calibration intentionally not implemented.
}
#endif

// :242
// Discards one status read, then waits for the BUSY bit to clear. The original
// returns the final read, which opnint's timer checks inspect; callers that
// write a register discard it.
static uint8_t rdychk(struct pmd *pmd, uint16_t port) {
    uint8_t status;
    (void)inb(pmd, port);
    while ((status = inb(pmd, port)) & 0x80) ;
    return status;
}

// C-only helpers shared by the sequencer.
//
// These have no labels of their own in the original. The line numbers mark the
// first lodsb/lodsw use and the FM3 check they abstract; the group stays at the
// top of the block because it has no single position in the source order.

// :308
// int60_main counts every INT 60h call; int60_start then rejects the ones the
// reint_chk table excludes for the running timer. The no-board path
// (int60_start_not_board) skips that check, as in the original. Only entries
// whose class is non-empty can be rejected, so the callers of those discard
// the result.
static bool int60_enter(struct pmd *pmd, uint8_t entry) {
    pmd->int60flag++;
    if (pmd->board == 0) return true;
    uint8_t mask = reint_chk[entry];
    if ((mask & 1) != 0 && pmd->TimerBflag != 0) return false;
    if ((mask & 2) != 0 && pmd->TimerAflag != 0) return false;
    if ((mask & 4) != 0 && pmd->int60flag != 1) return false;
    return true;
}

// :315
static void int60_exit(struct pmd *pmd) { pmd->int60flag--; }

#if board2
// PCMDRV.ASM:7, PCMDRVE.ASM:7, PCMDRV86.ASM:7
// The PCM part main loop.
static void pcmmain(struct pmd *pmd, struct pmd_qq *part) {
    uint8_t *src = part->address;
    if (src == NULL) return;

    if (part->partmask != 0) goto pcmmain_nonplay;

    part->leng--;
    uint8_t remaining = part->leng;
    uint8_t cmd;

    // PCMDRV.ASM:18, PCMDRVE.ASM:18, PCMDRV86.ASM:18
    // Keyoff when the remaining length reaches Q.
    if ((part->keyoff_flag & 3) == 0 && remaining <= part->qdat) {
        part->keyoff_flag = 0xff;
        keyoffm(pmd, part);
    }
    if (remaining != 0) goto mpexitm;

#if !pcm
    // PCMDRV.ASM:29, PCMDRVE.ASM:29
    part->lfoswi &= 0xf7; // portamento off
#endif

mp1m:
    cmd = lodsb(&src);
    if (cmd < 0x80) goto mp2m;
    if (cmd == 0x80) goto mp15m;

    switch (commandsm(pmd, part, &src)) {
    case PMD_CMD_MP1M: goto mp1m;
    case PMD_CMD_PCMMNP_1: goto pcmmnp_1;
    case PMD_CMD_PORTA_RETURNM: goto porta_returnm;
    default: break;
    }
    goto mp1m;

mp15m:
    // PCMDRV.ASM:42, PCMDRVE.ASM:42, PCMDRV86.ASM:42
    // End of music, or a loop if "L" was seen.
    src--;
    part->address = src;
    part->loopcheck = 3;
    part->onkai = 0xff;
    if (part->partloop == NULL) goto mpexitm;
    src = part->partloop;
    part->loopcheck = 1;
    goto mp1m;

mp2m:
    // PCMDRV.ASM:55, PCMDRVE.ASM:55, PCMDRV86.ASM:55
    // Note: set the pitch and gate.
    cmd = lfoinitp(pmd, part, cmd);
    cmd = oshift(part, cmd);
    fnumsetm(pmd, part, cmd);

    cmd = lodsb(&src);
    part->leng = cmd;
    calc_q(pmd, part, &src);

porta_returnm:
    // PCMDRV.ASM:64, PCMDRVE.ASM:64, PCMDRV86.ASM:64
    if (part->volpush == 0) goto mp_newm;
    if (part->onkai == 0xff) goto mp_newm;
    pmd->volpush_flag--;
    if (pmd->volpush_flag == 0) goto mp_newm;
    pmd->volpush_flag = 0;
    part->volpush = 0;

mp_newm:
    volsetm(pmd, part);
    otodasim(pmd, part);
    if ((part->keyoff_flag & 1) != 0) keyonm(pmd, part);
    part->keyon_flag++;

    part->address = src;
    pmd->tieflag = 0;
    pmd->volpush_flag = 0;
    part->keyoff_flag = 0;
    if (*src == 0xfb) part->keyoff_flag = 2; // '&' immediately follows
    mnp_ret(pmd, part);
    return;

mpexitm:;
#if pcm
    // PCMDRV86.ASM:88
    uint8_t lfosw = part->lfoswi;
    if ((lfosw & 0x22) != 0) {
        pmd->lfo_switch = 0;
        if ((lfosw & 2) != 0) {
            (void)lfo(pmd, part);
            pmd->lfo_switch = (uint8_t)(lfosw & 2);
        }
        if ((lfosw & 0x20) != 0) {
            lfo_change(part);
            if (lfo(pmd, part)) {
                lfo_change(part);
                pmd->lfo_switch |= part->lfoswi & 0x20;
            } else {
                lfo_change(part);
            }
        }
        if (!soft_env(pmd, part) && (pmd->lfo_switch & 0x22) == 0 &&
            pmd->fadeout_speed == 0) {
            mnp_ret(pmd, part);
            return;
        }
        volsetm(pmd, part);
        mnp_ret(pmd, part);
        return;
    }
    if (!soft_env(pmd, part)) {
        if (pmd->fadeout_speed == 0) {
            mnp_ret(pmd, part);
            return;
        }
    }
    volsetm(pmd, part);
    mnp_ret(pmd, part);
    return;
#else
    // PCMDRV.ASM:89, PCMDRVE.ASM:89
    // The YM2608 drivers process the tone and volume LFOs and portamento.
    uint8_t lfosw = part->lfoswi;
    pmd->lfo_switch = (uint8_t)(lfosw & 8);
    if (lfosw != 0) {
        if ((lfosw & 3) != 0) {
            if (lfo(pmd, part)) pmd->lfo_switch |= lfosw & 3;
        }
        if ((lfosw & 0x30) != 0) {
            lfo_change(part);
            if (lfo(pmd, part)) {
                lfo_change(part);
                pmd->lfo_switch |= part->lfoswi & 0x30;
            } else {
                lfo_change(part);
            }
        }
        if ((pmd->lfo_switch & 0x19) != 0) {
            if ((pmd->lfo_switch & 8) != 0) porta_calc(pmd, part);
            otodasim(pmd, part);
        }
    }
    // PCMDRV:129, PCMDRVE.ASM:129 volsm
    if (!soft_env(pmd, part) && (pmd->lfo_switch & 0x22) == 0 &&
        pmd->fadeout_speed == 0) {
        mnp_ret(pmd, part);
        return;
    }
    volsetm(pmd, part);
    mnp_ret(pmd, part);
    return;
#endif

pcmmain_nonplay:;
#if pcm
    // PCMDRV86.ASM:134
    part->leng--;
    if (part->leng != 0) {
        mnp_ret(pmd, part);
        return;
    }
    if ((part->partmask & 2) != 0) {
        if (pmd->play86_flag == 1) goto pcmmnp_1; // the PCM effect is sounding
        pmd->pcmflag = 0;
        pmd->pcm_effec_num = 0xff;
        part->partmask &= 0xfd;
        if (part->partmask != 0) goto pcmmnp_1;
        neiro_set(pmd, part->voicenum);
        set_pcm_pan(pmd, part, part->fmpan, pmd->revpan);
        goto mp1m; // the part is revived
    }
#elif ademu
    // PCMDRVE.ASM:142
    part->keyoff_flag = 0xff;
    part->leng--;
    if (part->leng != 0) {
        mnp_ret(pmd, part);
        return;
    }
#else
    // PCMDRV.ASM:142
    part->keyoff_flag = 0xff;
    part->leng--;
    if (part->leng != 0) {
        mnp_ret(pmd, part);
        return;
    }
    if ((part->partmask & 2) != 0) {
        // PCMDRV.ASM:149
        // EOS check: the interrupt PCM has finished when bit 2 of the FM2 data
        // port is set.
        uint8_t eos = inb(pmd, pmd->fm2_port1);
        if ((eos & 0x04) == 0) goto pcmmnp_1; // still sounding
        pmd->pcmflag = 0;
        pmd->pcm_effec_num = 0xff;
        part->partmask &= 0xfd;
        if (part->partmask != 0) goto pcmmnp_1;
        // PCMDRV.ASM:316 revives at mp1m, past mp1m0's portamento clear.
        goto mp1m; // the part is revived
    }
#endif

pcmmnp_1:
    cmd = lodsb(&src);
    if (cmd == 0x80) goto pcmmnp_2;
    if (cmd < 0x80) goto pcmmnp_3;
    switch (commandsm(pmd, part, &src)) {
    case PMD_CMD_MP1M: goto mp1m;
    case PMD_CMD_PCMMNP_1: goto pcmmnp_1;
    case PMD_CMD_PORTA_RETURNM: goto porta_returnm;
    default: break;
    }
    goto pcmmnp_1;

pcmmnp_2:
    // PCMDRV.ASM:167, PCMDRVE.ASM:167, PCMDRV86.ASM:167
    // End of music, or a loop if "L" was seen.
    src--;
    part->address = src;
    part->loopcheck = 3;
    part->onkai = 0xff;
    if (part->partloop == NULL) {
        fmmnp_4(pmd, part);
        return;
    }
    src = part->partloop;
    part->loopcheck = 1;
    goto pcmmnp_1;

pcmmnp_3:
    // PCMDRV.ASM:163, PCMDRVE.ASM:151, PCMDRV86.ASM:173 A rest goes through
    // the shared rest-note path.
    rest_note(pmd, part, &src);
}
#endif

#if board2 && (pcm || adpcm)
// PCMDRV.ASM:185, PCMDRVE.ASM:173, PCMDRV86.ASM:180
static enum pmd_cmd_result commandsm(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    return command00(pmd, part, src, cmdtblm);
}

// PCMDRV.ASM:188, PCMDRVE.ASM:176, PCMDRV86.ASM:183
// The PCM command table, indexed by 0xff - command. The YM2608 ADPCM drivers
// accept the extended lfo commands; the PCM86 table replaces them with
// parameter skips (PCMDRV86.ASM:263-274) and stops at 0b2h.
static const pmd_cmd_fn cmdtblm[0xff - 0xb1 + 1] = {
    com_at_m,       // ff '@'
    comq,           // fe 'q'
    comv,           // fd 'v'
    comt,           // fc
    comtie,         // fb '&'
    comd,           // fa 'D'
    comstloop,      // f9 '['
    comedloop,      // f8 ']'
    comexloop,      // f7 ':'
    comlopset,      // f6 'L'
    comshift,       // f5 '_'
    comvolupm,      // f4 ')' (PPZ8), ']' (PCM)
    comvoldownm,    // f3 '(' / '['
    lfoset,         // f2
    lfoswitch,      // f1
    psgenvset,      // f0
    comy,           // ef 'y'
    jump1,          // ee
    jump1,          // ed
    pansetm,        // ec 'p'
    rhykey,         // eb
    rhyvs,          // ea
    rpnset,         // e9
    rmsvs,          // e8
    comshift2,      // e7
    rmsvs_sft,      // e6
    rhyvs_sft,      // e5
    jump1,          // e4
    comvolupm2,     // e3
    comvoldownm2,   // e2
    jump1,          // e1
    jump1,          // e0
    syousetu_lng_set, // df
    vol_one_up_pcm, // de
    vol_one_down,   // dd
    status_write,   // dc
    status_add,     // db
#if pcm
    jump1,          // da (the PCM86 driver has no portamento)
#else
    portam,         // da
#endif
    jump1,          // d9
    jump1,          // d8
    jump1,          // d7
    mdepth_set,     // d6
    comdd,          // d5
    ssg_efct_set,   // d4
    fm_efct_set,    // d3
    fade_set,       // d2
    jump1,          // d1
    jump1,          // d0
    jump1,          // cf
    pcmrepeat_set,  // ce
    extend_psgenvset, // cd
    jump1,          // cc
    lfowave_set,    // cb
    lfo_extend,     // ca
    envelope_extend, // c9
    jump3,          // c8
    jump3,          // c7
    jump6,          // c6
    jump1,          // c5
    comq2,          // c4
    pansetm_ex,     // c3
    lfoset_delay,   // c2
    jump0,          // c1 (slur)
    pcm_mml_part_mask, // c0
#if pcm
    jump4,          // bf
    jump1,          // be
    jump2,          // bd
    jump1,          // bc
    jump1,          // bb
    jump1,          // ba
    jump1,          // b9
#else
    _lfoset,        // bf
    _lfoswitch,     // be
    _mdepth_set,    // bd
    _lfowave_set,   // bc
    _lfo_extend,    // bb
    _volmask_set,   // ba
    _lfoset_delay,  // b9
#endif
    jump2,          // b8
    mdepth_count,   // b7
    jump1,          // b6
    jump2,          // b5
#if ppz
    ppz_extpartset, // b4 (PPZDRV.ASM:287)
#else
    jump16,         // b4
#endif
    comq3,          // b3
    comshift_master, // b2
    // PCMDRV86.ASM:276 has no b1 entry; the shared one keeps the table in
    // range, and command00 only clamps commands below b1.
    comq4,          // b1
};

// PCMDRV.ASM:291, PCMDRVE.ASM:279, PCMDRV86.ASM:281
static enum pmd_cmd_result pcm_mml_part_mask(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint8_t mask = lodsb(src);
    if (mask >= 2) return special_0c0h(pmd, part, src, mask);
    if (mask == 0) {
        part->partmask &= 0xbf;
        if (part->partmask != 0) return PMD_CMD_PCMMNP_1;
        return PMD_CMD_MP1M; // the part is revived
    }

    part->partmask |= 0x40;
    if (part->partmask != 0x40) return PMD_CMD_PCMMNP_1;
#if pcm
    stop_86pcm(pmd); // PCMDRV86.ASM:290
#elif ademu
    // PCMDRVE.ASM:288
    if (pmd->adpcm_emulate == 1) {
        const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
        if (drv != NULL) drv->keyoff(7, pmd->user_data);
    }
#else
    // PCMDRV.ASM:300
    opnset46(pmd, 0x0102); // PAN=0, x8 bit mode
    opnset46(pmd, 0x0001); // PCM reset
#endif
    return PMD_CMD_PCMMNP_1;
}

// PCMDRV.ASM:317, PCMDRVE.ASM:305, PCMDRV86.ASM:304
// Sets the PCM repeat range (0ceh).
static enum pmd_cmd_result pcmrepeat_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)part;
#if pcm
    // PCMDRV86.ASM:304 `whole` is the original voice size (the original pushes
    // DX:CX) and `running` the working copy it adjusts while reading the
    // parameters.
    uint32_t whole = ((uint32_t)pmd->_size2 << 16) | pmd->_size1;
    uint32_t running = whole;
    uint32_t size = whole;
    uint32_t repeat_size = whole;
    uint32_t repeat_ofs = ((uint32_t)pmd->_start_ofs2 << 16) | pmd->_start_ofs;
    pmd->repeat_flag = 1;
    pmd->release_flag1 = 0;

    // PCMDRV86.ASM:320 The first value is the repeat start.
    uint16_t value = lodsw(src);
    if ((int16_t)value < 0) {
        value = pcm86vol_chk(pmd, (uint16_t)(0u - value));
        repeat_size = value;
        running = (uint32_t)(whole - value);
        repeat_ofs += running;
    } else {
        value = pcm86vol_chk(pmd, value);
        repeat_size = (uint32_t)(whole - value);
        repeat_ofs += value;
    }

    // PCMDRV86.ASM:345 The second value is the repeat end.
    value = lodsw(src);
    if (value != 0) {
        if ((int16_t)value < 0) {
            value = pcm86vol_chk(pmd, (uint16_t)(0u - value));
            repeat_size = (uint32_t)(repeat_size - value);
            size = (uint32_t)(size - value);
        } else {
            value = pcm86vol_chk(pmd, value);
            size = value;
            running = (uint32_t)(running - value);
            repeat_size = (uint32_t)(repeat_size - running);
        }
    }
    pmd->repeat_size1 = (uint16_t)repeat_size;
    pmd->repeat_size2 = (uint16_t)(repeat_size >> 16);
    pmd->repeat_ofs = (uint16_t)repeat_ofs;
    pmd->repeat_ofs2 = (uint16_t)(repeat_ofs >> 16);
    pmd->_size1 = (uint16_t)size;
    pmd->_size2 = (uint16_t)(size >> 16);

    // PCMDRV86.ASM:371 The third value is the release start; the original
    // restores the pushed whole size for it.
    value = lodsw(src);
    if (value == 0x8000) return PMD_CMD_RETURN; // no release
    uint32_t release_size = whole;
    uint32_t release_ofs = ((uint32_t)pmd->_start_ofs2 << 16) | pmd->_start_ofs;
    pmd->release_flag1 = 1;
    if (value >= 0x8000) {
        value = pcm86vol_chk(pmd, (uint16_t)(0u - value));
        release_size = value;
        running = (uint32_t)(whole - value);
        release_ofs += running;
    } else {
        value = pcm86vol_chk(pmd, value);
        release_size = (uint32_t)(release_size - value);
        release_ofs += value;
    }
    pmd->release_size1 = (uint16_t)release_size;
    pmd->release_size2 = (uint16_t)(release_size >> 16);
    pmd->release_ofs = (uint16_t)release_ofs;
    pmd->release_ofs2 = (uint16_t)(release_ofs >> 16);
#elif ademu
    // PCMDRVE.ASM:305 The loop offsets are in 1/64ths of the voice's size.
    uint16_t size = 0;
    part->voicenum |= 0x80;
    {
        const struct pmd_ppz8_voice *voice = ppz_voicetable_calc(pmd, part);
        part->voicenum &= 0x7f;
        if (voice != NULL) size = (uint16_t)(voice->size >> 6);
    }

    uint16_t value = lodsw(src);
    uint16_t start = (int16_t)value < 0 ? (uint16_t)(value + size) : value;
    value = lodsw(src);
    uint16_t end = (value == 0 || (int16_t)value < 0) ? (uint16_t)(value + size) : value;
    (void)lodsw(src); // the release value cannot be set

    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    if (drv != NULL) {
        drv->set_loop(7, (uint32_t)start * 32, (uint32_t)end * 32, pmd->user_data);
    }
#else
    // PCMDRV.ASM:317 The offsets are relative to the voice's start or stop.
    uint16_t value = lodsw(src);
    if ((int16_t)value < 0) value = (uint16_t)(value + pmd->pcmstop);
    else value = (uint16_t)(value + pmd->pcmstart);
    pmd->pcmrepeat1 = value;

    value = lodsw(src);
    if (value == 0 || (int16_t)value < 0) value = (uint16_t)(value + pmd->pcmstop);
    else value = (uint16_t)(value + pmd->pcmstart);
    pmd->pcmrepeat2 = value;

    value = lodsw(src);
    // PCMDRV.ASM:340 A third value of 8000h disables the release but is still
    // stored.
    if (value != 0x8000) {
        if ((int16_t)value < 0) value = (uint16_t)(value + pmd->pcmstop);
        else value = (uint16_t)(value + pmd->pcmstart);
    }
    pmd->pcmrelease = value;
#endif
    return PMD_CMD_RETURN;
}

// PCMDRV.ASM:355, PCMDRVE.ASM:359
// Portamento for the PCM parts.
#if !pcm
static enum pmd_cmd_result portam(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    if (part->partmask != 0) {
        (*src)++; // the first pitch byte is skipped while masked
        return PMD_CMD_RETURN;
    }

    uint8_t note = lodsb(src);
    note = lfoinitp(pmd, part, note);
    note = oshift(part, note);
    fnumsetm(pmd, part, note);

    uint16_t from_fnum = part->fnum;
    uint8_t from_note = part->onkai;

    note = lodsb(src);
    note = oshift(part, note);
    fnumsetm(pmd, part, note);
    uint16_t to_fnum = part->fnum;

    part->onkai = from_note;
    part->fnum = from_fnum;
    to_fnum = (uint16_t)(to_fnum - from_fnum);

    part->leng = lodsb(src);
    calc_q(pmd, part, src);

    int16_t step = (int16_t)to_fnum;
    part->porta_num2 = (int16_t)(step / (int16_t)part->leng);
    part->porta_num3 = (int16_t)(step % (int16_t)part->leng);
    part->lfoswi |= 8; // portamento on
    return PMD_CMD_PORTA_RETURNM;
}
#endif

// PCMDRV.ASM:401, PCMDRVE.ASM:405, PCMDRV86.ASM:426
static enum pmd_cmd_result comvolupm(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    (void)src;
    unsigned sum = part->volume + 16;
    part->volume = sum > 0xff ? 255 : (uint8_t)sum;
    return PMD_CMD_RETURN;
}

// PCMDRV.ASM:411, PCMDRVE.ASM:415, PCMDRV86.ASM:436
static enum pmd_cmd_result comvolupm2(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    unsigned sum = lodsb(src) + part->volume;
    part->volume = sum > 0xff ? 255 : (uint8_t)sum;
    return PMD_CMD_RETURN;
}

// PCMDRV.ASM:418, PCMDRVE.ASM:422, PCMDRV86.ASM:444
static enum pmd_cmd_result comvoldownm(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    (void)src;
    part->volume = part->volume < 16 ? 0 : (uint8_t)(part->volume - 16);
    return PMD_CMD_RETURN;
}

// PCMDRV.ASM:425, PCMDRVE.ASM:429, PCMDRV86.ASM:451
static enum pmd_cmd_result comvoldownm2(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    uint8_t amount = lodsb(src);
    part->volume = part->volume < amount ? 0 : (uint8_t)(part->volume - amount);
    return PMD_CMD_RETURN;
}

#if ademu
// PPZDRV.ASM:851, PCMDRVE.ASM:460 The PPZ8 pan values for p0-p3.
static const uint8_t pcm_pan_data[] = {0, 9, 1, 5};
#endif

// PCMDRV.ASM:437, PCMDRVE.ASM:441, PCMDRV86.ASM:467
// Panning for the PCM parts. The YM2608 stores the pan in the top two bits of
// register B4h, PPZ8's emulation uses a 0-9 value, and the PCM86 a 0..127
// balance with a reversed-phase flag.
static enum pmd_cmd_result pansetm(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint8_t sel = lodsb(src);
#if pcm
    // PCMDRV86.ASM:467 p0 reversed phase, p1 right, p2 left, p3 centre.
    uint8_t pan = 0;
    uint8_t rev = 0;
    if (sel == 1) {
        pan = 0x7f;
    } else if (sel == 2) {
        pan = 0x80;
    } else if (sel != 3) {
        rev = 1;
    }
    set_pcm_pan(pmd, part, pan, rev);
#elif ademu
    // PCMDRVE.ASM:441 The value is masked and rotated into bits 6-7.
    sel &= 3;
    part->fmpan = (uint8_t)(sel << 6);
    // The pan value goes straight to PPZ8 unless the ADPCM emulation already
    // took channel 7.
    uint8_t ppz_pan = sel;
    if (pmd->adpcm_emulate != 1) ppz_pan = pcm_pan_data[sel];
    {
        const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
        if (drv != NULL) drv->set_pan(7, ppz_pan, pmd->user_data);
    }
#else
    // PCMDRV.ASM:441 rotates first, so the mask keeps bits 6-7 only.
    sel = (uint8_t)(((sel << 6) | (sel >> 2)) & 0xc0);
    part->fmpan = sel;
    (void)pmd;
#endif
    return PMD_CMD_RETURN;
}

// PCMDRV.ASM:449, PCMDRVE.ASM:465, PCMDRV86.ASM:491
// Pan setting extend: px -127..+127 plus the reversed-phase flag (0c3h).
static enum pmd_cmd_result pansetm_ex(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint8_t pan = lodsb(src);
#if pcm
    set_pcm_pan(pmd, part, pan, *(*src)++);
#else
    (*src)++; // the reversed-phase flag is skipped
    // PCMDRV.ASM:449 The extended value is reduced to left, right, or centre.
    if (pan == 0) pan = 3;
    else if ((int8_t)pan < 0) pan = 1;
    else pan = 2;
    pan &= 3;
    part->fmpan = (uint8_t)(pan << 6);
#if ademu
    uint8_t ppz_pan = pan;
    if (pmd->adpcm_emulate != 1) ppz_pan = pcm_pan_data[pan];
    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    if (drv != NULL) drv->set_pan(7, ppz_pan, pmd->user_data);
#else
    (void)pmd;
#endif
#endif
    return PMD_CMD_RETURN;
}

#if pcm
// PCMDRV86.ASM:495
// Sets the PCM86 pan from a signed 0..127 balance plus the reversed-phase
// flag.
static void set_pcm_pan(struct pmd *pmd, struct pmd_qq *part, uint8_t pan, uint8_t rev) {
    part->fmpan = pan;
    pmd->revpan = rev;
    set_pcm_pan2(pmd, pan, rev);
}

// PCMDRV86.ASM:498
// The balance half of set_pcm_pan, which the PCM effect also uses directly.
static void set_pcm_pan2(struct pmd *pmd, uint8_t pan, uint8_t rev) {
    if ((int8_t)pan < 0) {
        pmd->pcm86_pan_flag = 1; // left
        pan = (uint8_t)((pan + 0x80) & 0x7f);
    } else if (pan == 0) {
        pmd->pcm86_pan_flag = 3; // middle
    } else {
        pmd->pcm86_pan_flag = 2; // right
        pan = (uint8_t)(~pan & 0x7f);
    }
    pmd->pcm86_pan_dat = pan;
    if ((rev & 1) != 0) pmd->pcm86_pan_flag |= 4; // reversed phase
}
#endif

// PCMDRV.ASM:467, PCMDRVE.ASM:483, PCMDRV86.ASM:531
// Voice change for the PCM parts.
static enum pmd_cmd_result com_at_m(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint8_t voice = lodsb(src);
#if pcm
    part->voicenum = voice;
    neiro_set(pmd, voice);
#elif ademu
    part->voicenum = voice;
    if (pmd->adpcm_emulate == 1) return PMD_CMD_RETURN;
    pmd->adpcm_emulate = 1;
    {
        const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
        if (drv != NULL) drv->set_adpcm_emulation(true, pmd->user_data);
    }
#if ppz
    part->voicenum |= 0x80;
    pmd->partb = 7; // the PCM part's PPZ8 channel, left as-is by the original
    ppz_neiro_reset(pmd, part);
    part->voicenum &= 0x7f;

    pmd->parts[part10h].partmask |= 0x10;
    part->partmask &= 0xef;
    if (part->partmask == 0) return PMD_CMD_MP1M; // the part is revived
#endif
#else
    part->voicenum = voice;
    {
        const struct pmd_pcm_adpcm_voice *v = &pmd->pcm_table.adpcm_data.pcmadrs[voice];
        pmd->pcmstart = v->start;
        pmd->pcmstop = v->stop;
    }
    pmd->pcmrepeat1 = 0;
    pmd->pcmrepeat2 = 0;
    pmd->pcmrelease = 0x8000;
#endif
    return PMD_CMD_RETURN;
}

// PCMDRV.ASM:489, PCMDRVE.ASM:512, PCMDRV86.ASM:564
static void volsetm(struct pmd *pmd, struct pmd_qq *part) {
#if pcm
    uint8_t volume = pcm_vol_calc(pmd, part, pmd->pcm_voldown, false);
    pcm86_vol_out(pmd, volume);
#elif ademu
    // PCMDRVE.ASM:604 squares the fadeout like PCMDRV.ASM:514.
    uint8_t volume = pcm_vol_calc(pmd, part, pmd->pcm_voldown, true);
    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    if (drv != NULL) drv->set_volume(7, volume, pmd->user_data);
#else
    // PCMDRV.ASM:596 The YM2608 ADPCM volume write, with PCMDRV.ASM:514's
    // squared fadeout.
    uint8_t volume = pcm_vol_calc(pmd, part, pmd->pcm_voldown, true);
    opnset46(pmd, (uint16_t)((0x0b << 8) | volume));
#endif
}

// PCMDRV.ASM:606, PCMDRVE.ASM:630, PCMDRV86.ASM:707
static void keyonm(struct pmd *pmd, struct pmd_qq *part) {
    if (part->onkai == 0xff) return; // a rest
#if pcm
    play_86pcm(pmd);
#elif ademu
    // PCMDRVE.ASM:640 Voice with bit 15 set (the PPZ8 bank select).
    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    if (drv != NULL) {
        drv->keyon(7, (uint16_t)(0x8080 | part->voicenum), pmd->user_data);
    }
#else
    opnset46(pmd, 0x0102); // PAN=0, x8 bit mode
    opnset46(pmd, 0x0021); // PCM reset
    opnset46(pmd, (uint16_t)((0x02 << 8) | (uint8_t)pmd->pcmstart));
    opnset46(pmd, (uint16_t)((0x03 << 8) | (uint8_t)(pmd->pcmstart >> 8)));
    opnset46(pmd, (uint16_t)((0x04 << 8) | (uint8_t)pmd->pcmstop));
    opnset46(pmd, (uint16_t)((0x05 << 8) | (uint8_t)(pmd->pcmstop >> 8)));

    if ((pmd->pcmrepeat1 | pmd->pcmrepeat2) == 0) {
        opnset46(pmd, 0x00a0); // play once
        opnset46(pmd, (uint16_t)((0x01 << 8) | (uint8_t)(part->fmpan | 2)));
        return;
    }

    opnset46(pmd, 0x00b0); // repeat
    opnset46(pmd, (uint16_t)((0x01 << 8) | (uint8_t)(part->fmpan | 2)));
    opnset46(pmd, (uint16_t)((0x02 << 8) | (uint8_t)pmd->pcmrepeat1));
    opnset46(pmd, (uint16_t)((0x03 << 8) | (uint8_t)(pmd->pcmrepeat1 >> 8)));
    opnset46(pmd, (uint16_t)((0x04 << 8) | (uint8_t)pmd->pcmrepeat2));
    opnset46(pmd, (uint16_t)((0x05 << 8) | (uint8_t)(pmd->pcmrepeat2 >> 8)));
#endif
}

// PCMDRV.ASM:676, PCMDRVE.ASM:650, PCMDRV86.ASM:722
static void keyoffm(struct pmd *pmd, struct pmd_qq *part) {
#if pcm
    if (pmd->release_flag1 == 1) {
        // PCMDRV86.ASM:723 The release range replaces the playback range.
        pmd->start_ofs = pmd->release_ofs;
        pmd->start_ofs2 = pmd->release_ofs2;
        pmd->size1 = pmd->release_size1;
        pmd->size2 = pmd->release_size2;
        pmd->release_flag2 = 1;
    }
#endif
    if (part->envf == 0xff) {
        if (part->eenv_count == 4) return;
    } else if (part->envf == 2) {
        return;
    }
#if pcm || ademu
    keyoffp(pmd, part);
#else
    // PCMDRV.ASM:687 An explicit release address restarts the ADPCM at it.
    if (pmd->pcmrelease == 0x8000) {
        keyoffp(pmd, part);
        return;
    }
    opnset46(pmd, 0x0021); // PCM reset
    opnset46(pmd, (uint16_t)((0x02 << 8) | (uint8_t)pmd->pcmrelease));
    opnset46(pmd, (uint16_t)((0x03 << 8) | (uint8_t)(pmd->pcmrelease >> 8)));
    opnset46(pmd, (uint16_t)((0x04 << 8) | (uint8_t)pmd->pcmstop));
    opnset46(pmd, (uint16_t)((0x05 << 8) | (uint8_t)(pmd->pcmstop >> 8)));
    opnset46(pmd, 0x00a0); // play once
    keyoffp(pmd, part);
#endif
}

// PCMDRV.ASM:718, PCMDRVE.ASM:665, PCMDRV86.ASM:752
// Sets the PCM pitch from the fnum, portamento, LFO, and detune.
static void otodasim(struct pmd *pmd, struct pmd_qq *part) {
    if (part->fnum == 0) return;
#if pcm
    // PCMDRV86.ASM:752 The PCM86 keeps the fractional pitch in fnum2 and
    // scales the detune by it.
    uint16_t frac = part->fnum2;
    uint16_t pitch = part->fnum;
    if (pmd->pcm86_vol != 1 && part->detune != 0) {
        uint16_t scaled = frac;
        uint16_t pitch_shift = pitch;
        for (int i = 0; i < 5; i++) {
            pitch_shift >>= 1;
            scaled = (uint16_t)((scaled >> 1) | ((pitch_shift & 1) << 15));
        }
        int16_t det = part->detune;
        if (det < 0) {
            uint16_t sum = (uint16_t)(scaled + (uint16_t)det);
            if (sum == 0) {
                scaled = 1;
            } else if ((unsigned)scaled >= (unsigned)(-(int)det)) {
                scaled = sum;
            } else {
                scaled = 1; // the result must not underflow to zero
            }
        } else {
            uint16_t sum = (uint16_t)(scaled + (uint16_t)det);
            scaled = sum < scaled ? 0xffff : sum;
        }
        uint16_t high = 0;
        for (int i = 0; i < 5; i++) {
            high = (uint16_t)((high << 1) | (scaled >> 15));
            scaled = (uint16_t)(scaled << 1);
        }
        pitch = (uint16_t)((pitch & 0xffe0) | high);
        frac = (uint16_t)((frac & 0x001f) | scaled);
    }
    tone_set2(pmd, frac, (uint8_t)pitch);
#else
    uint16_t pitch = part->fnum;
    pitch = (uint16_t)(pitch + part->porta_num);

    int16_t delta = 0;
    if ((part->lfoswi & 0x11) != 0) {
        if ((part->lfoswi & 1) != 0) delta = part->lfodat;
        if ((part->lfoswi & 0x10) != 0) delta = (int16_t)(delta + part->_lfodat);
        // PCM LFO depth is applied four times (PCMDRV.ASM:740).
        delta = (int16_t)(delta + delta);
        delta = (int16_t)(delta + delta);
    }
    delta = (int16_t)(delta + part->detune);

    if (delta < 0) {
        // PCMDRV.ASM:752 A borrow clears the tone; the original branches on
        // the carry of the 16-bit add.
        unsigned sum = (unsigned)pitch + (unsigned)(uint16_t)delta;
        pitch = sum > 0xffff ? (uint16_t)sum : 0;
    } else {
        unsigned sum = (unsigned)pitch + (unsigned)delta;
        pitch = sum > 0xffff ? 0xffff : (uint16_t)sum;
    }
#if ademu
    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    if (drv != NULL) drv->set_tone(7, pitch, pmd->user_data);
#else
    opnset46(pmd, (uint16_t)((0x09 << 8) | (uint8_t)pitch));
    opnset46(pmd, (uint16_t)((0x0a << 8) | (uint8_t)(pitch >> 8)));
#endif
#endif
}

#if !pcm
// PCMDRV.ASM:927, PCMDRVE.ASM:767 The YM2608 ADPCM pitch table (x2).
static const uint16_t pcm_adpcm_tune_data[12] = {
    0x6264, 0x6840, 0x6e74, 0x7506, 0x7bfc, 0x835e,
    0x8b2e, 0x9376, 0x9c3c, 0xa588, 0xaf62, 0xb9d0,
};

// PCMDRV.ASM:771, PCMDRVE.ASM:711
// Sets the ADPCM pitch from `note`.
static void fnumsetm(struct pmd *pmd, struct pmd_qq *part, uint8_t note) {
    (void)pmd;
    uint8_t pitch_class = note & 0x0f;
    if (pitch_class == 0x0f) {
        // PCMDRV.ASM:775, PCMDRVE.ASM:715 a rest
        part->onkai = 0xff;
        if ((part->lfoswi & 0x11) != 0) return;
        part->fnum = 0;
        return;
    }
    part->onkai = note;

    uint8_t octave = (uint8_t)(note >> 4);
    uint8_t block = octave;
    uint8_t shift = octave <= 5 ? (uint8_t)(5 - octave) : 0;

    uint16_t fnum = pcm_adpcm_tune_data[pitch_class];
    if (block >= 6) {
        // o7 and above: double the entry when it does not overflow.
        uint8_t high_nibble = 0x50;
        if ((int16_t)fnum >= 0) {
            fnum = (uint16_t)(fnum << 1);
            high_nibble = 0x60;
        }
        part->onkai = (uint8_t)((part->onkai & 0x0f) | high_nibble);
    } else {
        fnum = (uint16_t)(fnum >> shift);
    }
    part->fnum = fnum;
}
#else
// PCMDRV86.ASM:817
// Sets the PCM86 pitch from `note`: fnum holds the integer part and
// fnum2 the fraction, both taken from the frequency table.
static void fnumsetm(struct pmd *pmd, struct pmd_qq *part, uint8_t note) {
    uint8_t pitch_class = note & 0x0f;
    if (pitch_class == 0x0f) {
        // PCMDRV86.ASM:821 a rest
        part->onkai = 0xff;
        if ((part->lfoswi & 0x11) != 0) return;
        // PMD.ASM:4296 fnrest clears fnum only; fnum2 keeps its old value.
        part->fnum = 0;
        return;
    }

    if (pmd->pcm86_vol == 1 && note >= 0x65) {
        // PCMDRV86.ASM:823 /S plays everything from 65h up one octave lower.
        note = pitch_class < 5 ? 0x60 : 0x50;
        note |= pitch_class;
    }

    part->onkai = note;
    uint8_t index = (uint8_t)((note & 0xf0) >> 1);
    index = (uint8_t)(index + ((note & 0xf0) >> 2) + pitch_class);
    const uint8_t *entry = pmd->p86_freqtable + (unsigned)index * 3;
    part->fnum = (uint16_t)(0xff00 | entry[0]);
    part->fnum2 = (uint16_t)(entry[1] | (entry[2] << 8));
}
#endif

// PPZDRV.ASM:578, PCMDRV.ASM:496
// The volume correction chain shared by the PPZ8 and PCM volume setters:
// voldown, fadeout, envelope, and volume LFO. `square_fade` selects the YM2608
// ADPCM drivers (PCMDRV.ASM:514, PCMDRVE.ASM:604), which square the fade before
// applying it.
static uint8_t pcm_vol_calc(struct pmd *pmd, struct pmd_qq *part, uint8_t voldown, bool square_fade) {
    uint8_t volume = part->volpush;
    if (volume == 0) volume = part->volume;
    uint8_t value = volume;

    if (voldown != 0) {
        value = (uint8_t)(((uint16_t)value * (uint8_t)(-voldown)) >> 8);
    }

    uint8_t fade = pmd->fadeout_volume;
    if (fade != 0) {
        uint8_t neg = (uint8_t)(-fade);
        if (square_fade) neg = (uint8_t)(((uint16_t)neg * neg) >> 8);
        value = (uint8_t)(((uint16_t)value * neg) >> 8);
    }

    volume = value;
    if (volume == 0) return 0;

    if (part->envf == 0xff) {
        // PPZDRV.ASM:609, PCMDRV.ASM:529, PCMDRVE.ASM:552, PCMDRV86.ASM:602
        // The extended envelope scales by (volume + 1) / 16 and rounds up on
        // the bit the fourth shift drops.
        uint8_t v = part->eenv_volume;
        if (v == 0) return 0;
        uint16_t product = (uint16_t)volume * (uint8_t)(v + 1);
        volume = (uint8_t)(product >> 4);
        if ((product & 0x08) != 0) volume++;
    } else {
        int8_t penv = (int8_t)part->penv;
        if (penv < 0) {
            uint8_t m = (uint8_t)((uint8_t)(-penv) * 16);
            if (volume < m) return 0;
            volume = (uint8_t)(volume - m);
        } else {
            uint8_t m = (uint8_t)((uint8_t)penv * 16);
            uint8_t sum = (uint8_t)(volume + m);
            volume = sum < volume ? 255 : sum;
        }
    }

    if ((part->lfoswi & 0x22) == 0) return volume;
    int16_t delta = 0;
    if ((part->lfoswi & 2) != 0) delta = part->lfodat;
    if ((part->lfoswi & 0x20) != 0) delta = (int16_t)(delta + part->_lfodat);

    if (delta < 0) {
        // PPZDRV.ASM:670, PCMDRV.ASM:590, PCMDRVE.ASM:613, PCMDRV86.ASM:663
        // A borrow clamps the volume to zero; the original branches on the
        // carry of the 16-bit add.
        unsigned sum = (unsigned)volume + (unsigned)(uint16_t)delta;
        volume = sum > 0xffff ? (uint8_t)sum : 0;
    } else {
        // PPZDRV.ASM:666, PCMDRV.ASM:586, PCMDRVE.ASM:609, PCMDRV86.ASM:659
        // Any set bit above the low byte clamps to the maximum.
        unsigned sum = (unsigned)volume + (unsigned)delta;
        volume = sum > 0xff ? 255 : (uint8_t)sum;
    }
    return volume;
}
#endif

// PCMDRV.ASM:821
void pmd_pcm_effect(struct pmd *pmd, uint8_t efcnum, uint16_t deltan,
                    uint8_t pan, uint8_t vol) {
    bool ok = int60_enter(pmd, 0x0f);
#if board2
    if (ok && pmd->board != 0) pcm_effect(pmd, efcnum, deltan, pan, vol);
#else
    // The default and VA1 builds have no PCM driver, and so no effect.
    (void)ok;
    (void)efcnum;
    (void)deltan;
    (void)pan;
    (void)vol;
#endif
    int60_exit(pmd);
}

#if ppz
// PPZDRV.ASM:475
static const uint8_t ppzpandata[] = {0, 9, 1, 5};

// PPZDRV.ASM:853 ppz_tune_data: the o5 standard fnum for each semitone.
static const uint16_t ppz_tune_data[12] = {
    0x8000, 0x87a6, 0x8fb3, 0x9838, 0xa146, 0xaade,
    0xb4ff, 0xbfcc, 0xcb34, 0xd747, 0xe418, 0xf1a5,
};

// PPZDRV.ASM:4
// The original guards every PPZ8 call with `ppz_call_seg != 0`. Returns the
// vtable when the calls are available, NULL otherwise.
static const struct pmd_drv_ppz8 *ppz8_ops(struct pmd *pmd) {
    return pmd->ppz_call_seg != 0 ? pmd->ppz8 : NULL;
}

// PPZDRV.ASM:11
// The PPZ8 part main loop.
static void ppzmain(struct pmd *pmd, struct pmd_qq *part) {
    uint8_t *src = part->address;
    if (src == NULL) return;

    if (part->partmask != 0) goto ppzmain_nonplay;

    part->leng--;
    uint8_t remaining = part->leng;
    uint8_t cmd;

    // PPZDRV.ASM:26 Keyoff when the remaining length reaches Q.
    if ((part->keyoff_flag & 3) == 0 && remaining <= part->qdat) {
        part->keyoff_flag = 0xff;
        keyoffz(pmd, part);
    }
    if (remaining != 0) goto mpexitz;

    part->lfoswi &= 0xf7; // portamento off

mp1z:
    cmd = lodsb(&src);
    if (cmd < 0x80) goto mp2z;
    if (cmd == 0x80) goto mp15z;

    switch (commandsz(pmd, part, &src)) {
    case PMD_CMD_MP1Z: goto mp1z;
    case PMD_CMD_PPZMNP_1: goto ppzmnp_1;
    case PMD_CMD_PORTA_RETURNZ: goto porta_returnz;
    default: break;
    }
    goto mp1z;

mp15z:
    // PPZDRV.ASM:49 End of music, or a loop if "L" was seen.
    src--;
    part->address = src;
    part->loopcheck = 3;
    part->onkai = 0xff;
    if (part->partloop == NULL) goto mpexitz;
    src = part->partloop;
    part->loopcheck = 1;
    goto mp1z;

mp2z:
    // PPZDRV.ASM:62 Note: set the pitch and gate.
    cmd = lfoinitp(pmd, part, cmd);
    cmd = oshift(part, cmd);
    fnumsetz(pmd, part, cmd);

    cmd = lodsb(&src);
    part->leng = cmd;
    calc_q(pmd, part, &src);

porta_returnz:
    // PPZDRV.ASM:71
    if (part->volpush == 0) goto mp_newz;
    if (part->onkai == 0xff) goto mp_newz;
    pmd->volpush_flag--;
    if (pmd->volpush_flag == 0) goto mp_newz;
    pmd->volpush_flag = 0;
    part->volpush = 0;

mp_newz:
    volsetz(pmd, part);
    otodasiz(pmd, part);
    if ((part->keyoff_flag & 1) != 0) keyonz(pmd, part);
    part->keyon_flag++;

    part->address = src;
    pmd->tieflag = 0;
    pmd->volpush_flag = 0;
    part->keyoff_flag = 0;
    if (*src == 0xfb) part->keyoff_flag = 2; // '&' immediately follows
    mnp_ret(pmd, part);
    return;

mpexitz:;
    // PPZDRV.ASM:96 LFO, portamento, and fadeout processing.
    uint8_t lfosw = part->lfoswi;
    pmd->lfo_switch = (uint8_t)(lfosw & 8);
    if (lfosw != 0) {
        if ((lfosw & 3) != 0) {
            if (lfo(pmd, part)) pmd->lfo_switch |= lfosw & 3;
        }
        if ((lfosw & 0x30) != 0) {
            lfo_change(part);
            if (lfo(pmd, part)) {
                lfo_change(part);
                pmd->lfo_switch |= part->lfoswi & 0x30;
            } else {
                lfo_change(part);
            }
        }
        if ((pmd->lfo_switch & 0x19) != 0) {
            if ((pmd->lfo_switch & 8) != 0) porta_calc(pmd, part);
            otodasiz(pmd, part);
        }
    }
    // PPZDRV.ASM:136 volsz
    if (!soft_env(pmd, part) && (pmd->lfo_switch & 0x22) == 0 &&
        pmd->fadeout_speed == 0) {
        mnp_ret(pmd, part);
        return;
    }
    volsetz(pmd, part);
    mnp_ret(pmd, part);
    return;

ppzmain_nonplay:
    // PPZDRV.ASM:149
    part->keyoff_flag = 0xff;
    part->leng--;
    if (part->leng != 0) {
        mnp_ret(pmd, part);
        return;
    }

ppzmnp_1:
    cmd = lodsb(&src);
    if (cmd == 0x80) goto ppzmnp_2;
    if (cmd < 0x80) goto ppzmnp_3;
    switch (commandsz(pmd, part, &src)) {
    case PMD_CMD_MP1Z: goto mp1z;
    case PMD_CMD_PPZMNP_1: goto ppzmnp_1;
    case PMD_CMD_PORTA_RETURNZ: goto porta_returnz;
    default: break;
    }
    goto ppzmnp_1;

ppzmnp_2:
    // PPZDRV.ASM:163 End of music, or a loop if "L" was seen.
    src--;
    part->address = src;
    part->loopcheck = 3;
    part->onkai = 0xff;
    if (part->partloop == NULL) {
        fmmnp_4(pmd, part);
        return;
    }
    src = part->partloop;
    part->loopcheck = 1;
    goto ppzmnp_1;

ppzmnp_3:
    // PPZDRV.ASM:178 A rest goes through the shared rest-note path.
    part->fnum2 = 0;
    rest_note(pmd, part, &src);
}

// PPZDRV.ASM:185
static enum pmd_cmd_result commandsz(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    return command00(pmd, part, src, cmdtblz);
}

// PPZDRV.ASM:188
// The PPZ8 command table, indexed by 0xff - command.
static const pmd_cmd_fn cmdtblz[0xff - 0xb1 + 1] = {
    com_at_z,       // ff '@'
    comq,           // fe 'q'
    comv,           // fd 'v'
    comt,           // fc
    comtie,         // fb '&'
    comd,           // fa 'D'
    comstloop,      // f9 '['
    comedloop,      // f8 ']'
    comexloop,      // f7 ':'
    comlopset,      // f6 'L'
    comshift,       // f5 '_'
    comvolupm,      // f4 ')'
    comvoldownm,    // f3 '('
    lfoset,         // f2
    lfoswitch,      // f1
    psgenvset,      // f0
    comy,           // ef 'y'
    jump1,          // ee
    jump1,          // ed
    pansetz,        // ec 'p'
    rhykey,         // eb
    rhyvs,          // ea
    rpnset,         // e9
    rmsvs,          // e8
    comshift2,      // e7
    rmsvs_sft,      // e6
    rhyvs_sft,      // e5
    jump1,          // e4
    comvolupm2,     // e3
    comvoldownm2,   // e2
    jump1,          // e1
    jump1,          // e0
    syousetu_lng_set, // df
    vol_one_up_pcm, // de
    vol_one_down,   // dd
    status_write,   // dc
    status_add,     // db
    portaz,         // da
    jump1,          // d9
    jump1,          // d8
    jump1,          // d7
    mdepth_set,     // d6
    comdd,          // d5
    ssg_efct_set,   // d4
    fm_efct_set,    // d3
    fade_set,       // d2
    jump1,          // d1
    jump1,          // d0
    jump1,          // cf
    ppzrepeat_set,  // ce
    extend_psgenvset, // cd
    jump1,          // cc
    lfowave_set,    // cb
    lfo_extend,     // ca
    envelope_extend, // c9
    jump3,          // c8
    jump3,          // c7
    jump6,          // c6
    jump1,          // c5
    comq2,          // c4
    pansetz_ex,     // c3
    lfoset_delay,   // c2
    jump0,          // c1 (slur)
    ppz_mml_part_mask, // c0
    _lfoset,        // bf
    _lfoswitch,     // be
    _mdepth_set,    // bd
    _lfowave_set,   // bc
    _lfo_extend,    // bb
    _volmask_set,   // ba
    _lfoset_delay,  // b9
    jump2,          // b8
    mdepth_count,   // b7
    jump1,          // b6
    jump2,          // b5
    jump16,         // b4 (the PCM tables own ppz_extpartset)
    comq3,          // b3
    comshift_master, // b2
    comq4,          // b1
};

// PPZDRV.ASM:287
// Sets the extended PPZ8 parts (0b4h). Each 16-bit value is an offset into the
// module data; a zero leaves the part untouched.
static enum pmd_cmd_result ppz_extpartset(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)part;
    for (int i = 0; i < 8; i++) {
        uint16_t offset = lodsw(src);
        if (offset == 0) continue;
        struct pmd_qq *ext = &pmd->parts[part10a + i];
        ext->address = pmd->mmlbuf + offset;
        ext->leng = 1; // starts on the next count
        ext->keyoff_flag = 0xff; // currently keyed off
        ext->mdc = 0xff; // M depth counter (infinite)
        ext->mdc2 = 0xff;
        ext->_mdc = 0xff;
        ext->_mdc2 = 0xff;
        ext->onkai = 0xff; // rest
        ext->volume = 128; // PCM volume default
        ext->fmpan = 5; // centre
    }
    return PMD_CMD_RETURN;
}

// PPZDRV.ASM:320
static enum pmd_cmd_result ppz_mml_part_mask(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint8_t mask = lodsb(src);
    if (mask >= 2) return special_0c0h(pmd, part, src, mask);
    if (mask == 0) {
        part->partmask &= 0xbf;
        if (part->partmask != 0) return PMD_CMD_PPZMNP_1;
        return PMD_CMD_MP1Z; // the part is revived
    }

    part->partmask |= 0x40;
    if (part->partmask != 0x40) return PMD_CMD_PPZMNP_1;
#if ademu
    // PPZDRV.ASM:330 While the PCM part plays through PPZ8's ADPCM emulation,
    // channel 7 is the PCM part's, not this part's.
    if (pmd->partb == 7 && pmd->adpcm_emulate == 1) return PMD_CMD_PPZMNP_1;
#endif
    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    if (drv != NULL) drv->keyoff(pmd->partb, pmd->user_data);
    return PMD_CMD_PPZMNP_1;
}

// PPZDRV.ASM:352
// Sets the part's loop range from the PPZ8 voice table.
static enum pmd_cmd_result ppzrepeat_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    const struct pmd_ppz8_voice *voice = ppz_voicetable_calc(pmd, part);
    if (drv != NULL && voice != NULL) {
        uint32_t size = voice->size;
        // Each parameter is an absolute offset unless it is negative, in which
        // case it counts back from the end of the data.
        const uint8_t *p = *src;
        uint32_t start = 0;
        uint32_t end = 0;
        for (int i = 0; i < 2; i++) {
            uint16_t param = (uint16_t)(p[0] | (p[1] << 8));
            p += 2;
            // PPZDRV.ASM:376 get_loop_ppz8 offsets a negative value by the
            // whole data size and leaves a positive one as it is.
            uint32_t value = param;
            if ((int16_t)param < 0) value = (uint32_t)(int32_t)(int16_t)param + size;
            if (i == 0) start = value;
            else end = value;
        }
        drv->set_loop(pmd->partb, start, end, pmd->user_data);
    }
    *src += 6;
    return PMD_CMD_RETURN;
}

// PPZDRV.ASM:392
// Returns the part's PPZ8 voice table entry, or NULL when the voice is not
// loaded.
static const struct pmd_ppz8_voice *ppz_voicetable_calc(struct pmd *pmd, struct pmd_qq *part) {
    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    if (drv == NULL) return NULL;
    return drv->get_voice(part->voicenum, pmd->user_data);
}

// PPZDRV.ASM:415
// Portamento for the PPZ8 parts (0dah).
static enum pmd_cmd_result portaz(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    if (part->partmask != 0) {
        (*src)++; // the first pitch byte is skipped while masked
        return PMD_CMD_RETURN;
    }

    uint8_t note = lodsb(src);
    note = lfoinitp(pmd, part, note);
    note = oshift(part, note);
    fnumsetz(pmd, part, note);

    uint32_t from_fnum = ((uint32_t)part->fnum2 << 16) | part->fnum;
    uint8_t from_note = part->onkai;

    note = lodsb(src);
    note = oshift(part, note);
    fnumsetz(pmd, part, note);
    uint32_t to_fnum = ((uint32_t)part->fnum2 << 16) | part->fnum;

    part->onkai = from_note;
    part->fnum = (uint16_t)from_fnum;
    part->fnum2 = (uint16_t)(from_fnum >> 16);

    int32_t delta = (int32_t)(to_fnum - from_fnum) >> 4;
    part->leng = lodsb(src);
    calc_q(pmd, part, src);

    int16_t step = (int16_t)(uint16_t)delta;
    part->porta_num2 = (int16_t)(step / (int16_t)part->leng);
    part->porta_num3 = (int16_t)(step % (int16_t)part->leng);
    part->lfoswi |= 8; // portamento on
    return PMD_CMD_PORTA_RETURNZ;
}

// PPZDRV.ASM:475
static enum pmd_cmd_result pansetz(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint8_t pan = lodsb(src);
    pan = ppzpandata[pan & 3];
    part->fmpan = pan;
    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    if (drv != NULL) drv->set_pan(pmd->partb, pan, pmd->user_data);
    return PMD_CMD_RETURN;
}

// PPZDRV.ASM:494
// Pan setting extend: px -4..+4 (0c3h), mapped onto PPZ8's 1..9.
static enum pmd_cmd_result pansetz_ex(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint8_t pan = lodsb(src);
    (*src)++; // the reversed-phase flag is skipped
    if ((int8_t)pan >= 0) {
        if (pan >= 5) pan = 4;
    } else if ((int8_t)pan < -4) {
        pan = (uint8_t)-4;
    }
    pan = (uint8_t)(pan + 5);
    part->fmpan = pan;
    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    if (drv != NULL) drv->set_pan(pmd->partb, pan, pmd->user_data);
    return PMD_CMD_RETURN;
}

// PPZDRV.ASM:514
static enum pmd_cmd_result com_at_z(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint8_t voice = lodsb(src);
#if ademu
    bool revive = false;
    if (pmd->adpcm_emulate == 1) {
        // PPZDRV.ASM:521 Voices 128-255 cannot be emulated; they fall back to
        // 127.
        if ((int8_t)voice < 0) voice = 127;
        if (pmd->partb == 7) {
            pmd->parts[part10].partmask |= 0x10;
            part->partmask &= 0xef;
            revive = part->partmask == 0;
            pmd->adpcm_emulate = 0;
            const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
            if (drv != NULL) {
                drv->set_adpcm_emulation(false, pmd->user_data);
            }
        }
    }
#endif
    part->voicenum = voice;
    ppz_neiro_reset(pmd, part);
#if ademu
    if (revive) return PMD_CMD_MP1Z;
#endif
    return PMD_CMD_RETURN;
}

// PPZDRV.ASM:542
// Loads the voice's loop range and source rate into PPZ8.
static void ppz_neiro_reset(struct pmd *pmd, struct pmd_qq *part) {
    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    if (drv == NULL) return;
    const struct pmd_ppz8_voice *voice = ppz_voicetable_calc(pmd, part);
    if (voice == NULL) return;
    drv->set_loop(pmd->partb, voice->loop_start, voice->loop_end, pmd->user_data);
    drv->set_source_rate(pmd->partb, voice->rate, pmd->user_data);
}

// PPZDRV.ASM:571
static void volsetz(struct pmd *pmd, struct pmd_qq *part) {
    uint8_t volume = pcm_vol_calc(pmd, part, pmd->ppz_voldown, false);
    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    if (drv == NULL) return;
    if (volume == 0) {
        drv->keyoff(pmd->partb, pmd->user_data); // volume 0 is a keyoff
        return;
    }
    drv->set_volume(pmd->partb, (uint8_t)(volume >> 4), pmd->user_data);
}

// PPZDRV.ASM:699
static void keyonz(struct pmd *pmd, struct pmd_qq *part) {
    if (part->onkai == 0xff) return;
    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    if (drv == NULL) return;
    // PPZDRV.ASM:710 DX = the voice number with bit 7 moved to bit 15 (the
    // bank select).
    uint8_t num = part->voicenum;
    uint16_t voice = (uint16_t)((num & 0x7f) | ((uint16_t)(num & 0x80) << 8));
    drv->keyon(pmd->partb, voice, pmd->user_data);
}

// PPZDRV.ASM:720
static void keyoffz(struct pmd *pmd, struct pmd_qq *part) {
    if (part->envf == 0xff) {
        if (part->eenv_count == 4) return;
    } else if (part->envf == 2) {
        return;
    }
    keyoffp(pmd, part);
}

// PPZDRV.ASM:735
// Sets the PPZ8 tone from the fnum, portamento, LFO, and detune.
static void otodasiz(struct pmd *pmd, struct pmd_qq *part) {
    uint32_t value = ((uint32_t)part->fnum2 << 16) | part->fnum;
    if (value == 0) return;

    if (part->porta_num != 0) value += (uint32_t)((int32_t)part->porta_num * 16);

    int16_t delta = 0;
    if ((part->lfoswi & 0x11) != 0) {
        if ((part->lfoswi & 1) != 0) delta = (int16_t)(delta + part->lfodat);
        if ((part->lfoswi & 0x10) != 0) delta = (int16_t)(delta + part->_lfodat);
    }
    delta = (int16_t)(delta + part->detune);

    // PPZDRV.ASM:771 DX = the high bytes of both halves, the pitch the delta
    // scales by.
    int16_t scale = (int16_t)((uint16_t)((value >> 8) & 0xff) |
                              (uint16_t)(((value >> 24) & 0xff) << 8));
    int32_t product = (int32_t)delta * scale;
    uint32_t sum = value + (uint32_t)product;
    if ((product >> 16) < 0) {
        if (sum < value) value = sum;
        else value = 0;
    } else {
        if (sum >= value) value = sum;
        else value = 0xffffffff;
    }

    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    if (drv != NULL) {
        drv->set_tone(pmd->partb, (uint16_t)(value >> 16), pmd->user_data);
    }
}

// PPZDRV.ASM:801
// Sets the PPZ8 pitch from `note`.
static void fnumsetz(struct pmd *pmd, struct pmd_qq *part, uint8_t note) {
    (void)pmd;
    uint8_t pitch_class = note & 0x0f;
    if (pitch_class == 0x0f) {
        // PPZDRV.ASM:805 a rest
        part->onkai = 0xff;
        if ((part->lfoswi & 0x11) != 0) return;
        part->fnum = 0;
        part->fnum2 = 0;
        return;
    }
    part->onkai = note;

    uint16_t fnum = ppz_tune_data[pitch_class];
    uint16_t frac = 0;

    uint8_t octave = (uint8_t)((note >> 4) & 0x0f); // the octave
    if (octave < 4) {
        // PPZDRV.ASM:824 shr ax,cl
        fnum = (uint16_t)(fnum >> (4 - octave));
    } else {
        // PPZDRV.ASM:830 ppz_over_o5_loop
        for (octave -= 4; octave != 0; octave--) {
            frac = (uint16_t)((frac << 1) | (fnum >> 15));
            fnum = (uint16_t)(fnum << 1);
        }
    }
    // PPZDRV.ASM:835 ppz_fnumset
    part->fnum = fnum;
    part->fnum2 = frac;
}
#endif

#if board2 && (pcm || adpcm)
#if pcm
// PCMDRV86.ASM:412
// The /S option scales every explicit size and offset by 32.
static uint16_t pcm86vol_chk(struct pmd *pmd, uint16_t value) {
    if (pmd->pcm86_vol == 0) return value;
    unsigned scaled = (unsigned)value << 5;
    return (uint16_t)scaled;
}

// PCMDRV86.ASM:1162
// Returns the sample data address of the current 32-bit offset, or NULL when
// P86DRV is not resident.
static const uint8_t *get_data_offset(struct pmd *pmd) {
    if (pmd->p86 == NULL) return NULL;
    uint32_t offset = ((uint32_t)pmd->start_ofs2 << 16) | pmd->start_ofs;
    return pmd->p86->get_data_offset(offset, pmd->user_data);
}

// PCMDRV86.ASM:789
// Writes the PCM86 frequency register from the addsize values.
static void tone_set2(struct pmd *pmd, uint16_t freq, uint8_t rate) {
    pmd->addsize2 = freq;
    pmd->addsize1 = (uint8_t)(rate & 0x1f);
    // PCMDRV86.ASM:796 Bits 5-7 of BL become the slowest three rate bits,
    // inverted.
    rate = (uint8_t)(((rate << 3) | (rate >> 5)) & 0x07);
    rate ^= 7;
    uint8_t flags = inb(pmd, 0xa468);
    outb(pmd, 0x5f, flags);
    flags &= 0xf8;
    flags |= rate;
    outb(pmd, 0xa468, flags);
}

// PCMDRV86.ASM:534
// Loads a PCM86 voice's data range from the voice table.
static void neiro_set(struct pmd *pmd, uint8_t voice) {
    const struct pmd_pcm86_voice *v = &pmd->pcm_table.pcm86.pcmadrs[voice];
    pmd->_start_ofs = (uint16_t)v->offset;
    pmd->_start_ofs2 = (uint16_t)(v->offset >> 16);
    pmd->_size1 = (uint16_t)v->size;
    pmd->_size2 = (uint16_t)(v->size >> 16);
    pmd->repeat_flag = 0;
    pmd->release_flag1 = 0;
}

// PCMDRV86.ASM:671
// Writes a 0..255 volume to the PCM86 volume register.
static void pcm86_vol_out(struct pmd *pmd, uint8_t volume) {
    if (pmd->pcm86_vol != 0) {
        // PCMDRV86.ASM:676 SPB-style volume: the register takes roughly
        // sqrt(volume).
        uint8_t rem = volume;
        uint8_t sq = 0;
        bool carry = true;
        for (;;) {
            unsigned sub = (unsigned)rem - sq - (carry ? 1 : 0);
            carry = sub > 0xff;
            rem = (uint8_t)sub;
            if (carry) break;
            sub = (unsigned)rem - sq;
            carry = sub > 0xff;
            rem = (uint8_t)sub;
            if (carry) break;
            sq++;
            if (sq == 15) break;
        }
        volume = sq;
    } else {
        volume >>= 4;
    }
    volume = (uint8_t)((volume & 0x0f) ^ 0x0f);
    outb(pmd, 0xa466, (uint8_t)(volume | 0xa0));
}
#endif

#if pcm
// PCMDRV86.ASM:1004
// Scales one sample for the quieter side of an off-centre pan. The original
// multiplies AL by the pan byte with an 8-bit IMUL and keeps the high byte of
// the doubled product.
static uint8_t pcm86_pan_scale(struct pmd *pmd, uint8_t data) {
    int16_t product = (int16_t)((int8_t)data * (int8_t)pmd->pcm86_pan_dat);
    return (uint8_t)((uint16_t)((uint16_t)product * 2u) >> 8);
}

// PCMDRV86.ASM:967
// Writes one stereo sample pair. The pan flag that selects the variant is
// PPZ8's trans_table index (PCMDRV86.ASM:1415).
static void pcm86_out_sample(struct pmd *pmd, uint8_t data) {
    uint8_t left = data;
    uint8_t right = data;
    switch (pmd->pcm86_pan_flag & 7) {
    case 1: // left_trans
        right = pcm86_pan_scale(pmd, data);
        break;
    case 2: // right_trans
        left = pcm86_pan_scale(pmd, data);
        break;
    case 4: // double_trans_g
    case 7:
        right = (uint8_t)(0u - data);
        break;
    case 5: // left_trans_g
        right = pcm86_pan_scale(pmd, (uint8_t)(0u - data));
        break;
    case 6: // right_trans_g
        left = pcm86_pan_scale(pmd, data);
        right = (uint8_t)(0u - data);
        break;
    default: // double_trans (centre)
        break;
    }
    outb(pmd, 0xa46c, left);
    outb(pmd, 0xa46c, right);
}

// One FIFO transfer's cursor (PCMDRV86.ASM:910).
struct pcm_transfer {
    // DS:SI: the mapped sample data and the offset within it.
    const uint8_t *base;
    uint16_t src;
    // DI: the remaining size's low word.
    uint16_t size;
    // BP: the bytes written since the last remap.
    uint16_t written;
};

// PCMDRV86.ASM:1094
// Advances the sample address by one step. Returns true when the transfer is
// finished, i.e. the original's carry.
static bool add_address(struct pmd *pmd, struct pcm_transfer *t) {
    uint16_t sum = (uint16_t)(pmd->addsizew + (uint16_t)pmd->addsize2);
    bool carry = sum < pmd->addsizew;
    pmd->addsizew = sum;

    uint16_t add = pmd->addsize1;
    t->written = (uint16_t)(t->written + add + (carry ? 1 : 0));
    t->src = (uint16_t)(t->src + add + (carry ? 1 : 0));

    if (t->src >= 0x4000) {
        // The 16 KB EMS window is exhausted: advance and remap.
        uint32_t offset = ((uint32_t)pmd->start_ofs2 << 16) | pmd->start_ofs;
        offset += t->written;
        pmd->start_ofs = (uint16_t)offset;
        pmd->start_ofs2 = (uint16_t)(offset >> 16);
        t->written = 0;
        t->base = get_data_offset(pmd);
        t->src = 0;
    }

    unsigned remaining = (unsigned)t->size - add - (carry ? 1 : 0);
    bool borrow = remaining > 0xffff;
    t->size = (uint16_t)remaining;

    if (!borrow) {
        if (t->size != 0) return false;
        if (pmd->size2 != 0) return false;
    } else {
        if (pmd->size2 != 0) {
            pmd->size2--;
            return false;
        }
    }

    // PCMDRV86.ASM:1130 addadd_repchk
    if (pmd->repeat_flag == 0) return true;
    if (pmd->release_flag2 == 1) return true;

    pmd->size2 = pmd->repeat_size2;
    t->size = pmd->repeat_size1;
    pmd->start_ofs2 = pmd->repeat_ofs2;
    pmd->start_ofs = pmd->repeat_ofs;
    t->written = 0;
    t->base = get_data_offset(pmd);
    t->src = 0;
    return false;
}

// PCMDRV86.ASM:910
// Transfers up to `count` stereo samples from the PCM86 data to the FIFO.
static void pcm_trans(struct pmd *pmd, uint16_t count) {
    bool usable = pmd->p86 != NULL &&
                  pmd->p86->get_version(pmd->user_data) >= 0x10 &&
                  (pmd->size1 | pmd->size2) != 0;
    if (!usable) {
        // PCMDRV86.ASM:1193 zero_trans fills the buffer with silence and stops
        // transferring. The original jumps here before loading dx with the PCM
        // data port, so its writes land on a leftover port; the FIFO data port
        // is used here.
        for (uint16_t i = 0; i < count; i++) {
            outb(pmd, 0xa46c, 0);
            outb(pmd, 0xa46c, 0);
        }
        pmd->trans_flag = 0;
        return;
    }

    pmd->p86->push_ems(pmd->user_data);
    {
        struct pcm_transfer t = {
            .base = get_data_offset(pmd),
            .src = 0,
            .size = pmd->size1,
            .written = 0,
        };
        uint16_t remaining = count;
        bool finished = false;
        for (;;) {
            if (t.base == NULL) {
                finished = true;
                break;
            }
            pcm86_out_sample(pmd, t.base[t.src]);
            if (add_address(pmd, &t)) {
                finished = true;
                break;
            }
            if (--remaining == 0) break;
        }

        if (finished) {
            // PCMDRV86.ASM:1177 trans_fin: fill the rest with silence.
            remaining--;
            if (remaining != 0) {
                for (; remaining != 0; remaining--) {
                    outb(pmd, 0xa46c, 0);
                    outb(pmd, 0xa46c, 0);
                }
            }
            pmd->size1 = 0;
            pmd->size2 = 0;
        } else {
            // PCMDRV86.ASM:977 trans_exit
            uint32_t offset = ((uint32_t)pmd->start_ofs2 << 16) | pmd->start_ofs;
            offset += t.written;
            pmd->start_ofs = (uint16_t)offset;
            pmd->start_ofs2 = (uint16_t)(offset >> 16);
            pmd->size1 = t.size;
        }
    }
    pmd->p86->pop_ems(pmd->user_data);
}

// PCMDRV86.ASM:859
// The PCM86 FIFO interrupt handler.
static void fifo_main(struct pmd *pmd) {
    if (pmd->play86_flag != 0) {
        if (pmd->trans_flag != 0) {
            pcm_trans(pmd, trans_size / 2);
        } else {
            // Playing with nothing left to transfer: stop and leave the FIFO
            // interrupt disabled.
            stop_86pcm(pmd);
            return;
        }
    }

    // PCMDRV86.ASM:894 Reset the FIFO interrupt flag and release it.
    uint8_t value = inb(pmd, 0xa468);
    outb(pmd, 0x5f, value);
    value &= 0xef;
    outb(pmd, 0xa468, value);
    outb(pmd, 0x5f, value);
    value |= 0x10;
    outb(pmd, 0xa468, value);
}

// PCMDRV86.ASM:1205
static void play_86pcm(struct pmd *pmd) {
    // PCMDRV86.ASM:1209 Stop playback, select the CPU-to-FIFO mode, and reset
    // the FIFO.
    uint8_t value = inb(pmd, 0xa468);
    outb(pmd, 0x5f, value);
    value &= 0x7f;
    outb(pmd, 0xa468, value);
    outb(pmd, 0x5f, value);
    value &= 0xbf;
    outb(pmd, 0xa468, value);
    outb(pmd, 0x5f, value);
    value |= 0x08;
    outb(pmd, 0xa468, value);
    outb(pmd, 0x5f, value);
    value &= 0xf7;
    outb(pmd, 0xa468, value);
    outb(pmd, 0x5f, value);
    value &= 0xdf;
    outb(pmd, 0xa468, value);
    outb(pmd, 0x5f, value);
    value &= 0xef;
    outb(pmd, 0xa468, value);
    outb(pmd, 0xa46a, 0xf2); // pan: 8-bit left and right

    // PCMDRV86.ASM:1249 The first buffer comes from the voice's start.
    pmd->start_ofs = pmd->_start_ofs;
    pmd->start_ofs2 = pmd->_start_ofs2;
    pmd->size1 = pmd->_size1;
    pmd->size2 = pmd->_size2;
    pmd->addsizew = 0;
    pmd->release_flag2 = 0;
    pcm_trans(pmd, trans_size);

    // PCMDRV86.ASM:1265 Enable the FIFO interrupt at trans_size bytes.
    value = inb(pmd, 0xa468);
    outb(pmd, 0x5f, value);
    value |= 0x10;
    outb(pmd, 0xa468, value);
    outb(pmd, 0x5f, value);
    value |= 0x20;
    outb(pmd, 0xa468, value);
    outb(pmd, 0xa46a, (uint8_t)(trans_size / 128 - 1));

    // PCMDRV86.ASM:1285 Start playback.
    value = inb(pmd, 0xa468);
    outb(pmd, 0x5f, value);
    value |= 0x80;
    outb(pmd, 0xa468, value);

    pmd->play86_flag = 1;
    pmd->trans_flag = 1;
}

// PCMDRV86.ASM:1300
// Stops PCM86 playback and leaves the FIFO interrupt disabled.
static void stop_86pcm(struct pmd *pmd) {
    uint8_t efcnum = inb(pmd, 0xa468);
    outb(pmd, 0x5f, efcnum);
    efcnum &= 0x7f;
    outb(pmd, 0xa468, efcnum);

    outb(pmd, 0x5f, efcnum);
    efcnum |= 0x08;
    outb(pmd, 0xa468, efcnum); // FIFO reset
    outb(pmd, 0x5f, efcnum);
    efcnum &= 0xf7;
    outb(pmd, 0xa468, efcnum); // reset released

    outb(pmd, 0x5f, efcnum);
    efcnum &= 0xdf;
    outb(pmd, 0xa468, efcnum); // FIFO interrupt disabled

    outb(pmd, 0x5f, efcnum);
    efcnum &= 0xef;
    outb(pmd, 0xa468, efcnum);
    outb(pmd, 0x5f, efcnum);
    efcnum |= 0x10;
    outb(pmd, 0xa468, efcnum);

    pmd->play86_flag = 0;
    pmd->trans_flag = 0;
}
#endif

#if pcm
// PCMDRV86.ASM:1347
// Plays a PCM effect: `efcnum` is the effect number, `deltan` the DeltaN, `pan`
// the pan, and `volume` the volume.
static void pcm_effect(struct pmd *pmd, uint8_t efcnum, uint16_t deltan, uint8_t pan,
                       uint8_t volume) {
    pmd->parts[part10].partmask |= 2;
    pmd->pcmflag = 1;
    pmd->pcm_effec_num = efcnum;
    pmd->_voice_delta_n = deltan;
    pmd->_pcm_volume = volume;
    pmd->_pcmpan = pan;

    stop_86pcm(pmd);

    neiro_set(pmd, pmd->pcm_effec_num);
    set_pcm_pan2(pmd, pmd->_pcmpan, 0);

    // PCMDRV86.ASM:1366 Fold the DeltaN into the addsize pair: the high byte
    // becomes the rate and the low byte the fraction.
    uint8_t lo = (uint8_t)pmd->_voice_delta_n;
    uint8_t hi = (uint8_t)(pmd->_voice_delta_n >> 8);
    uint8_t rate = (uint8_t)((hi & 0x0f) | ((hi & 0x70) << 1));
    tone_set2(pmd, (uint16_t)(lo << 8), rate);
    pcm86_vol_out(pmd, pmd->_pcm_volume);
    play_86pcm(pmd);
}
#elif ademu
// PCMDRVE.ASM:761 The PPZ8 emulation has no interrupt PCM effect.
static void pcm_effect(struct pmd *pmd, uint8_t efcnum, uint16_t deltan, uint8_t pan,
                       uint8_t volume) {
    (void)pmd;
    (void)efcnum;
    (void)deltan;
    (void)pan;
    (void)volume;
}
#else
// PCMDRV.ASM:821
static void pcm_effect(struct pmd *pmd, uint8_t efcnum, uint16_t deltan, uint8_t pan,
                       uint8_t volume) {
    if (pmd->pcm_gs_flag == 1) return; // the ADPCM is not available

    pmd->parts[part10].partmask |= 2;
    pmd->pcmflag = 1;
    pmd->pcm_effec_num = efcnum;
    pmd->_voice_delta_n = deltan;
    pmd->_pcm_volume = volume;
    pmd->_pcmpan = pan;

    {
        const struct pmd_pcm_adpcm_voice *v = &pmd->pcm_table.adpcm_data.pcmadrs[efcnum];
        pmd->_pcmstart = v->start;
        pmd->_pcmstop = v->stop;
    }
    opnset46(pmd, (uint16_t)((0x0b << 8) | pmd->_pcm_volume));
    uint16_t tone = pmd->_voice_delta_n;
    opnset46(pmd, (uint16_t)((0x09 << 8) | (uint8_t)tone));
    opnset46(pmd, (uint16_t)((0x0a << 8) | (uint8_t)(tone >> 8)));

    // PCMDRV.ASM:884 pcm_keyon
    opnset46(pmd, 0x0102); // x8 bit mode
    opnset46(pmd, 0x0021); // PCM reset
    opnset46(pmd, (uint16_t)((0x02 << 8) | (uint8_t)pmd->_pcmstart));
    opnset46(pmd, (uint16_t)((0x03 << 8) | (uint8_t)(pmd->_pcmstart >> 8)));
    opnset46(pmd, (uint16_t)((0x04 << 8) | (uint8_t)pmd->_pcmstop));
    opnset46(pmd, (uint16_t)((0x05 << 8) | (uint8_t)(pmd->_pcmstop >> 8)));
    opnset46(pmd, 0x00a0); // play once
    // PCMDRV.ASM:909 The masked pan is rotated into the top two bits.
    opnset46(pmd, (uint16_t)((0x01 << 8) | (uint8_t)(((pmd->_pcmpan & 3) << 6) | 2)));
    opnset46(pmd, 0x1080); // clear EOS
    opnset46(pmd, 0x1018); // only EOS/TA/TB change
}
#endif
#endif

// The number of effects in efftbl (EFFECT.INC's EFFMAC entries).
enum {
    eff_count = 40,
};

// EFCDRV.ASM:9
// Plays a rhythm effect through PPSDRV when its voice differs from the one
// played last, then starts the SSG effect with pitch and volume correction.
static void effgo(struct pmd *pmd, struct pmd_qq *part, uint8_t efcnum) {
    if (pmd->ppsdrv_flag != 0) {
        efcnum |= 0x80;
        bool same = pmd->last_shot_data == efcnum;
        pmd->last_shot_data = efcnum;
        if (same && pmd->pps != NULL) pmd->pps->keyoff(pmd->user_data);
    }
    pmd->hosei_flag = 3; // pitch and volume correction (the rhythm part)
    eff_main(pmd, part, efcnum);
}

// EFCDRV.ASM:22
// The 'n' command's entry: it corrects the pitch only.
static void eff_on2(struct pmd *pmd, struct pmd_qq *part, uint8_t efcnum) {
    pmd->hosei_flag = 1;
    eff_main(pmd, part, efcnum);
}

// EFCDRV.ASM:25
// The INT 60h entry to eff_main, which it reaches with no pitch or volume
// correction.
static void eff_on(struct pmd *pmd, uint8_t efcnum) {
    pmd->hosei_flag = 0;
    eff_main(pmd, NULL, efcnum);
}

// EFCDRV.ASM:25
void pmd_eff_on(struct pmd *pmd, uint8_t efcnum) {
    bool ok = int60_enter(pmd, 0x03);
    if (ok && pmd->board != 0) eff_on(pmd, efcnum);
    int60_exit(pmd);
}

// EFCDRV.ASM:27
// Starts the effect named by efcnum. A negative effect number goes to PPSDRV when
// PPSDRV is resident and the effect driver is off; everything else uses the
// SSG effect table.
static void eff_main(struct pmd *pmd, struct pmd_qq *part, uint8_t efcnum) {
    if (pmd->effflag != 0) return; // the effects are disabled

    if (pmd->ppsdrv_flag != 0 && (int8_t)efcnum < 0) {
        // EFCDRV.ASM:40 ppsdrv
        if (pmd->effon >= 2) return; // an ordinary effect is sounding
        pmd->parts[part9].partmask |= 2;
        pmd->effon = 1; // priority 1 (ppsdrv)
        pmd->psgefcnum = efcnum;

        uint8_t detune = 0;
        uint8_t volume = 15;
        if ((pmd->hosei_flag & 1) != 0) {
            // BH = the low byte of the part's detune
            detune = (uint8_t)part->detune;
            volume = 15;
        }
        if ((pmd->hosei_flag & 2) != 0) {
            if (part->volume < 15) volume = part->volume;
            uint8_t fade = pmd->fadeout_volume;
            if (fade != 0) {
                uint16_t product = (uint16_t)volume * (uint8_t)(-fade);
                volume = (uint8_t)(product >> 8);
            }
        }
        if (volume == 0) return;
        volume ^= 0x0f;
        if (pmd->pps != NULL) {
            struct pmd_pps_keyon keyon = {
                .num = (uint8_t)(efcnum & 0x7f),
                .detune = detune,
                .volume = volume,
            };
            pmd->pps->keyon(keyon, pmd->user_data);
        }
        return;
    }

    // EFCDRV.ASM:84 eg_nonppsdrv
    pmd->psgefcnum = efcnum;
    if (efcnum >= eff_count) return; // no such effect; the original reads past the table
    const uint8_t *entry = &efftbl[(unsigned)efcnum * 3];
    if (pmd->effon > entry[0]) return; // a higher-priority effect is sounding

    if (pmd->ppsdrv_flag != 0 && pmd->pps != NULL) {
        pmd->pps->keyoff(pmd->user_data);
    }
    const uint8_t *src = &efftbl[(unsigned)(entry[1] | (entry[2] << 8))];
    uint8_t priority = entry[0];
    pmd->parts[part9].partmask |= 2;
    efffor(pmd, &src); // the first frame
    pmd->effon = priority;
}

// EFCDRV.ASM:118
// Advances the SSG effect by one Timer A tick.
static void effplay(struct pmd *pmd) {
    if (--pmd->effcnt != 0) {
        effsweep(pmd);
        return;
    }
    const uint8_t *src = pmd->effadr;
    efffor(pmd, &src);
}

// EFCDRV.ASM:178
// Reads one data byte and writes it to the register in reg, then steps reg.
static void efsnd(struct pmd *pmd, const uint8_t **src, uint8_t *reg,
                  uint8_t *value) {
    *value = *(*src)++;
    opnset44(pmd, (uint16_t)((*reg << 8) | *value));
    (*reg)++;
}

// EFCDRV.ASM:125
// Plays one effect frame from src and stores the cursor for the sweep ticks.
static void efffor(struct pmd *pmd, const uint8_t **src) {
    uint8_t reg;
    uint8_t value;

    uint8_t data = *(*src)++;
    if (data == 0xff) {
        effend(pmd);
        return;
    }
    pmd->effcnt = data;

    reg = 4; // tone frequency
    efsnd(pmd, src, &reg, &value);
    uint8_t low = value;
    efsnd(pmd, src, &reg, &value);
    uint8_t high = value;
    pmd->eswthz = (uint16_t)((high << 8) | low);

    pmd->eswnhz = **src;
    reg = 6; // noise frequency
    efsnd(pmd, src, &reg, &value);
    pmd->psnoi_last = value;

    // The mix data byte: bit 3 is the tone and bit 5 the noise enable.
    data = *(*src)++;
    value = (uint8_t)(((data << 2) | (data >> 6)) & 0x24);
    uint8_t reg07 = get07(pmd);
    value = (uint8_t)(value | (reg07 & 0xdb));
    opnset44(pmd, (uint16_t)((7 << 8) | value));

    reg = 10; // volume, envelope frequency (two bytes), and envelope pattern
    efsnd(pmd, src, &reg, &value);
    efsnd(pmd, src, &reg, &value);
    efsnd(pmd, src, &reg, &value);
    efsnd(pmd, src, &reg, &value);

    pmd->eswtst = (uint16_t)(int16_t)(int8_t)*(*src)++;
    data = *(*src)++;
    pmd->eswnst = data;
    pmd->eswnct = data & 0x0f;
    pmd->effadr = *src;
}

// EFCDRV.ASM:184
// effoff only selects the driver's data segment before falling into effend.
void pmd_effoff(struct pmd *pmd) {
    bool ok = int60_enter(pmd, 0x04);
    if (ok && pmd->board != 0) effend(pmd);
    int60_exit(pmd);
}

// EFCDRV.ASM:187
// Silences the SSG effect: PPSDRV is keyed off, the SSG volume goes to its
// minimum, and the noise channel is cut.
static void effend(struct pmd *pmd) {
    if (pmd->ppsdrv_flag != 0 && pmd->pps != NULL) {
        pmd->pps->keyoff(pmd->user_data);
    }
    opnset44(pmd, 0x0a00); // volume min
    uint8_t value = get07(pmd);
    value = (uint8_t)((value & 0xdb) | 0x24);
    opnset44(pmd, (uint16_t)((7 << 8) | value));
    pmd->effon = 0;
    pmd->psgefcnum = 0xff;
}

// EFCDRV.ASM:210
// Applies the tone and noise sweeps of the current effect frame.
static void effsweep(struct pmd *pmd) {
    uint16_t tone = (uint16_t)(pmd->eswthz + pmd->eswtst);
    pmd->eswthz = tone;
    opnset44(pmd, (uint16_t)((4 << 8) | (uint8_t)tone));
    opnset44(pmd, (uint16_t)((5 << 8) | (uint8_t)(tone >> 8)));
    // The original rewrites the mix register with its own contents.
    opnset44(pmd, (uint16_t)((7 << 8) | get07(pmd)));

    uint8_t value = pmd->eswnst;
    if (value == 0) return; // no noise sweep

    if (--pmd->eswnct != 0) return;
    pmd->eswnct = value & 0x0f;

    value = (uint8_t)((int8_t)value >> 4);
    pmd->eswnhz = (uint8_t)(pmd->eswnhz + value);
    value = pmd->eswnhz;
    opnset44(pmd, (uint16_t)((6 << 8) | value));
    pmd->psnoi_last = value;
}

// :392
// Plays one Timer A tick of the FM effect through part_e.
static void fm_efcplay(struct pmd *pmd) {
    pmd->prgdat_adr2 = pmd->efcdat + (pmd->efcdat[254] | (pmd->efcdat[255] << 8));

    struct pmd_qq *part = &pmd->part_e;
#if board2
    uint16_t port1 = pmd->fm_port1;
    uint16_t port2 = pmd->fm_port2;
    uint8_t partb = pmd->partb;
    uint8_t fmsel = pmd->fmsel;
    pmd->partb = 3;
    sel46(pmd); // the handler must stay on the FM2 bank even if mmain runs
    uint8_t *src = fmmain(pmd, part);
    pmd->partb = partb;
    pmd->fmsel = fmsel;
    pmd->fm_port1 = port1;
    pmd->fm_port2 = port2;
#else
    uint8_t partb = pmd->partb;
    pmd->partb = 3;
    uint8_t *src = fmmain(pmd, part);
    pmd->partb = partb;
#endif
    if (*src == 0x80 && part->leng == 0) fm_effect_off(pmd);
}

// :377
uint16_t pmd_get_ss(struct pmd *pmd) {
    int60_enter(pmd, 0x05);
    uint16_t bar = pmd->board != 0 ? pmd->syousetu : PMD_NO_BOARD;
    int60_exit(pmd);
    return bar;
}

// :381
struct pmd_status pmd_get_status(struct pmd *pmd) {
    int60_enter(pmd, 0x0a);
    struct pmd_status result;
    if (pmd->board != 0) {
        result.status = pmd->status;
        result.status2 = pmd->status2;
    } else {
        result.status = PMD_NO_BOARD;
        result.status2 = PMD_NO_BOARD;
    }
    int60_exit(pmd);
    return result;
}

// :386
void pmd_fout(struct pmd *pmd, uint8_t speed) {
    int60_enter(pmd, 0x02);
    if (pmd->board != 0) pmd->fadeout_speed = speed;
    int60_exit(pmd);
}

// :433
bool pmd_mstart_f(struct pmd *pmd) {
    bool ok = int60_enter(pmd, 0x00);
    if (ok && pmd->board != 0) {
        if ((pmd->TimerAflag | pmd->TimerBflag) != 0) {
            pmd->music_flag |= 1;
            ok = false;
        } else {
            mstart(pmd);
        }
    }
    int60_exit(pmd);
    return ok;
}

// :440
// Starts playback. mstart_f's timer-busy check lives in pmd_mstart_f.
static void mstart(struct pmd *pmd) {
    // :445
    pmd->music_flag &= 0xfe;

    // :449
    mstop(pmd);

    // :453
    data_init(pmd);
    play_init(pmd);
    pmd->fadeout_volume = 0;

#if board2 && adpcm
#if ademu
    // :461 ADPCM emulation off, and the PCM part masked with bit 4.
    pmd->adpcm_emulate = 0;
    {
        const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
        if (drv != NULL) drv->set_adpcm_emulation(false, pmd->user_data);
    }
    pmd->parts[part10].partmask |= 0x10;
#else
    // :466 NEC ADPCM is unavailable: mask the PCM part.
    if (pmd->pcm_gs_flag != 0) {
        pmd->parts[part10].partmask |= 0x04;
    }
#endif
#endif
#if ppz
    // :481 PPZ8 initialization.
    {
        const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
        if (drv != NULL) {
            drv->set_removal_protection(true, pmd->user_data);
            drv->init(pmd->user_data);
            drv->stop_all(pmd->user_data);
        }
    }
#endif

    // :490
    opn_init(pmd);

    // :497
    setint(pmd);
    pmd->play_flag = 1;
    pmd->mstart_flag++;
}

// :505
// Initializes each part's start address and defaults.
static void play_init(struct pmd *pmd) {
    uint8_t *src = pmd->mmlbuf;
    pmd->x68_flg = src[-1];

    // :510 The first word of the module is 2 * (max_part2 + 1) for an empty
    // module, in which case there is no program-change area.
    if (src[0] != 2 * (max_part2 + 1)) {
        uint16_t offset = (uint16_t)(src[2 * (max_part2 + 1)] |
                                     (src[2 * (max_part2 + 1) + 1] << 8));
        pmd->prgdat_adr = src + offset;
        pmd->prg_flg = 1;
    } else {
        pmd->prg_flg = 0;
    }

    // :520
    for (unsigned i = 0; i < max_part2; i++) {
        struct pmd_qq *part = pmd->part_data_table[i];
        uint16_t offset = (uint16_t)(src[0] | (src[1] << 8));
        src += 2;
        uint8_t *address = pmd->mmlbuf + offset;
        if (*address == 0x80) address = NULL;
        part->address = address;
        part->leng = 1;
        part->keyoff_flag = 0xff;
        part->mdc = 0xff;
        part->mdc2 = 0xff;
        part->_mdc = 0xff;
        part->_mdc2 = 0xff;
        part->onkai = 0xff;
        part->onkai_def = 0xff;

        if (i < 6) {
            // :544 FM1-FM6.
            part->volume = 108;
            part->fmpan = 0xc0;
#if board2
            part->slotmask = 0xf0;
            part->neiromask = 0xff;
#else
            if (i < 3) {
                part->slotmask = 0xf0;
                part->neiromask = 0xff;
            } else {
                part->partmask |= 0x20; // s0
            }
#endif
        } else if (i < 9) {
            // :563 PSG1-PSG3.
            part->volume = 8;
            part->psgpat = 7;
            part->envf = 3;
        } else if (i == 9) {
#if board2
            // :575 Part 9 (OPNA/ADPCM or PCM).
            part->volume = 128;
#if adpcm
            part->fmpan = 0xc0;
#endif
#if pcm
            part->fmpan = 0;
            pmd->pcm86_pan_flag = 0;
            pmd->revpan = 0;
#endif
#endif
        } else if (i == 10) {
            // :597 Part 10 (rhythm).
            part->volume = 15;
        }
    }

    // :605
    uint16_t offset = (uint16_t)(src[0] | (src[1] << 8));
    pmd->radtbl = pmd->mmlbuf + offset;
    pmd->rhyadr = &pmd->rhydmy;
}

// :626
// Initializes the playback data area.
static void data_init(struct pmd *pmd) {
    pmd->fadeout_volume = 0;
    pmd->fadeout_speed = 0;
    pmd->fadeout_flag = 0;

    // :632
    data_init2(pmd);
}

// :632
// Clears the part work areas and the playback state, keeping only the part
// masks and the key-on counters. ff_music_main restarts the module through it
// without resetting the fadeout fields.
static void data_init2(struct pmd *pmd) {
    for (int i = 0; i < max_part1; i++) {
        struct pmd_qq *part = &pmd->parts[i];
        uint8_t mask = part->partmask;
        uint8_t keyon = part->keyon_flag;
        memset(part, 0, sizeof(*part));
        part->partmask = mask & 0x0f;
        part->keyon_flag = keyon;
        part->onkai = 0xff;
        part->onkai_def = 0xff;
    }

    // :665
    pmd->tieflag = 0;
    pmd->status = 0;
    pmd->status2 = 0;
    pmd->syousetu = 0;
    pmd->opncount = 0;
    pmd->TimerAtime = 0;
    pmd->lastTimerAtime = 0;

    // :673 rep stosw clears both the front and back key-on bytes.
    pmd->omote_key1 = 0;
    pmd->omote_key2 = 0;
    pmd->omote_key3 = 0;
    pmd->ura_key1 = 0;
    pmd->ura_key2 = 0;
    pmd->ura_key3 = 0;

    pmd->fm3_alg_fb = 0;
    pmd->af_check = 0;

    pmd->pcmstart = 0;
    pmd->pcmstop = 0;
    pmd->pcmrepeat1 = 0;
    pmd->pcmrepeat2 = 0;
    pmd->pcmrelease = 0x8000;

    pmd->kshot_dat = 0;
    pmd->rshot_dat = 0;
    pmd->last_shot_data = 0;

    pmd->slotdetune_flag = 0;
    pmd->slot_detune1 = 0;
    pmd->slot_detune2 = 0;
    pmd->slot_detune3 = 0;
    pmd->slot_detune4 = 0;

    pmd->slot3_flag = 0;
    pmd->ch3mode = 0x3f;

    pmd->fmsel = 0;

    pmd->syousetu_lng = 96;

    pmd->fm_port1 = pmd->fm1_port1;
    pmd->fm_port2 = pmd->fm1_port2;

    pmd->fm_voldown = pmd->_fm_voldown;
    pmd->ssg_voldown = pmd->_ssg_voldown;
    pmd->pcm_voldown = pmd->_pcm_voldown;
#if ppz
    pmd->ppz_voldown = pmd->_ppz_voldown;
#endif
    pmd->rhythm_voldown = pmd->_rhythm_voldown;

    pmd->pcm86_vol = pmd->_pcm86_vol;
}

// :718
// Initializes the OPN registers for playback.
static void opn_init(struct pmd *pmd) {
    uint16_t reg;

    opnset44(pmd, 0x2983);

    pmd->psnoi = 0;
    if (pmd->effon == 0) {
        opnset44(pmd, 0x0600); // PSG noise
        pmd->psnoi_last = 0;
    }

    // :729 SSG-EG reset.
#if board2
    sel44(pmd);
    for (unsigned bank = 2; bank != 0; bank--) {
        uint8_t n = 15;
        reg = 0x9000; // SSG-EG registers
        do {
            if ((n & 3) != 0) opnset(pmd, reg);
            reg = (uint16_t)(reg + 0x0100);
            n--;
        } while (n != 0);
        sel46(pmd);
    }
    sel44(pmd);
#else
    {
        uint8_t n = 15;
        reg = 0x9000; // SSG-EG registers
        do {
            if ((n & 3) != 0) opnset(pmd, reg);
            reg = (uint16_t)(reg + 0x0100);
            n--;
        } while (n != 0);
    }
#endif

#if !board2
    // :745 A YM2203 stops here.
    if (pmd->ongen == 0) return;
#endif

    // :753 PAN and hard LFO defaults for the YM2608.
#if board2
    for (unsigned bank = 2; bank != 0; bank--) {
        // The original assumes mmain cannot interrupt here and skips the
        // initial sel44.
        reg = 0xb4c0;
        uint8_t chan = 3;
        do {
            bool skip = pmd->fm_effec_flag != 0 && chan == 1 && bank == 1;
            if (!skip) opnset(pmd, reg);
            reg = (uint16_t)(reg + 0x0100);
            chan--;
        } while (chan != 0);
        sel46(pmd);
    }
    sel44(pmd);
#else
    reg = 0xb4c0;
    {
        uint8_t chan = 3;
        do {
            bool skip = pmd->fm_effec_flag != 0 && chan == 1;
            if (!skip) opnset(pmd, reg);
            reg = (uint16_t)(reg + 0x0100);
            chan--;
        } while (chan != 0);
    }
#endif

    // :795
    reg = 0x2200;
    pmd->port22h = (uint8_t)reg;
    opnset44(pmd, reg);

#if board2
    // :801 Rhythm defaults: pan middle, volume 15.
    for (int i = 0; i < 6; i++) pmd->rdat[i] = 0xcf;
    opnset44(pmd, 0x10ff); // rhythm all dump

    // :811 Rhythm total level.
    uint8_t volume = 48;
    uint8_t voldown = pmd->rhythm_voldown;
    if (voldown != 0) {
        volume = (uint8_t)(volume << 2);
        uint16_t product = (uint16_t)(uint8_t)(-voldown) * volume;
        volume = (uint8_t)(product >> 8);
        volume >>= 1;
        volume >>= 1;
    }
    pmd->rhyvol = volume;
    opnset44(pmd, (uint16_t)((0x11 << 8) | volume));

#if !ademu
    // :826 PCM reset and limit.
    if (pmd->pcm_gs_flag != 1) {
        opnset46(pmd, 0x0cff);
        opnset46(pmd, 0x0dff);
    }
#endif
#if ppz || ademu
    // :848 PPZ8 pan initialization: all eight channels to centre (5).
    {
        const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
        if (drv != NULL) {
            for (uint8_t chan = 8; chan != 0; chan--) {
                drv->set_pan((uint8_t)(chan - 1), 5, pmd->user_data);
            }
        }
    }
#endif
#else
    // :839 YM2608 on a normal board: direct reset through the upper ports.
    rdychk(pmd, pmd->fm2_port1);
    outb(pmd, pmd->fm2_port1, 0x10);
    _wait(pmd);
    outb(pmd, pmd->fm2_port2, 0x80);
    _wait(pmd);
    outb(pmd, pmd->fm2_port2, 0x18);
#endif
}

// :874
bool pmd_mstop_f(struct pmd *pmd) {
    bool ok = int60_enter(pmd, 0x01);
    if (ok && pmd->board != 0) {
        if ((pmd->TimerAflag | pmd->TimerBflag) != 0) {
            pmd->music_flag |= 2;
            ok = false;
        } else {
            pmd->fadeout_flag = 0;
            mstop(pmd);
        }
    }
    int60_exit(pmd);
    return ok;
}

// :883
static void mstop(struct pmd *pmd) {
    pmd->music_flag &= 0xfd;
    pmd->play_flag = 0;
    pmd->pause_flag = 0;
    pmd->fadeout_speed = 0;
    pmd->status2 = 0xff;
    pmd->fadeout_volume = 0xff;
    silence(pmd);
}

// :900
// The music player's Timer B entry: runs every part.
static void mmain(struct pmd *pmd) {
    pmd->loop_work = 3;

    // :904
    if (pmd->x68_flg == 0) {
        pmd->partb = 1;
        psgmain(pmd, &pmd->parts[part7]);
        pmd->partb = 2;
        psgmain(pmd, &pmd->parts[part8]);
        pmd->partb = 3;
        psgmain(pmd, &pmd->parts[part9]);
    }

    // :918
#if board2
    sel46(pmd);
    pmd->partb = 1;
    fmmain(pmd, &pmd->parts[part4]);
    pmd->partb = 2;
    fmmain(pmd, &pmd->parts[part5]);
    pmd->partb = 3;
    fmmain(pmd, &pmd->parts[part6]);
    sel44(pmd);
#endif
    pmd->partb = 1;
    fmmain(pmd, &pmd->parts[part1]);
    pmd->partb = 2;
    fmmain(pmd, &pmd->parts[part2]);
    pmd->partb = 3;
    fmmain(pmd, &pmd->parts[part3]);
    fmmain(pmd, &pmd->parts[part3b]);
    fmmain(pmd, &pmd->parts[part3c]);
    fmmain(pmd, &pmd->parts[part3d]);

    // :967
    if (pmd->x68_flg == 0) {
        rhythmmain(pmd, &pmd->parts[part11]);
#if board2
        pcmmain(pmd, &pmd->parts[part10]);
#endif
#if ppz
        pmd->partb = 0;
        ppzmain(pmd, &pmd->parts[part10a]);
        pmd->partb = 1;
        ppzmain(pmd, &pmd->parts[part10b]);
        pmd->partb = 2;
        ppzmain(pmd, &pmd->parts[part10c]);
        pmd->partb = 3;
        ppzmain(pmd, &pmd->parts[part10d]);
        pmd->partb = 4;
        ppzmain(pmd, &pmd->parts[part10e]);
        pmd->partb = 5;
        ppzmain(pmd, &pmd->parts[part10f]);
        pmd->partb = 6;
        ppzmain(pmd, &pmd->parts[part10g]);
        pmd->partb = 7;
        ppzmain(pmd, &pmd->parts[part10h]);
#endif
    }

    // :994 mmain_exit
    if (pmd->loop_work == 0) return;

    // :999
    for (int i = 0; i < max_part1; i++) {
        struct pmd_qq *part = pmd->part_data_table[i];
        if (part->loopcheck == 3) continue;
        part->loopcheck = 0;
    }

    if (pmd->loop_work == 3) {
        // :1021
        pmd->status2 = 0xff;
        return;
    }

    // :1013
    pmd->status2++;
    if (pmd->status2 == 0xff) pmd->status2 = 1;
}

#if board2
// :1029
static void sel46(struct pmd *pmd) {
    pmd->fm_port1 = pmd->fm2_port1;
    pmd->fm_port2 = pmd->fm2_port2;
    pmd->fmsel = 1;
}

// :1039
static void sel44(struct pmd *pmd) {
    pmd->fm_port1 = pmd->fm1_port1;
    pmd->fm_port2 = pmd->fm1_port2;
    pmd->fmsel = 0;
}
#endif

// :1053
// The FM part main loop. `src` is the part's data cursor and `part` its work
// area; the cursor is returned for fm_efcplay, whose end-of-effect test reads
// the live cursor (:407).
static uint8_t *fmmain(struct pmd *pmd, struct pmd_qq *part) {
    uint8_t *src = part->address;
    if (src == NULL) return src;

    if (part->partmask != 0) goto fmmain_nonplay;

    // :1058
    part->leng--;
    uint8_t remaining = part->leng;
    uint8_t cmd;

    // :1062 Keyoff when the remaining length reaches Q.
    if ((part->keyoff_flag & 3) == 0 && remaining <= part->qdat) {
        keyoff(pmd, part);
        part->keyoff_flag = 0xff;
    }

    if (remaining != 0) goto mpexit;

mp10:
    part->lfoswi &= 0xf7; // portamento off

mp1:
    cmd = lodsb(&src);
    if (cmd < 0x80) goto mp2;
    if (cmd == 0x80) goto mp15;

    switch (commands(pmd, part, &src)) {
    case PMD_CMD_MP1: goto mp1;
    case PMD_CMD_FMMNP_1: goto fmmnp_1;
    case PMD_CMD_PORTA_RETURN: goto porta_return;
    default: break;
    }
    goto mp1;

mp15:
    // :1089 End of music, or a loop if "L" was seen.
    src--;
    part->address = src;
    part->loopcheck = 3;
    part->onkai = 0xff;
    if (part->partloop == NULL) goto mpexit;
    src = part->partloop;
    part->loopcheck = 1;
    goto mp1;

mp2:
    // :1101 Note: set the pitch and gate.
    cmd = lfoinit(pmd, part, cmd);
    cmd = oshift(part, cmd);
    fnumset(pmd, part, cmd);

    cmd = lodsb(&src);
    part->leng = cmd;
    calc_q(pmd, part, &src);

porta_return:
    // :1110
    if (part->volpush == 0) goto mp_new;
    if (part->onkai == 0xff) goto mp_new;
    pmd->volpush_flag--;
    if (pmd->volpush_flag == 0) goto mp_new;
    pmd->volpush_flag = 0;
    part->volpush = 0;

mp_new:
    volset(pmd, part);
    otodasi(pmd, part);
    keyon(pmd, part);
    part->keyon_flag++;

    part->address = src;
    pmd->tieflag = 0;
    pmd->volpush_flag = 0;
    part->keyoff_flag = 0;
    if (*src == 0xfb) part->keyoff_flag = 2; // '&' immediately follows
    mnp_ret(pmd, part);
    return src;

mpexit:
    // :1134 LFO, portamento, and fadeout processing.
#if board2
    if (part->hldelay_c != 0) {
        part->hldelay_c--;
        if (part->hldelay_c == 0) {
            opnset(pmd, (uint16_t)(((pmd->partb + 0xb3) << 8) | part->fmpan));
        }
    }
#endif
    if (part->sdelay_c != 0) {
        part->sdelay_c--;
        if (part->sdelay_c == 0 && (part->keyoff_flag & 1) == 0) keyon(pmd, part);
    }

    uint8_t lfosw = part->lfoswi;
    pmd->lfo_switch = lfosw & 8;
    if (lfosw != 0) {
        if ((lfosw & 3) != 0) {
            if (lfo(pmd, part)) pmd->lfo_switch |= lfosw & 3;
        }
        if ((lfosw & 0x30) != 0) {
            lfo_change(part);
            if (lfo(pmd, part)) {
                lfo_change(part);
                pmd->lfo_switch |= part->lfoswi & 0x30;
            } else {
                lfo_change(part);
            }
        }
        if ((pmd->lfo_switch & 0x19) != 0) {
            if ((pmd->lfo_switch & 8) != 0) porta_calc(pmd, part);
            otodasi(pmd, part);
        }
        if ((pmd->lfo_switch & 0x22) != 0) goto vol_set;
    }
    if (pmd->fadeout_speed == 0) {
        mnp_ret(pmd, part);
        return src;
    }

vol_set:
    volset(pmd, part);

    mnp_ret(pmd, part);
    return src;

fmmain_nonplay:
    // :1271
    part->keyoff_flag = 0xff;
    part->leng--;
    if (part->leng != 0) {
        mnp_ret(pmd, part);
        return src;
    }

    if ((part->partmask & 2) != 0 && pmd->fm_effec_flag == 0) {
        part->partmask &= 0xfd;
        if (part->partmask == 0) goto mp10;
    }

fmmnp_1:
    cmd = lodsb(&src);
    if (cmd == 0x80) goto fmmnp_2;
    if (cmd < 0x80) goto fmmnp_3;
    switch (commands(pmd, part, &src)) {
    case PMD_CMD_MP1: goto mp1;
    case PMD_CMD_FMMNP_1: goto fmmnp_1;
    case PMD_CMD_PORTA_RETURN: goto porta_return;
    default: break;
    }
    goto fmmnp_1;

fmmnp_2:
    // :1291
    src--;
    part->address = src;
    part->loopcheck = 3;
    part->onkai = 0xff;
    if (part->partloop == NULL) { fmmnp_4(pmd, part); return src; }
    src = part->partloop;
    part->loopcheck = 1;
    goto fmmnp_1;

fmmnp_3:
    // :1306 A rest.
    rest_note(pmd, part, &src);
    return src;
}

// :1306
// The rest-note path. The original shares it with every part loop by jumping
// into fmmain's fmmnp_3.
static void rest_note(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    part->fnum = 0;
    part->onkai = 0xff;
    part->onkai_def = 0xff;
    part->leng = lodsb(src);
    part->keyon_flag++;
    part->address = *src;

    pmd->volpush_flag--;
    if (pmd->volpush_flag == 0) {
        fmmnp_4(pmd, part);
        return;
    }
    part->volpush = 0;

    pmd->tieflag = 0;
    pmd->volpush_flag = 0;
    mnp_ret(pmd, part);
}

#if ppz
// :250 _ppz
// Gives PPZ8's own interrupt handler a turn when its directly callable entry
// was obtained. With ppz_call_seg == 1 PPZ8 handles its interrupts itself and
// the macro does nothing.
static void ppz_int_hook(struct pmd *pmd) {
    if (pmd->ppz_call_seg == 2 && pmd->ppz8 != NULL &&
        pmd->ppz8->fm_int != NULL) {
        pmd->ppz8->fm_int(pmd->user_data);
    }
}
#else
static void ppz_int_hook(struct pmd *pmd) { (void)pmd; }
#endif

// :1204
static void mnp_ret(struct pmd *pmd, struct pmd_qq *part) {
    pmd->loop_work &= part->loopcheck;
    ppz_int_hook(pmd);
}

// :1215
// Computes the gate time from the Q/QM/QR commands.
static void calc_q(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    if (**src == 0xc1) {
        (*src)++; // slur
        part->qdat = 0;
        return;
    }

    uint8_t q = part->qdata;
    if (part->qdatb != 0) {
        uint16_t product = (uint16_t)part->leng * part->qdatb;
        q = (uint8_t)(q + (product >> 8));
    }

    if (part->qdat3 != 0) {
        uint16_t max = (uint16_t)((part->qdat3 & 0x7f) + 1);
        uint8_t r = rnd(pmd, max);
        if ((part->qdat3 & 0x80) != 0) {
            if (q >= r) {
                q -= r;
            } else {
                q = 0;
            }
        } else {
            q = (uint8_t)(q + r);
        }
    }

    if (part->qdat2 != 0) {
        uint8_t limit = (uint8_t)(part->leng - part->qdat2);
        if ((part->leng < part->qdat2)) {
            part->qdat = 0;
            return;
        }
        if (q >= limit) q = limit;
    }
    part->qdat = q;
}

// :1318
static void fmmnp_4(struct pmd *pmd, struct pmd_qq *part) {
    pmd->tieflag = 0;
    pmd->volpush_flag = 0;
    mnp_ret(pmd, part);
}

// :1329
// The SSG part main loop.
static void psgmain(struct pmd *pmd, struct pmd_qq *part) {
    uint8_t *src = part->address;
    if (src == NULL) return;

    if (part->partmask != 0) goto psgmain_nonplay;

    part->leng--;
    uint8_t remaining = part->leng;
    uint8_t cmd;

    if ((part->keyoff_flag & 3) == 0 && remaining <= part->qdat) {
        keyoffp(pmd, part);
        part->keyoff_flag = 0xff;
    }

    if (remaining != 0) goto mpexitp;
    part->lfoswi &= 0xf7;

mp1p:
    cmd = lodsb(&src);
    if (cmd < 0x80) goto mp2p;
    if (cmd == 0x80) goto mp15p;

mp1cp:
    switch (commandsp(pmd, part, &src)) {
    case PMD_CMD_MP1P: goto mp1p;
    case PMD_CMD_PSGMNP_1: goto psgmnp_1;
    case PMD_CMD_PORTA_RETURNP: goto porta_returnp;
    default: break;
    }
    goto mp1p;

mp15p:
    src--;
    part->address = src;
    part->loopcheck = 3;
    part->onkai = 0xff;
    if (part->partloop == NULL) goto mpexitp;
    src = part->partloop;
    part->loopcheck = 1;
    goto mp1p;

mp2p:
    cmd = lfoinitp(pmd, part, cmd);
    cmd = oshift(part, cmd);
    fnumsetp(pmd, part, cmd);

    cmd = lodsb(&src);
    part->leng = cmd;
    calc_q(pmd, part, &src);

porta_returnp:
    if (part->volpush == 0) goto mp_newp;
    if (part->onkai == 0xff) goto mp_newp;
    pmd->volpush_flag--;
    if (pmd->volpush_flag == 0) goto mp_newp;
    pmd->volpush_flag = 0;
    part->volpush = 0;

mp_newp:
    volsetp(pmd, part);
    otodasip(pmd, part);
    keyonp(pmd, part);
    part->keyon_flag++;

    part->address = src;
    pmd->tieflag = 0;
    pmd->volpush_flag = 0;
    part->keyoff_flag = 0;
    if (*src == 0xfb) part->keyoff_flag = 2;
    mnp_ret(pmd, part);
    return;

mpexitp:;
    // :1410
    uint8_t lfosw = part->lfoswi;
    pmd->lfo_switch = (uint8_t)(lfosw & 8);
    if (lfosw != 0) {
        if ((lfosw & 3) != 0) {
            if (lfo(pmd, part)) pmd->lfo_switch |= lfosw & 3;
        }
        if ((lfosw & 0x30) != 0) {
            lfo_change(part);
            if (lfo(pmd, part)) {
                lfo_change(part);
                pmd->lfo_switch |= part->lfoswi & 0x30;
            } else {
                lfo_change(part);
            }
        }
        if ((pmd->lfo_switch & 0x19) != 0) {
            if ((pmd->lfo_switch & 8) != 0) porta_calc(pmd, part);
            otodasip(pmd, part);
        }
    }

    if (!soft_env(pmd, part) && (pmd->lfo_switch & 0x22) == 0 &&
        pmd->fadeout_speed == 0) {
        mnp_ret(pmd, part);
    return;
    }
    volsetp(pmd, part);
    mnp_ret(pmd, part);
    return;

psgmain_nonplay:
    // :1463
    part->keyoff_flag = 0xff;
    part->leng--;
    if (part->leng != 0) {
        mnp_ret(pmd, part);
        return;
    }
    part->lfoswi &= 0xf7;

psgmnp_1:
    cmd = lodsb(&src);
    if (cmd == 0x80) goto psgmnp_2;
    if (cmd < 0x80) goto psgmnp_4;
    if (cmd == 0xda && ssgdrum_check(pmd, part, cmd)) goto mp1cp;
    switch (commandsp(pmd, part, &src)) {
    case PMD_CMD_MP1P: goto mp1p;
    case PMD_CMD_PSGMNP_1: goto psgmnp_1;
    case PMD_CMD_PORTA_RETURNP: goto porta_returnp;
    default: break;
    }
    goto psgmnp_1;

psgmnp_2:
    src--;
    part->address = src;
    part->loopcheck = 3;
    part->onkai = 0xff;
    if (part->partloop == NULL) { fmmnp_4(pmd, part); return; }
    src = part->partloop;
    part->loopcheck = 1;
    goto psgmnp_1;

psgmnp_4:
    if (ssgdrum_check(pmd, part, cmd)) goto mp2p;
    part->fnum = 0;
    part->onkai = 0xff;
    part->onkai_def = 0xff;
    cmd = lodsb(&src);
    part->leng = cmd;
    part->keyon_flag++;
    part->address = src;
    pmd->volpush_flag--;
    if (pmd->volpush_flag != 0) part->volpush = 0;
    fmmnp_4(pmd, part);
}

// :1508
// Returns true when the SSG drum should be stopped and the SSG part revived.
static bool ssgdrum_check(struct pmd *pmd, struct pmd_qq *part, uint8_t note) {
    if ((part->partmask & 1) != 0) return false;
    if ((part->partmask & 2) == 0) return false;
    if (pmd->effon >= 2) return false;
    if ((note & 0x0f) == 0x0f) return false;
    if (pmd->effon == 1) effend(pmd);
    part->partmask &= 0xfd;
    if (part->partmask != 0) return false;
    return true;
}

// :1539
// The rhythm part main loop.
static void rhythmmain(struct pmd *pmd, struct pmd_qq *part) {
    uint8_t *src = part->address;
    uint8_t cmd;
    if (src == NULL) return;
    part->leng--;
    if (part->leng != 0) {
        mnp_ret(pmd, part);
        return;
    }

rhyms0:
    {
        uint8_t *data = (uint8_t *)pmd->rhyadr;
rhyms00:
        cmd = *data++;
        if (cmd == 0xff) goto reom;
        if ((cmd & 0x80) != 0) goto rhythmon;
        pmd->kshot_dat = 0;
rlnset:
        cmd = *data++;
        pmd->rhyadr = data;
        part->leng = cmd;
        part->keyon_flag++;
        { fmmnp_4(pmd, part); return; }

rhythmon:
        if ((cmd & 0x40) == 0) goto rhy_shot;
        uint8_t *save_src = src;
        src = data;
        (void)commandsr(pmd, part, &src);
        data = src;
        src = save_src;
        goto rhyms00;

rhy_shot:
        if (part->partmask == 0) goto r_nonmask;
        pmd->kshot_dat = 0;
        data++;
        goto rlnset;

r_nonmask:
        {
            uint8_t high = cmd;
            cmd = *data++;
            uint16_t shot = (uint16_t)(((uint16_t)high << 8) | cmd) & 0x3fff;
            pmd->kshot_dat = shot;
            if (shot == 0) goto rlnset;
            pmd->rhyadr = data;

#if board2
            if (pmd->kp_rhythm_flag != 0) {
                uint16_t bits = shot;
                const uint8_t *entry = &rhydat[0][0];
                for (unsigned i = 0; i < 11; i++) {
                    if ((bits & 1) != 0) {
                        opnset44(pmd, (uint16_t)((entry[0] << 8) | entry[1]));
                        uint8_t key = (uint8_t)(entry[2] & pmd->rhythmmask);
                        if (key == 0) goto rsb200;
                        if ((key & 0x80) == 0) {
                            opnset44(pmd, (uint16_t)(0x1000 | key));
                        } else {
                            opnset44(pmd, 0x1084);
                            uint8_t extra =
                                (uint8_t)(0x08 & pmd->rhythmmask);
                            if (extra == 0) goto rsb200;
                            _rwait(pmd);
                            opnset44(pmd, (uint16_t)(0x1000 | extra));
                        }
                    }
rsb200:
                    bits >>= 1;
                    entry += 3;
                }
            }
#endif
            {
                uint16_t bits = shot;
                uint8_t inst;
                if (pmd->fadeout_volume != 0) {
#if board2
                    if (pmd->kp_rhythm_flag != 0) volset2rf(pmd, pmd->rhyvol);
#endif
                    if (pmd->ppsdrv_flag == 0) goto roret;
                }
                inst = 0xff;
rolop:
                inst++;
                if (bits & 1) goto rhygo;
                bits >>= 1;
                goto rolop;
rhygo:
                effgo(pmd, part, inst);
                if (pmd->ppsdrv_flag == 0) goto roret;
                bits >>= 1; // the scanned instrument is consumed
                if (bits == 0) goto roret;
                goto rolop;
            }
roret:
            data = (uint8_t *)pmd->rhyadr;
            goto rlnset;
        }
    }

reom:
    cmd = lodsb(&src);
    if (cmd == 0x80) goto rfin;
    if (cmd < 0x80) goto re00;
    (void)commandsr(pmd, part, &src);
    goto reom;

re00:
    part->address = src;
    {
        uint16_t index = (uint16_t)((uint16_t)cmd * 2);
        uint16_t offset = (uint16_t)(pmd->radtbl[index] |
                                     (pmd->radtbl[index + 1] << 8));
        pmd->rhyadr = pmd->mmlbuf + offset;
        goto rhyms0;
    }

rfin:
    src--;
    part->address = src;
    part->loopcheck = 3;
    if (part->partloop != NULL) {
        src = part->partloop;
        part->loopcheck = 1;
        goto reom;
    }
    pmd->rhyadr = &pmd->rhydmy;
    { fmmnp_4(pmd, part); return; }
}

// :1713
static enum pmd_cmd_result commands(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    return command00(pmd, part, src, cmdtbl);
}

// :1717
static enum pmd_cmd_result commandsr(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    return command00(pmd, part, src, cmdtblr);
}

// :1721
static enum pmd_cmd_result commandsp(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    return command00(pmd, part, src, cmdtblp);
}

// :1724
static enum pmd_cmd_result command00(struct pmd *pmd, struct pmd_qq *part, uint8_t **src, const pmd_cmd_fn *table) {
    uint8_t cmd = *(*src - 1);
    if (cmd < 0xb1) {
        // :1739 The original rewinds SI and writes an end marker.
        (*src)--;
        **src = 0x80;
        return PMD_CMD_RETURN;
    }
    // :1734 PPZ8's FM entry runs before each command handler.
    ppz_int_hook(pmd);
    return table[0xff - cmd](pmd, part, src);
}

// :1745
// The command tables, indexed by 0xff - command. The FM table.
static const pmd_cmd_fn cmdtbl[0xff - 0xb1 + 1] = {
    com_at,         // ff '@'
    comq,           // fe 'q'
    comv,           // fd 'v'
    comt,           // fc
    comtie,         // fb '&'
    comd,           // fa 'D'
    comstloop,      // f9 '['
    comedloop,      // f8 ']'
    comexloop,      // f7 ':'
    comlopset,      // f6 'L'
    comshift,       // f5 '_'
    comvolup,       // f4 ')'
    comvoldown,     // f3 '('
    lfoset,         // f2
    lfoswitch_f,    // f1
    jump4,          // f0
    comy,           // ef 'y'
    jump1,          // ee
    jump1,          // ed
    panset,         // ec 'p'
    rhykey,         // eb
    rhyvs,          // ea
    rpnset,         // e9
    rmsvs,          // e8
    comshift2,      // e7
    rmsvs_sft,      // e6
    rhyvs_sft,      // e5
    hlfo_delay,     // e4
    comvolup2,      // e3
    comvoldown2,    // e2
    hlfo_set,       // e1
    hlfo_onoff,     // e0
    syousetu_lng_set, // df
    vol_one_up_fm,  // de
    vol_one_down,   // dd
    status_write,   // dc
    status_add,     // db
    porta,          // da
    jump1,          // d9
    jump1,          // d8
    jump1,          // d7
    mdepth_set,     // d6
    comdd,          // d5
    ssg_efct_set,   // d4
    fm_efct_set,    // d3
    fade_set,       // d2
    jump1,          // d1
    jump1,          // d0
    slotmask_set,   // cf
    jump6,          // ce
    jump5,          // cd
    jump1,          // cc
    lfowave_set,    // cb
    lfo_extend,     // ca
    jump1,          // c9
    slotdetune_set, // c8
    slotdetune_set2,// c7
    fm3_extpartset, // c6
    volmask_set,    // c5
    comq2,          // c4 'Q'
    panset_ex,      // c3
    lfoset_delay,   // c2
    jump0,          // c1 slur
    fm_mml_part_mask, // c0
    _lfoset,        // bf
    _lfoswitch_f,   // be
    _mdepth_set,    // bd
    _lfowave_set,   // bc
    _lfo_extend,    // bb
    _volmask_set,   // ba
    _lfoset_delay,  // b9
    tl_set,         // b8
    mdepth_count,   // b7
    fb_set,         // b6
    slot_delay,     // b5
    jump16,         // b4
    comq3,          // b3
    comshift_master,// b2 '_M'
    comq4,          // b1
};

// :1843
// The PSG table.
static const pmd_cmd_fn cmdtblp[0xff - 0xb1 + 1] = {
    jump1,          // ff
    comq,           // fe
    comv,           // fd
    comt,           // fc
    comtie,         // fb
    comd,           // fa
    comstloop,      // f9
    comedloop,      // f8
    comexloop,      // f7
    comlopset,      // f6
    comshift,       // f5
    comvolupp,      // f4
    comvoldownp,    // f3
    lfoset,         // f2
    lfoswitch,      // f1
    psgenvset,      // f0
    comy,           // ef
    psgnoise,       // ee
    psgsel,         // ed
    jump1,          // ec
    rhykey,         // eb
    rhyvs,          // ea
    rpnset,         // e9
    rmsvs,          // e8
    comshift2,      // e7
    rmsvs_sft,      // e6
    rhyvs_sft,      // e5
    jump1,          // e4
    comvolupp2,     // e3
    comvoldownp2,   // e2
    jump1,          // e1
    jump1,          // e0
    syousetu_lng_set, // df
    vol_one_up_psg, // de
    vol_one_down,   // dd
    status_write,   // dc
    status_add,     // db
    portap,         // da
    jump1,          // d9
    jump1,          // d8
    jump1,          // d7
    mdepth_set,     // d6
    comdd,          // d5
    ssg_efct_set,   // d4
    fm_efct_set,    // d3
    fade_set,       // d2
    jump1,          // d1
    psgnoise_move,  // d0
    jump1,          // cf
    jump6,          // ce
    extend_psgenvset, // cd
    detune_extend,  // cc
    lfowave_set,    // cb
    lfo_extend,     // ca
    envelope_extend,// c9
    jump3,          // c8
    jump3,          // c7
    jump6,          // c6
    jump1,          // c5
    comq2,          // c4
    jump2,          // c3
    lfoset_delay,   // c2
    jump0,          // c1
    ssg_mml_part_mask, // c0
    _lfoset,        // bf
    _lfoswitch,     // be
    _mdepth_set,    // bd
    _lfowave_set,   // bc
    _lfo_extend,    // bb
    jump1,          // ba
    _lfoset_delay,  // b9
    jump2,          // b8
    mdepth_count,   // b7
    jump1,          // b6
    jump2,          // b5
    jump16,         // b4
    comq3,          // b3
    comshift_master,// b2
    comq4,          // b1
};

// :1939
// The rhythm table.
static const pmd_cmd_fn cmdtblr[0xff - 0xb1 + 1] = {
    jump1,          // ff
    jump1,          // fe
    comv,           // fd
    comt,           // fc
    comtie,         // fb
    comd,           // fa
    comstloop,      // f9
    comedloop,      // f8
    comexloop,      // f7
    comlopset,      // f6
    jump1,          // f5
    comvolupp,      // f4
    comvoldownp,    // f3
    jump4,          // f2
    pdrswitch,      // f1
    jump4,          // f0
    comy,           // ef
    jump1,          // ee
    jump1,          // ed
    jump1,          // ec
    rhykey,         // eb
    rhyvs,          // ea
    rpnset,         // e9
    rmsvs,          // e8
    jump1,          // e7
    rmsvs_sft,      // e6
    rhyvs_sft,      // e5
    jump1,          // e4
    comvolupp2,     // e3
    comvoldownp2,   // e2
    jump1,          // e1
    jump1,          // e0
    syousetu_lng_set, // df
    vol_one_up_psg, // de
    vol_one_down,   // dd
    status_write,   // dc
    status_add,     // db
    jump1,          // da
    jump1,          // d9
    jump1,          // d8
    jump1,          // d7
    jump2,          // d6
    comdd,          // d5
    ssg_efct_set,   // d4
    fm_efct_set,    // d3
    fade_set,       // d2
    jump1,          // d1
    jump1,          // d0
    jump1,          // cf
    jump6,          // ce
    jump5,          // cd
    jump1,          // cc
    jump1,          // cb
    jump1,          // ca
    jump1,          // c9
    jump3,          // c8
    jump3,          // c7
    jump6,          // c6
    jump1,          // c5
    jump1,          // c4
    jump2,          // c3
    jump1,          // c2
    jump0,          // c1
    rhythm_mml_part_mask, // c0
    jump4,          // bf
    jump1,          // be
    jump2,          // bd
    jump1,          // bc
    jump1,          // bb
    jump1,          // ba
    jump1,          // b9
    jump2,          // b8
    jump1,          // b7
    jump1,          // b6
    jump2,          // b5
    jump16,         // b4
    jump1,          // b3
    jump1,          // b2
    jump1,          // b1
};

// :2035
static enum pmd_cmd_result jump16(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    (void)part;
    *src += 16;
    return PMD_CMD_RETURN;
}

// :2037
static enum pmd_cmd_result jump6(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    (void)part;
    *src += 6;
    return PMD_CMD_RETURN;
}

// :2039
static enum pmd_cmd_result jump5(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    (void)part;
    *src += 5;
    return PMD_CMD_RETURN;
}

// :2041
static enum pmd_cmd_result jump4(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    (void)part;
    *src += 4;
    return PMD_CMD_RETURN;
}

// :2043
static enum pmd_cmd_result jump3(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    (void)part;
    *src += 3;
    return PMD_CMD_RETURN;
}

// :2045
static enum pmd_cmd_result jump2(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    (void)part;
    *src += 2;
    return PMD_CMD_RETURN;
}

// :2047
static enum pmd_cmd_result jump1(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    (void)part;
    (*src)++;
    return PMD_CMD_RETURN;
}

// :2049
static enum pmd_cmd_result jump0(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    (void)part;
    (void)src;
    return PMD_CMD_RETURN;
}

// :2055
static enum pmd_cmd_result special_0c0h(struct pmd *pmd, struct pmd_qq *part, uint8_t **src, uint8_t cmd) {
    // The original's f6/f5 entries are unreachable because values below f7
    // are rejected above; they are kept to mirror the original table.
    static const pmd_cmd_fn table[0xff - 0xf5 + 1] = {
        vd_fm, _vd_fm, vd_ssg, _vd_ssg, vd_pcm,
        _vd_pcm, vd_rhythm, _vd_rhythm, pmd86_s, vd_ppz, _vd_ppz,
    };
    if (cmd < 0xf7) {
        (*src)--;
        **src = 0x80;
        return PMD_CMD_RETURN;
    }
    return table[0xff - cmd](pmd, part, src);
}

// :2082
static enum pmd_cmd_result pmd86_s(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)part;
    pmd->pcm86_vol = lodsb(src) & 1;
    return PMD_CMD_RETURN;
}

// :2091
static enum pmd_cmd_result vd_fm(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    return vd_main(pmd, part, src, &pmd->fm_voldown, &pmd->_fm_voldown, false);
}

// :2092
static enum pmd_cmd_result vd_main(struct pmd *pmd, struct pmd_qq *part, uint8_t **src, uint8_t *vol, uint8_t *base, bool add) {
    (void)pmd;
    (void)part;
    uint8_t value = lodsb(src);
    if (!add) {
        *vol = value;
        return PMD_CMD_RETURN;
    }
    if (value == 0) {
        *vol = *base;
        return PMD_CMD_RETURN;
    }
    if ((value & 0x80) != 0) {
        unsigned sum = (unsigned)*vol + value;
        if (sum > 0xff) *vol = (uint8_t)sum;
        else *vol = 0;
    } else {
        unsigned sum = (unsigned)*vol + value;
        *vol = sum > 0xff ? 0xff : (uint8_t)sum;
    }
    return PMD_CMD_RETURN;
}

// :2097
static enum pmd_cmd_result vd_ssg(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    return vd_main(pmd, part, src, &pmd->ssg_voldown, &pmd->_ssg_voldown, false);
}

// :2100
static enum pmd_cmd_result vd_pcm(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    return vd_main(pmd, part, src, &pmd->pcm_voldown, &pmd->_pcm_voldown, false);
}

// :2103
static enum pmd_cmd_result vd_rhythm(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    return vd_main(pmd, part, src, &pmd->rhythm_voldown, &pmd->_rhythm_voldown,
                   false);
}

// :2107
static enum pmd_cmd_result vd_ppz(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    return vd_main(pmd, part, src, &pmd->ppz_voldown, &pmd->_ppz_voldown, false);
}

// :2111
static enum pmd_cmd_result _vd_fm(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    return vd_main(pmd, part, src, &pmd->fm_voldown, &pmd->_fm_voldown, true);
}

// :2136
static enum pmd_cmd_result _vd_ssg(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    return vd_main(pmd, part, src, &pmd->ssg_voldown, &pmd->_ssg_voldown, true);
}

// :2141
static enum pmd_cmd_result _vd_pcm(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    return vd_main(pmd, part, src, &pmd->pcm_voldown, &pmd->_pcm_voldown, true);
}

// :2146
static enum pmd_cmd_result _vd_rhythm(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    return vd_main(pmd, part, src, &pmd->rhythm_voldown, &pmd->_rhythm_voldown,
                   true);
}

// :2151
static enum pmd_cmd_result _vd_ppz(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    return vd_main(pmd, part, src, &pmd->ppz_voldown, &pmd->_ppz_voldown, true);
}

// :2159
static enum pmd_cmd_result slot_delay(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    uint8_t mask = lodsb(src);
    part->sdelay_m = (uint8_t)((~(mask & 0x0f) & 0x0f) << 4);
    uint8_t delay = lodsb(src);
    part->sdelay = delay;
    part->sdelay_c = delay;
    return PMD_CMD_RETURN;
}

// :2176
static enum pmd_cmd_result fb_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint8_t fb = lodsb(src);
    if ((fb & 0x80) != 0) {
        _fb_set(pmd, part, fb);
        return PMD_CMD_RETURN;
    }
    fb = (uint8_t)((fb << 3) | (fb >> 5)); // rol fb,3
    fb_set3(pmd, part, fb);
    return PMD_CMD_RETURN;
}

// :2186
static void fb_set3(struct pmd *pmd, struct pmd_qq *part, uint8_t fb) {
    uint8_t reg = (uint8_t)(0xb0 - 1 + pmd->partb);
    uint8_t value;
    if (fm3_active(pmd, part)) {
        if ((part->slotmask & 0x10) == 0) return;
        value = (uint8_t)((pmd->fm3_alg_fb & 7) | fb);
        pmd->fm3_alg_fb = value;
    } else {
        value = (uint8_t)((part->alg_fb & 7) | fb);
    }
    opnset(pmd, (uint16_t)((reg << 8) | value));
    part->alg_fb = value;
}

// :2213
static void _fb_set(struct pmd *pmd, struct pmd_qq *part, uint8_t fb) {
    if ((fb & 0x40) == 0) fb &= 7;

    uint8_t value;
    if (fm3_active(pmd, part)) value = pmd->fm3_alg_fb;
    else value = part->alg_fb;
    value = (uint8_t)(((value >> 3) | (value << 5)) & 7); // ror dl,3
    fb = (uint8_t)(fb + value);
    if ((fb & 0x80) != 0) {
        fb = 0;
    } else if (fb >= 8) {
        fb = 0x38;
    } else {
        fb = (uint8_t)((fb << 3) | (fb >> 5)); // fb_set2's rol fb,3
    }
    fb_set3(pmd, part, fb);
}

// :2248
static enum pmd_cmd_result tl_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint8_t reg = (uint8_t)(0x40 - 1 + pmd->partb);
    uint8_t mask = lodsb(src);
    uint8_t slots = mask & 0x0f;
    uint8_t swapped = (uint8_t)(((part->slotmask >> 4) | (part->slotmask << 4)));
    slots &= swapped;
    uint8_t delta = lodsb(src);
    bool write = part->partmask == 0;

    if ((mask & 0x80) == 0) delta &= 0x7f;

    uint8_t *slot[4] = {&part->slot1, &part->slot2, &part->slot3, &part->slot4};
    uint8_t regs[4] = {reg, (uint8_t)(reg + 8), (uint8_t)(reg + 4),
                       (uint8_t)(reg + 12)};
    for (int i = 0; i < 4; i++) {
        if ((slots & (1u << i)) == 0) continue;
        uint8_t value;
        if ((mask & 0x80) == 0) {
            value = delta;
        } else {
            value = (uint8_t)(*slot[i] + delta);
            if ((value & 0x80) != 0) {
                if ((delta & 0x80) != 0) value = 0;
                else value = 127;
            }
        }
        *slot[i] = value;
        if (write) opnset(pmd, (uint16_t)((regs[i] << 8) | value));
    }
    return PMD_CMD_RETURN;
}

// :2357
static enum pmd_cmd_result fm_mml_part_mask(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint8_t mask = lodsb(src);
    if (mask >= 2) return special_0c0h(pmd, part, src, mask);
    if (mask == 0) {
        part->partmask &= 0xbf;
        if (part->partmask != 0) return PMD_CMD_FMMNP_1;
        neiro_reset(pmd, part);
        return PMD_CMD_MP1;
    }
    part->partmask |= 0x40;
    if (part->partmask == 0x40) silence_fmpart(pmd, part);
    return PMD_CMD_FMMNP_1;
}

// :2378
static enum pmd_cmd_result ssg_mml_part_mask(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint8_t mask = lodsb(src);
    if (mask >= 2) return special_0c0h(pmd, part, src, mask);
    if (mask == 0) {
        part->partmask &= 0xbf;
        if (part->partmask != 0) return PMD_CMD_PSGMNP_1;
        return PMD_CMD_MP1P;
    }
    part->partmask |= 0x40;
    if (part->partmask == 0x40) {
        uint8_t read, mask;
        psgmsk(pmd, &read, &mask);
        uint8_t value = (uint8_t)(read | mask);
        opnset44(pmd, (uint16_t)((7 << 8) | value));
    }
    return PMD_CMD_PSGMNP_1;
}

// :2402
static enum pmd_cmd_result rhythm_mml_part_mask(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint8_t mask = lodsb(src);
    if (mask >= 2) return special_0c0h(pmd, part, src, mask);
    if (mask == 0) part->partmask &= 0xbf;
    else part->partmask |= 0x40;
    return PMD_CMD_RETURN;
}

// :2417
static void neiro_reset(struct pmd *pmd, struct pmd_qq *part) {
    if (part->neiromask == 0) return;

    uint8_t voice = part->voicenum;
    uint16_t slot1 = (uint16_t)(part->slot1 | (part->slot3 << 8));
    uint16_t slot2 = (uint16_t)(part->slot2 | (part->slot4 << 8));
    pmd->af_check = 1;
    neiroset(pmd, part, voice);
    pmd->af_check = 0;
    part->slot1 = (uint8_t)slot1;
    part->slot3 = (uint8_t)(slot1 >> 8);
    part->slot2 = (uint8_t)slot2;
    part->slot4 = (uint8_t)(slot2 >> 8);

    uint8_t mask = (uint8_t)(~part->carrier & part->slotmask);
    if (mask != 0) {
        uint8_t reg = (uint8_t)(0x4c - 1 + pmd->partb);
        mask = (uint8_t)((mask << 1) | (mask >> 7)); // rol mask,1
        if (mask & 1) opnset(pmd, (uint16_t)((reg << 8) | part->slot4));
        reg -= 8;
        mask = (uint8_t)((mask << 1) | (mask >> 7));
        if (mask & 1) opnset(pmd, (uint16_t)((reg << 8) | part->slot3));
        reg += 4;
        mask = (uint8_t)((mask << 1) | (mask >> 7));
        if (mask & 1) opnset(pmd, (uint16_t)((reg << 8) | part->slot2));
        reg -= 8;
        mask = (uint8_t)((mask << 1) | (mask >> 7));
        if (mask & 1) opnset(pmd, (uint16_t)((reg << 8) | part->slot1));
    }

    // :2465 nr_exit
#if board2
    {
        uint8_t reg = (uint8_t)(pmd->partb + 0xb3);
        uint8_t pan;
        calc_panout(pmd, part, &pan);
        opnset(pmd, (uint16_t)((reg << 8) | pan));
    }
#endif
}

// :2478
static enum pmd_cmd_result pdrswitch(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)part;
    uint8_t value = lodsb(src);
    if (pmd->ppsdrv_flag != 0 && pmd->pps != NULL &&
        pmd->pps->pdr_switch != NULL) {
        pmd->pps->pdr_switch(value, pmd->user_data);
    }
    return PMD_CMD_RETURN;
}

// :2493
static enum pmd_cmd_result volmask_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint8_t mask = lodsb(src) & 0x0f;
    if (mask == 0) {
        mask = part->carrier;
    } else {
        mask = (uint8_t)((mask << 4) | (mask >> 4));
        mask |= 0x0f;
    }
    part->volmask = mask;
    ch3_setting(pmd, part);
    return PMD_CMD_RETURN;
}

// :2510
static enum pmd_cmd_result _volmask_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint8_t mask = lodsb(src) & 0x0f;
    if (mask == 0) {
        mask = part->carrier;
    } else {
        mask = (uint8_t)((mask << 4) | (mask >> 4));
        mask |= 0x0f;
    }
    part->_volmask = mask;
    ch3_setting(pmd, part);
    return PMD_CMD_RETURN;
}

// :2528
static bool ch3_setting(struct pmd *pmd, struct pmd_qq *part) {
    if (!fm3_active(pmd, part)) return false;
    ch3mode_set(pmd, part);
    return true;
}

// :2549
static enum pmd_cmd_result fm3_extpartset(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)part;
    static const int slots[3] = {part3b, part3c, part3d};
    for (int i = 0; i < 3; i++) {
        uint16_t offset = lodsw(src);
        if (offset == 0) continue;
        fm3_partinit(pmd, &pmd->parts[slots[i]], pmd->mmlbuf + offset);
    }
    return PMD_CMD_RETURN;
}

// :2576
static void fm3_partinit(struct pmd *pmd, struct pmd_qq *part, uint8_t *address) {
    part->address = address;
    part->leng = 1;
    part->keyoff_flag = 0xff;
    part->mdc = 0xff;
    part->mdc2 = 0xff;
    part->_mdc = 0xff;
    part->_mdc2 = 0xff;
    part->onkai = 0xff;
    part->onkai_def = 0xff;
    part->volume = 108;
    part->fmpan = pmd->parts[part3].fmpan;
    part->partmask |= 0x20;
}

// :2597
static enum pmd_cmd_result detune_extend(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    uint8_t flag = lodsb(src) & 1;
    part->extendmode = (uint8_t)((part->extendmode & 0xfe) | flag);
    return PMD_CMD_RETURN;
}

// :2607
static enum pmd_cmd_result lfo_extend(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    uint8_t flag = (uint8_t)((lodsb(src) & 1) << 1);
    part->extendmode = (uint8_t)((part->extendmode & 0xfd) | flag);
    return PMD_CMD_RETURN;
}

// :2618
static enum pmd_cmd_result envelope_extend(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    uint8_t flag = (uint8_t)((lodsb(src) & 1) << 2);
    part->extendmode = (uint8_t)((part->extendmode & 0xfb) | flag);
    return PMD_CMD_RETURN;
}

// :2630
static enum pmd_cmd_result lfowave_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    part->lfo_wave = lodsb(src);
    return PMD_CMD_RETURN;
}

// :2638
static enum pmd_cmd_result extend_psgenvset(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    part->eenv_ar = lodsb(src) & 0x1f;
    part->eenv_dr = lodsb(src) & 0x1f;
    part->eenv_sr = lodsb(src) & 0x1f;
    uint8_t value = lodsb(src);
    part->eenv_rr = value & 0x0f;
    value = (uint8_t)((value << 4) | (value >> 4)); // rol al,4
    value &= 0x0f;
    part->eenv_sl = (uint8_t)(~value & 0x0f);
    part->eenv_al = lodsb(src) & 0x0f;

    if (part->envf != 0xff) {
        part->envf = 0xff;
        part->eenv_count = 4;
        part->eenv_volume = 0;
    }
    return PMD_CMD_RETURN;
}

// :2674
static enum pmd_cmd_result slotdetune_set2(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    if (!fm3_active(pmd, part)) {
        *src += 3;
        return PMD_CMD_RETURN;
    }
    uint8_t mask = lodsb(src);
    uint16_t detune = lodsw(src);
    if (mask & 1) pmd->slot_detune1 = (uint16_t)(pmd->slot_detune1 + detune);
    if (mask & 2) pmd->slot_detune2 = (uint16_t)(pmd->slot_detune2 + detune);
    if (mask & 4) pmd->slot_detune3 = (uint16_t)(pmd->slot_detune3 + detune);
    if (mask & 8) pmd->slot_detune4 = (uint16_t)(pmd->slot_detune4 + detune);
    pmd->slotdetune_flag =
        (pmd->slot_detune1 | pmd->slot_detune2 | pmd->slot_detune3 |
         pmd->slot_detune4) != 0;
    return PMD_CMD_RETURN;
}

// :2704
static enum pmd_cmd_result slotdetune_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    if (!fm3_active(pmd, part)) {
        *src += 3;
        return PMD_CMD_RETURN;
    }
    uint8_t mask = lodsb(src);
    uint16_t detune = lodsw(src);
    if (mask & 1) pmd->slot_detune1 = detune;
    if (mask & 2) pmd->slot_detune2 = detune;
    if (mask & 4) pmd->slot_detune3 = detune;
    if (mask & 8) pmd->slot_detune4 = detune;
    pmd->slotdetune_flag =
        (pmd->slot_detune1 | pmd->slot_detune2 | pmd->slot_detune3 |
         pmd->slot_detune4) != 0;
    return PMD_CMD_RETURN;
}

// :2745
static void ch3mode_set(struct pmd *pmd, struct pmd_qq *part) {
    uint8_t slot;
    if (part == &pmd->parts[part3]) slot = 1;
    else if (part == &pmd->parts[part3b]) slot = 2;
    else if (part == &pmd->parts[part3c]) slot = 4;
    else slot = 8;

    // :2756 cmset_00: a part with no masked slot, or one whose mask the LFOs
    // drive, leaves CSM/window mode clear.
    bool clear;
    if ((part->slotmask & 0xf0) == 0) clear = true;
    else if ((part->slotmask & 0xf0) != 0xf0) clear = false;
    else if ((part->volmask & 0x0f) == 0) clear = true;
    else if ((part->lfoswi & 1) != 0) clear = false;
    else if ((part->_volmask & 0x0f) == 0) clear = true;
    else clear = (part->lfoswi & 0x10) == 0;

    uint8_t mode;
    if (clear) {
        // :2770 cm_clear
        slot = (uint8_t)~slot;
        pmd->slot3_flag &= slot;
        if (pmd->slot3_flag != 0 || pmd->slotdetune_flag == 1) mode = 0x7f; // :2781 cm_set2
        else mode = 0x3f;
    } else {
        // :2779 cm_set
        pmd->slot3_flag |= slot;
        mode = 0x7f;
    }

    // :2783 cm_set_main
#if !board2
    if ((part->partmask & 2) != 0) {
        pmd->ch3mode_push = mode;
        return;
    }
#endif
    if (mode == pmd->ch3mode) return;
    pmd->ch3mode = mode;
    opnset44(pmd, (uint16_t)((0x27 << 8) | (mode & 0xcf)));

    if (mode == 0x3f) return;
    if (part == &pmd->parts[part3]) return;
    otodasi_cm(pmd, &pmd->parts[part3]);
    if (part == &pmd->parts[part3b]) return;
    otodasi_cm(pmd, &pmd->parts[part3b]);
    if (part == &pmd->parts[part3c]) return;
    otodasi_cm(pmd, &pmd->parts[part3c]);
}

// :2823
static void otodasi_cm(struct pmd *pmd, struct pmd_qq *part) {
    if (part->partmask != 0) return;
    otodasi(pmd, part);
}

// :2839
static enum pmd_cmd_result slotmask_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint8_t carrier = lodsb(src);
    uint8_t mask = carrier;
    carrier &= 0x0f;
    if (carrier != 0) {
        part->carrier = (uint8_t)((carrier << 4) | (carrier >> 4));
    } else {
        // :2851 sm_not_car
        uint8_t alg;
        if (fm3_active(pmd, part)) {
            alg = pmd->fm3_alg_fb;
        } else {
            uint8_t *tone;
            toneadr_calc(pmd, part, part->voicenum, &tone);
            alg = tone[24];
        }
        alg &= 7;
        part->carrier = carrier_table[alg];
    }

    // :2877 sm_set
    mask &= 0xf0;
    if (part->slotmask == mask) return PMD_CMD_RETURN;
    part->slotmask = mask;
    if ((mask & 0xf0) != 0) part->partmask &= 0xdf;
    else part->partmask |= 0x20;

    // :2889 sms_ns
    if (ch3_setting(pmd, part)) {
        // :2892 The earlier channel-3 parts are keyed before the jump to
        // sm_exit.
        struct pmd_qq *const ch3[] = {
            &pmd->parts[part3],
            &pmd->parts[part3b],
            &pmd->parts[part3c],
        };
        for (int i = 0; i < 3 && part != ch3[i]; i++) keyon_sm(pmd, ch3[i]);
    }

    // :2914 sm_exit
    mask = 0;
    carrier = part->slotmask;
    if (carrier & 0x80) mask |= 0x11;
    if (carrier & 0x40) mask |= 0x44;
    if (carrier & 0x20) mask |= 0x22;
    if (carrier & 0x10) mask |= 0x88;
    part->neiromask = mask;

    if (part->partmask == 0) return PMD_CMD_MP1;
    return PMD_CMD_FMMNP_1;
}

// :2945
static void keyon_sm(struct pmd *pmd, struct pmd_qq *part) {
    if (part->partmask != 0) return;
    if ((part->keyoff_flag & 1) != 0) return;
    keyon(pmd, part);
}

// :2957
static enum pmd_cmd_result ssg_efct_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint8_t efcnum = lodsb(src);
    if (part->partmask != 0) return PMD_CMD_RETURN;
    if (efcnum != 0) {
        eff_on2(pmd, part, efcnum);
    } else {
        effend(pmd);
    }
    return PMD_CMD_RETURN;
}

// :2981
static enum pmd_cmd_result fm_efct_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint8_t efcnum = lodsb(src);
    if (part->partmask != 0) return PMD_CMD_RETURN;
#if board2
    uint8_t fmsel = pmd->fmsel;
#endif
    uint8_t partb = pmd->partb;
    if (efcnum != 0) {
        fm_effect_on(pmd, efcnum);
    } else {
        fm_effect_off(pmd);
    }
    pmd->partb = partb;
#if board2
    if (fmsel == 0) sel44(pmd);
    else sel46(pmd);
#endif
    return PMD_CMD_RETURN;
}

// :3029
static enum pmd_cmd_result fade_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)part;
    pmd->fadeout_flag = 1;
    pmd->fadeout_speed = lodsb(src); // fout
    return PMD_CMD_RETURN;
}

// :3037
static enum pmd_cmd_result mdepth_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    uint8_t speed = lodsb(src);
    part->mdspd = speed;
    part->mdspd2 = speed;
    part->mdepth = lodsb(src);
    return PMD_CMD_RETURN;
}

// :3045
static enum pmd_cmd_result mdepth_count(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    uint8_t count = lodsb(src);
    if ((count & 0x80) != 0) {
        count &= 0x7f;
        if (count == 0) count--;
        part->_mdc = count;
        part->_mdc2 = count;
    } else {
        if (count == 0) count--;
        part->mdc = count;
        part->mdc2 = count;
    }
    return PMD_CMD_RETURN;
}

// :3067
static void porta_calc(struct pmd *pmd, struct pmd_qq *part) {
    (void)pmd;
    part->porta_num = (uint16_t)(part->porta_num + part->porta_num2);
    if (part->porta_num3 == 0) return;
    if (part->porta_num3 < 0) {
        part->porta_num3++;
        part->porta_num--;
    } else {
        part->porta_num3--;
        part->porta_num++;
    }
}

// :3085
static enum pmd_cmd_result porta(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    if (part->partmask != 0) {
        lodsb(src);
        return PMD_CMD_RETURN;
    }
    uint8_t note = lodsb(src);
    note = lfoinit(pmd, part, note);
    note = oshift(part, note);
    fnumset(pmd, part, note);

    uint16_t from_fnum = part->fnum;
    uint8_t from_note = part->onkai;

    note = lodsb(src);
    note = oshift(part, note);
    fnumset(pmd, part, note);
    uint16_t to_fnum = part->fnum;

    part->onkai = from_note;
    part->fnum = from_fnum;

    // The block bits sit in the high byte of the f-number.
    uint16_t from = from_fnum;
    uint16_t delta = 0;
    uint8_t to_block = (uint8_t)((to_fnum >> 8) & 0x38);
    uint8_t from_block = (uint8_t)((from >> 8) & 0x38);
    uint8_t block_diff = (uint8_t)(to_block - from_block);
    if (block_diff != 0) {
        // Both are 0x38-masked, so the original's three sar steps are an exact
        // division.
        int8_t octaves = (int8_t)((int8_t)block_diff / 8);
        delta = (uint16_t)(int16_t)octaves;
        delta = (uint16_t)((int16_t)delta * 0x26a);
    }
    from &= 0x7ff;
    to_fnum &= 0x7ff;
    to_fnum = (uint16_t)(to_fnum - from);
    delta = (uint16_t)(delta + to_fnum);

    part->leng = lodsb(src);
    calc_q(pmd, part, src);

    {
        int32_t n = (int16_t)delta;
        int32_t d = part->leng;
        part->porta_num2 = (int16_t)(n / d);
        part->porta_num3 = (int16_t)(n % d);
    }
    part->lfoswi |= 8;
    return PMD_CMD_PORTA_RETURN;
}

// :3155
static enum pmd_cmd_result portap(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    if (part->partmask != 0) {
        lodsb(src);
        return PMD_CMD_RETURN;
    }
    uint8_t note = lodsb(src);
    note = lfoinitp(pmd, part, note);
    note = oshift(part, note);
    fnumsetp(pmd, part, note);

    uint16_t from_fnum = part->fnum;
    uint8_t from_note = part->onkai;

    note = lodsb(src);
    note = oshift(part, note);
    fnumsetp(pmd, part, note);
    uint16_t to_fnum = part->fnum;

    part->onkai = from_note;
    part->fnum = from_fnum;
    to_fnum = (uint16_t)(to_fnum - from_fnum);

    part->leng = lodsb(src);
    calc_q(pmd, part, src);

    {
        int32_t n = (int16_t)to_fnum;
        int32_t d = part->leng;
        part->porta_num2 = (int16_t)(n / d);
        part->porta_num3 = (int16_t)(n % d);
    }
    part->lfoswi |= 8;
    return PMD_CMD_PORTA_RETURNP;
}

// :3200
static enum pmd_cmd_result status_write(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)part;
    pmd->status = lodsb(src);
    return PMD_CMD_RETURN;
}

// :3208
static enum pmd_cmd_result status_add(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)part;
    pmd->status = (uint8_t)(pmd->status + lodsb(src));
    return PMD_CMD_RETURN;
}

// :3218
static enum pmd_cmd_result vol_one_up_fm(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    uint8_t volume = (uint8_t)(lodsb(src) + part->volume);
    if (volume >= 128) volume = 127;
    part->volpush = (uint8_t)(volume + 1);
    pmd->volpush_flag = 1;
    return PMD_CMD_RETURN;
}

// :3229
static enum pmd_cmd_result vol_one_up_psg(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    uint8_t volume = (uint8_t)(lodsb(src) + part->volume);
    if (volume >= 16) volume = 15;
    part->volpush = (uint8_t)(volume + 1);
    pmd->volpush_flag = 1;
    return PMD_CMD_RETURN;
}

#if (board2 && (pcm || adpcm)) || ppz
// :3237
static enum pmd_cmd_result vol_one_up_pcm(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    unsigned sum = (unsigned)lodsb(src) + part->volume;
    uint8_t volume;
    if (sum > 0xff) volume = 254;
    else if (sum < 255) volume = (uint8_t)sum;
    else volume = 254;
    part->volpush = (uint8_t)(volume + 1);
    pmd->volpush_flag = 1;
    return PMD_CMD_RETURN;
}
#endif

// :3248
static enum pmd_cmd_result vol_one_down(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    uint8_t amount = lodsb(src);
    uint8_t volume = part->volume;
    if (volume < amount) volume = 0;
    else if (volume - amount >= 255) volume = 254;
    else volume = (uint8_t)(volume - amount);
    part->volpush = (uint8_t)(volume + 1);
    pmd->volpush_flag = 1;
    return PMD_CMD_RETURN;
}

// :3260
static enum pmd_cmd_result hlfo_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint8_t panval = lodsb(src);
#if board2
    part->fmpan = (uint8_t)((part->fmpan & 0xc0) | panval);
    if (fm3_active(pmd, part)) {
        pmd->parts[part3].fmpan = part->fmpan;
        pmd->parts[part3b].fmpan = part->fmpan;
        pmd->parts[part3c].fmpan = part->fmpan;
        pmd->parts[part3d].fmpan = part->fmpan;
    }
    if (part->partmask == 0) {
        uint8_t pan;
        calc_panout(pmd, part, &pan);
        opnset(pmd, (uint16_t)(((pmd->partb + 0xb3) << 8) | pan));
    }
#else
    (void)pmd;
    (void)part;
    (void)panval;
#endif
    return PMD_CMD_RETURN;
}

// :3304
static enum pmd_cmd_result hlfo_onoff(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint8_t value = lodsb(src);
#if board2
    pmd->port22h = value;
    opnset44(pmd, (uint16_t)(0x2200 | value));
#else
    (void)pmd;
#endif
    (void)pmd;
    (void)value;
    (void)part;
    return PMD_CMD_RETURN;
}

// :3318
static enum pmd_cmd_result hlfo_delay(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint8_t delay = lodsb(src);
#if board2
    part->hldelay = delay;
#else
    (void)pmd;
#endif
    (void)pmd;
    (void)delay;
    (void)part;
    return PMD_CMD_RETURN;
}

// :3328
static enum pmd_cmd_result syousetu_lng_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)part;
    pmd->syousetu_lng = lodsb(src);
    return PMD_CMD_RETURN;
}

// :3334
static enum pmd_cmd_result com_at(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint8_t voice = lodsb(src);
    part->voicenum = voice;
    if (part->partmask == 0) {
        neiroset(pmd, part, voice);
        return PMD_CMD_RETURN;
    }

    uint8_t *tone;
    toneadr_calc(pmd, part, voice, &tone);
    uint8_t alg = tone[24];
    part->alg_fb = alg;
    tone += 4;
    neiroset_tl(part, tone);

    if (!fm3_active(pmd, part) || part->neiromask == 0) return PMD_CMD_RETURN;

    if ((part->slotmask & 0x10) != 0) {
        // BUG: the original leaves AL holding the voice number here and stores
        // it as alg_fb. Preserved because it is observable in part state.
        part->alg_fb = part->voicenum;
    } else {
        uint8_t tmp = (uint8_t)(pmd->fm3_alg_fb & 0x38);
        alg = (uint8_t)((alg & 7) | tmp);
        part->alg_fb = tmp;
    }
    pmd->fm3_alg_fb = alg;
    return PMD_CMD_RETURN;
}

// :3381
static enum pmd_cmd_result comq(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    part->qdata = lodsb(src);
    part->qdat3 = 0;
    return PMD_CMD_RETURN;
}

// :3386
static enum pmd_cmd_result comq3(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    part->qdat2 = lodsb(src);
    return PMD_CMD_RETURN;
}

// :3390
static enum pmd_cmd_result comq4(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    part->qdat3 = lodsb(src);
    return PMD_CMD_RETURN;
}

// :3398
static enum pmd_cmd_result comq2(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    part->qdatb = lodsb(src);
    return PMD_CMD_RETURN;
}

// :3406
static enum pmd_cmd_result comv(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    part->volume = lodsb(src);
    return PMD_CMD_RETURN;
}

// :3417
static enum pmd_cmd_result comt(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)part;
    uint8_t cmd = lodsb(src);
    if (cmd < 251) {
        pmd->tempo_d = cmd;
        pmd->tempo_d_push = cmd;
        calc_tb_tempo(pmd);
        return PMD_CMD_RETURN;
    }

    uint8_t a = (uint8_t)(cmd + 1);
    if (a == 0) {
        uint8_t value = lodsb(src);
        if (value < 0x18) value = 0x18;
        pmd->tempo_48 = value;
        pmd->tempo_48_push = value;
        calc_tempo_tb(pmd);
        return PMD_CMD_RETURN;
    }

    {
        uint8_t b = (uint8_t)(a + 1);
        uint8_t param = lodsb(src);
        uint8_t value = param;
        if (b == 0) {
            // T±: relative tempo_d.
            uint8_t push = pmd->tempo_d_push;
            unsigned sum = (unsigned)push + value;
            if ((param & 0x80) != 0) value = sum > 0xff ? (uint8_t)sum : 0;
            else value = sum > 0xff ? 250 : (uint8_t)sum;
            if (value >= 251) value = 250;
            pmd->tempo_d = value;
            pmd->tempo_d_push = value;
            calc_tb_tempo(pmd);
            return PMD_CMD_RETURN;
        }

        // t±: relative tempo_48.
        uint8_t push = pmd->tempo_48_push;
        unsigned sum = (unsigned)push + value;
        if ((param & 0x80) != 0) {
            value = sum > 0xff ? (uint8_t)sum : 0x18;
            if (value < 0x18) value = 0x18;
        } else {
            value = sum > 0xff ? 255 : (uint8_t)sum;
        }
        pmd->tempo_48 = value;
        pmd->tempo_48_push = value;
        calc_tempo_tb(pmd);
        return PMD_CMD_RETURN;
    }
}

// :3480
static void calc_tb_tempo(struct pmd *pmd) {
    uint8_t divisor = (uint8_t)(0 - pmd->tempo_d);
    uint8_t tempo = 255;
    if (divisor >= 18) {
        uint8_t quotient = (uint8_t)(0x112c / divisor);
        uint8_t remainder = (uint8_t)(0x112c % divisor);
        tempo = quotient;
        if ((remainder & 0x80) != 0) tempo++;
    }
    pmd->tempo_48 = tempo;
    pmd->tempo_48_push = tempo;
}

// :3502
static void calc_tempo_tb(struct pmd *pmd) {
    uint8_t divisor = pmd->tempo_48;
    uint8_t tempo = 0;
    if (divisor >= 18) {
        uint8_t quotient = (uint8_t)(0x112c / divisor);
        uint8_t remainder = (uint8_t)(0x112c % divisor);
        tempo = (uint8_t)(0 - quotient);
        if ((remainder & 0x80) != 0) tempo--;
    }
    pmd->tempo_d = tempo;
    pmd->tempo_d_push = tempo;
}

// :3524
static enum pmd_cmd_result comtie(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)part;
    (void)src;
    pmd->tieflag |= 1;
    return PMD_CMD_RETURN;
}

// :3531
static enum pmd_cmd_result comd(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    part->detune = (int16_t)lodsw(src);
    return PMD_CMD_RETURN;
}

// :3538
static enum pmd_cmd_result comdd(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    part->detune = (int16_t)(part->detune + (int16_t)lodsw(src));
    return PMD_CMD_RETURN;
}

// :3545
static enum pmd_cmd_result comstloop(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint16_t offset = lodsw(src);
    uint8_t *base = part == &pmd->part_e ? pmd->efcdat : pmd->mmlbuf;
    base[offset + 1] = 0;
    return PMD_CMD_RETURN;
}

// :3561
static enum pmd_cmd_result comedloop(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint8_t count = lodsb(src);
    if (count == 0) {
        // :3565 muloop, an unconditional loop
        (*src)++;
        part->loopcheck = 1;
    } else {
        uint8_t previous = count;
        (*src)[0]++;
        count = lodsb(src);
        if (previous == count) {
            *src += 2;
            return PMD_CMD_RETURN;
        }
    }

    // :3575 reloop
    uint16_t offset = lodsw(src);
    offset = (uint16_t)(offset + 2);
    uint8_t *base = part == &pmd->part_e ? pmd->efcdat : pmd->mmlbuf;
    *src = base + offset;
    return PMD_CMD_RETURN;
}

// :3590
static enum pmd_cmd_result comexloop(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint16_t offset = lodsw(src);
    uint8_t *base = part == &pmd->part_e ? pmd->efcdat : pmd->mmlbuf;
    uint8_t *data = base + offset;
    uint8_t count = (uint8_t)(data[0] - 1);
    data++;
    if (count == data[0]) *src = data + 3;
    return PMD_CMD_RETURN;
}

// :3613
static enum pmd_cmd_result comlopset(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    part->partloop = *src;
    return PMD_CMD_RETURN;
}

// :3620
static enum pmd_cmd_result comshift(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    part->shift = (int8_t)lodsb(src);
    return PMD_CMD_RETURN;
}

// :3628
static enum pmd_cmd_result comshift2(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    part->shift = (int8_t)(part->shift + (int8_t)lodsb(src));
    return PMD_CMD_RETURN;
}

// :3637
static enum pmd_cmd_result comshift_master(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    part->shift_def = (int8_t)lodsb(src);
    return PMD_CMD_RETURN;
}

// :3646
static enum pmd_cmd_result comvolup(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    (void)src;
    uint8_t volume = (uint8_t)(part->volume + 4);
    part->volume = volume >= 128 ? 127 : volume;
    return PMD_CMD_RETURN;
}

// :3657
static enum pmd_cmd_result comvolup2(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    uint8_t volume = (uint8_t)(lodsb(src) + part->volume);
    part->volume = volume >= 128 ? 127 : volume;
    return PMD_CMD_RETURN;
}

// :3663
static enum pmd_cmd_result comvolupp(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    (void)src;
    uint8_t volume = (uint8_t)(part->volume + 1);
    part->volume = volume >= 16 ? 15 : volume;
    return PMD_CMD_RETURN;
}

// :3673
static enum pmd_cmd_result comvolupp2(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    uint8_t volume = (uint8_t)(lodsb(src) + part->volume);
    part->volume = volume >= 16 ? 15 : volume;
    return PMD_CMD_RETURN;
}

// :3682
static enum pmd_cmd_result comvoldown(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    (void)src;
    uint8_t volume = part->volume;
    part->volume = volume >= 4 ? (uint8_t)(volume - 4) : 0;
    return PMD_CMD_RETURN;
}

// :3690
static enum pmd_cmd_result comvoldown2(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    uint8_t amount = lodsb(src);
    part->volume = part->volume >= amount ? (uint8_t)(part->volume - amount) : 0;
    return PMD_CMD_RETURN;
}

// :3700
static enum pmd_cmd_result comvoldownp(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    (void)src;
    if (part->volume != 0) part->volume--;
    return PMD_CMD_RETURN;
}

// :3708
static enum pmd_cmd_result comvoldownp2(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    uint8_t amount = lodsb(src);
    part->volume = part->volume >= amount ? (uint8_t)(part->volume - amount) : 0;
    return PMD_CMD_RETURN;
}

// :3720
static enum pmd_cmd_result _lfoset(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    lfo_change(part);
    enum pmd_cmd_result r = lfoset(pmd, part, src);
    lfo_change(part);
    return r;
}

// :3733
static enum pmd_cmd_result _mdepth_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    lfo_change(part);
    enum pmd_cmd_result r = mdepth_set(pmd, part, src);
    lfo_change(part);
    return r;
}

// :3737
static enum pmd_cmd_result _lfowave_set(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    lfo_change(part);
    enum pmd_cmd_result r = lfowave_set(pmd, part, src);
    lfo_change(part);
    return r;
}

// :3741
static enum pmd_cmd_result _lfo_extend(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    lfo_change(part);
    enum pmd_cmd_result r = lfo_extend(pmd, part, src);
    lfo_change(part);
    return r;
}

// :3745
static enum pmd_cmd_result _lfoset_delay(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    lfo_change(part);
    enum pmd_cmd_result r = lfoset_delay(pmd, part, src);
    lfo_change(part);
    return r;
}

// :3749
static enum pmd_cmd_result _lfoswitch(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    uint8_t value = (uint8_t)(lodsb(src) & 7);
    value = (uint8_t)((value << 4) | (value >> 4));
    part->lfoswi = (uint8_t)((part->lfoswi & 0x8f) | value);
    lfo_change(part);
    lfoinit_main(part);
    lfo_change(part);
    return PMD_CMD_RETURN;
}

// :3762
static enum pmd_cmd_result _lfoswitch_f(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    enum pmd_cmd_result r = _lfoswitch(pmd, part, src);
    ch3_setting(pmd, part);
    return r;
}

// :3769
static void lfo_change(struct pmd_qq *part) {
    int16_t tmp16 = part->lfodat;
    part->lfodat = part->_lfodat;
    part->_lfodat = tmp16;

    part->lfoswi = (uint8_t)((part->lfoswi >> 4) | (part->lfoswi << 4));
    part->extendmode = (uint8_t)((part->extendmode >> 4) |
                               (part->extendmode << 4));

    uint8_t t;
#define SWAP(a, b)                                                            \
    do {                                                                      \
        t = (a);                                                              \
        (a) = (b);                                                            \
        (b) = t;                                                              \
    } while (0)
    SWAP(part->delay, part->_delay);
    SWAP(part->speed, part->_speed);
    SWAP(part->step, part->_step);
    SWAP(part->time, part->_time);
    SWAP(part->delay2, part->_delay2);
    SWAP(part->speed2, part->_speed2);
    SWAP(part->step2, part->_step2);
    SWAP(part->time2, part->_time2);
    SWAP(part->mdepth, part->_mdepth);
    SWAP(part->mdspd, part->_mdspd);
    SWAP(part->mdspd2, part->_mdspd2);
    SWAP(part->lfo_wave, part->_lfo_wave);
    SWAP(part->mdc, part->_mdc);
    SWAP(part->mdc2, part->_mdc2);
#undef SWAP
}

// :3813
static enum pmd_cmd_result lfoset(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    uint8_t value = lodsb(src);
    part->delay = value;
    part->delay2 = value;
    value = lodsb(src);
    part->speed = value;
    part->speed2 = value;
    value = lodsb(src);
    part->step = value;
    part->step2 = value;
    value = lodsb(src);
    part->time = value;
    part->time2 = value;
    lfoinit_main(part);
    return PMD_CMD_RETURN;
}

// :3827
static enum pmd_cmd_result lfoset_delay(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    uint8_t delay = lodsb(src);
    part->delay = delay;
    part->delay2 = delay;
    lfoinit_main(part);
    return PMD_CMD_RETURN;
}

// :3836
static enum pmd_cmd_result lfoswitch(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    uint8_t value = lodsb(src);
    if ((value & 0xf8) != 0) value = 1;
    value &= 7;
    part->lfoswi = (uint8_t)((part->lfoswi & 0xf8) | value);
    lfoinit_main(part);
    return PMD_CMD_RETURN;
}

// :3847
static enum pmd_cmd_result lfoswitch_f(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    enum pmd_cmd_result r = lfoswitch(pmd, part, src);
    ch3_setting(pmd, part);
    return r;
}

// :3854
static enum pmd_cmd_result psgenvset(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    uint8_t value = lodsb(src);
    part->pat = value;
    part->patb = value;
    part->pv2 = lodsb(src);
    value = lodsb(src);
    part->pr1 = value;
    part->pr1b = value;
    value = lodsb(src);
    part->pr2 = value;
    part->pr2b = value;
    if (part->envf == 0xff) {
        part->envf = 2;
        part->penv = (uint8_t)-15;
    }
    return PMD_CMD_RETURN;
}

// :3876
static enum pmd_cmd_result comy(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)part;
    uint16_t value = lodsw(src);
    uint8_t low = (uint8_t)value;
    uint8_t high = (uint8_t)(value >> 8);
    opnset(pmd, (uint16_t)((low << 8) | high));
    return PMD_CMD_RETURN;
}

// :3885
static enum pmd_cmd_result psgnoise(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)part;
    pmd->psnoi = lodsb(src);
    return PMD_CMD_RETURN;
}

// :3889
static enum pmd_cmd_result psgnoise_move(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)part;
    uint8_t noise = (uint8_t)(lodsb(src) + pmd->psnoi);
    if ((noise & 0x80) != 0) noise = 0;
    if (noise >= 32) noise = 31;
    pmd->psnoi = noise;
    return PMD_CMD_RETURN;
}

// :3906
static enum pmd_cmd_result psgsel(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)pmd;
    part->psgpat = lodsb(src);
    return PMD_CMD_RETURN;
}

// :3914
static enum pmd_cmd_result panset(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint8_t pan = lodsb(src);
    panset_main(pmd, part, pan);
    return PMD_CMD_RETURN;
}

// :3917
static void panset_main(struct pmd *pmd, struct pmd_qq *part, uint8_t pan) {
#if board2
    pan = (uint8_t)(((pan >> 2) | (pan << 6)) & 0xc0); // ror al,2
    uint8_t mask = pan;
    pan = (uint8_t)((part->fmpan & 0x3f) | mask);
    part->fmpan = pan;

    if (fm3_active(pmd, part)) {
        pmd->parts[part3].fmpan = pan;
        pmd->parts[part3b].fmpan = pan;
        pmd->parts[part3c].fmpan = pan;
        pmd->parts[part3d].fmpan = pan;
    }
    if (part->partmask != 0) return;

    uint8_t value = pan;
    uint8_t reg = (uint8_t)(pmd->partb + 0xb3);
    calc_panout(pmd, part, &value);
    opnset(pmd, (uint16_t)((reg << 8) | value));
#else
    (void)pmd;
    (void)part;
    (void)pan;
#endif
}

#if board2
// :3961
static void calc_panout(struct pmd *pmd, struct pmd_qq *part, uint8_t *pan) {
    (void)pmd;
    *pan = part->fmpan;
    if (part->hldelay_c != 0) *pan &= 0xc0;
}
#endif

// :3972
static enum pmd_cmd_result panset_ex(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    uint8_t pan = lodsb(src);
    (*src)++; // reverse-phase flag
#if board2
    if (pan == 0) pan = 3;
    else if ((pan & 0x80) != 0) pan = 1;
    else pan = 2;
    panset_main(pmd, part, pan);
#else
    (void)pmd;
    (void)part;
    (void)pan;
#endif
    return PMD_CMD_RETURN;
}

// :3994
static enum pmd_cmd_result rhykey(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)part;
#if board2
    uint8_t mask = lodsb(src);
    mask &= pmd->rhythmmask;
    if (mask == 0) return PMD_CMD_RETURN;
    uint8_t keys = mask;

    if (pmd->fadeout_volume != 0) volset2rf(pmd, pmd->rhyvol);

    if ((keys & 0x80) == 0) {
        uint8_t bits = keys;
        uint8_t reg = 0x18;
        for (int i = 0; i < 6; i++) {
            if (bits & 1) opnset44(pmd, (uint16_t)((reg << 8) | pmd->rdat[i]));
            bits >>= 1;
            reg++;
        }
    }

    opnset44(pmd, (uint16_t)(0x1000 | keys));
    if ((keys & 0x80) == 0) {
        rflag_inc(pmd, keys, &pmd->rshot_bd);
        pmd->rshot_dat |= keys;
    } else {
        rflag_inc(pmd, (uint8_t)~keys, &pmd->rdump_bd);
        pmd->rshot_dat &= (uint8_t)~keys;
    }
    if (**src == 0xeb) _rwait(pmd);
#else
    (*src)++;
    (void)pmd;
#endif
    return PMD_CMD_RETURN;
}

#if board2
// :4045
static void rflag_inc(struct pmd *pmd, uint8_t mask, uint8_t *flags) {
    (void)pmd;
    uint8_t bits = mask;
    for (int i = 0; i < 6; i++) {
        if (bits & 1) flags[i]++;
        bits >>= 1;
    }
}
#endif

// :4064
static enum pmd_cmd_result rhyvs(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)part;
#if board2
    uint8_t cmd = lodsb(src);
    uint8_t volume = (uint8_t)(0x1f & cmd);
    rhyvs_write(pmd, cmd, volume);
#else
    (*src)++;
    (void)pmd;
#endif
    return PMD_CMD_RETURN;
}

#if board2
// :4077
// The original addresses `rdat-1 + n` with the 1-based rhythm instrument
// number, so out-of-range values reach the fields next to rdat.
static uint8_t *rdat_adr(struct pmd *pmd, uint8_t index) {
    static_assert(
        offsetof(struct pmd, rdat) == offsetof(struct pmd, rshot_dat) + 1,
        "rdat must follow rshot_dat");
    return (uint8_t *)&pmd->rshot_dat + index;
}

// :4064
static void rhyvs_write(struct pmd *pmd, uint8_t cmd, uint8_t volume) {
    uint8_t index = (uint8_t)((cmd >> 5) & 7);
    uint8_t reg = (uint8_t)(0x17 + index);
    uint8_t *rdat = rdat_adr(pmd, index);
    volume = (uint8_t)(volume | (*rdat & 0xc0));
    *rdat = volume;
    opnset44(pmd, (uint16_t)((reg << 8) | volume));
}
#endif

// :4097
static enum pmd_cmd_result rhyvs_sft(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)part;
#if board2
    uint8_t index = lodsb(src);
    uint8_t reg = (uint8_t)(0x17 + index);
    uint8_t *rdat = rdat_adr(pmd, index);
    uint8_t volume = (uint8_t)(*rdat & 0x1f);
    uint8_t value = (uint8_t)(lodsb(src) + volume);
    if (value >= 32) value = (value & 0x80) != 0 ? 0 : 31;
    volume = (uint8_t)(value & 0x1f);
    volume |= (uint8_t)(*rdat & 0xe0);
    *rdat = volume;
    opnset44(pmd, (uint16_t)((reg << 8) | volume));
#else
    *src += 2;
    (void)pmd;
#endif
    return PMD_CMD_RETURN;
}

// :4136
static enum pmd_cmd_result rpnset(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)part;
#if board2
    uint8_t cmd = lodsb(src);
    uint8_t pan = (uint8_t)(cmd & 3);
    pan = (uint8_t)(((pan >> 2) | (pan << 6)) & 0xff); // ror al,2
    rhyvs_write(pmd, cmd, pan);
#else
    (*src)++;
    (void)pmd;
#endif
    return PMD_CMD_RETURN;
}

// :4155
static enum pmd_cmd_result rmsvs(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)part;
#if board2
    uint8_t volume = lodsb(src);
    uint8_t voldown = pmd->rhythm_voldown;
    if (voldown != 0) {
        uint16_t product = (uint16_t)(uint8_t)(-voldown) * volume;
        volume = (uint8_t)(product >> 8);
    }
    volset2r(pmd, volume);
#else
    (*src)++;
    (void)pmd;
#endif
    return PMD_CMD_RETURN;
}

#if board2
// :4166
static void volset2r(struct pmd *pmd, uint8_t volume) {
    pmd->rhyvol = volume;
    volset2rf(pmd, volume);
}

// :4169
static void volset2rf(struct pmd *pmd, uint8_t volume) {
    uint8_t fade = pmd->fadeout_volume;
    if (fade != 0) {
        uint16_t product = (uint16_t)(uint8_t)(-fade) * volume;
        volume = (uint8_t)(product >> 8);
    }
    opnset44(pmd, (uint16_t)((0x11 << 8) | volume));
}
#endif

// :4184
static enum pmd_cmd_result rmsvs_sft(struct pmd *pmd, struct pmd_qq *part, uint8_t **src) {
    (void)part;
#if board2
    uint8_t delta = lodsb(src);
    uint8_t volume = (uint8_t)(pmd->rhyvol + delta);
    if (volume >= 64) volume = (volume & 0x80) != 0 ? 0 : 63;
    volset2r(pmd, volume);
#else
    (*src)++;
    (void)pmd;
#endif
    return PMD_CMD_RETURN;
}

// :4208
static uint8_t oshift(struct pmd_qq *part, uint8_t note) {
    if ((note & 0x0f) == 0x0f) return note; // rest
    int8_t shift = (int8_t)(part->shift + part->shift_def);
    if (shift == 0) return note;

    uint8_t pitch_class = note & 0x0f;
    uint8_t octave = (uint8_t)(note >> 4);

    if (shift < 0) {
        unsigned sum = (unsigned)pitch_class + (uint8_t)shift;
        bool carry = sum > 0xff;
        pitch_class = (uint8_t)sum;
        if (!carry) {
            do {
                octave--;
                unsigned s = (unsigned)pitch_class + 12;
                pitch_class = (uint8_t)s;
                if (s > 0xff) break;
            } while (1);
        }
    } else {
        unsigned sum = (unsigned)pitch_class + (uint8_t)shift;
        pitch_class = (uint8_t)sum;
        while (pitch_class >= 12) {
            octave++;
            pitch_class = (uint8_t)(pitch_class - 12);
        }
    }
    note = (uint8_t)((octave << 4) | (octave >> 4));
    note |= pitch_class;
    return note;
}

// :4266
static void fnumset(struct pmd *pmd, struct pmd_qq *part, uint8_t note) {
    (void)pmd;
    uint8_t pitch_class = note & 0x0f;
    if (pitch_class == 0x0f) {
        // :4292 fnrest
        part->onkai = 0xff;
        if ((part->lfoswi & 0x11) != 0) return;
        part->fnum = 0;
        return;
    }
    part->onkai = note;

    uint8_t block = (uint8_t)(((note >> 1) | (note << 7)) & 0x38);
    uint16_t fnum = fnum_data[pitch_class];
    uint8_t high = (uint8_t)((fnum >> 8) | block);
    part->fnum = (uint16_t)((high << 8) | (uint8_t)fnum);
}

// :4302
static void fnumsetp(struct pmd *pmd, struct pmd_qq *part, uint8_t note) {
    (void)pmd;
    uint8_t pitch_class = note & 0x0f;
    if (pitch_class == 0x0f) {
        part->onkai = 0xff;
        if ((part->lfoswi & 0x11) == 0) part->fnum = 0;
        return;
    }
    part->onkai = note;

    uint8_t octave = (uint8_t)(note >> 4);
    uint16_t fnum = psg_tune_data[pitch_class];
    if (octave != 0) {
        uint16_t shifted = (uint16_t)(fnum >> octave);
        if (((fnum >> (octave - 1)) & 1) != 0) shifted++;
        fnum = shifted;
    }
    part->fnum = fnum;
}

// :4335
static void otodasi(struct pmd *pmd, struct pmd_qq *part) {
    uint16_t fnum = part->fnum;
    if (fnum == 0) return;
    if (part->slotmask == 0) return;

    uint16_t block = fnum & 0x3800;
    fnum &= 0x7ff;
    fnum = (uint16_t)(fnum + part->porta_num);
    fnum = (uint16_t)(fnum + part->detune);

    uint8_t reg = pmd->partb;
    if (fm3_active(pmd, part)) {
        if (pmd->ch3mode != 0x3f) {
            // :4398 ch3_special
            uint16_t out_block = block;
            uint8_t slots = part->slotmask;
            uint8_t lfosw = part->lfoswi;
            uint8_t volmask = part->volmask;
            if ((volmask & 0x0f) == 0) volmask = 0xf0;
            uint8_t volmask2 = part->_volmask;
            if ((volmask2 & 0x0f) == 0) volmask2 = 0xf0;

            static const uint8_t hi_reg[4] = {0xa6, 0xac, 0xae, 0xad};
            static const uint8_t lo_reg[4] = {0xa2, 0xa8, 0xaa, 0xa9};
            uint16_t detunes[4] = {
                pmd->slot_detune4, pmd->slot_detune3,
                pmd->slot_detune2, pmd->slot_detune1,
            };
            for (int slot = 0; slot < 4; slot++) {
                if ((slots & 0x80) == 0) {
                    slots = (uint8_t)(slots << 1);
                    continue;
                }
                slots = (uint8_t)(slots << 1);
                uint16_t value = (uint16_t)(fnum + detunes[slot]);
                bool use_lfo1 = (volmask & 0x80) != 0 && (lfosw & 1) != 0;
                bool use_lfo2 = (volmask2 & 0x80) != 0 && (lfosw & 0x10) != 0;
                volmask = (uint8_t)(volmask << 1);
                volmask2 = (uint8_t)(volmask2 << 1);
                if (use_lfo1) value = (uint16_t)(value + part->lfodat);
                if (use_lfo2) value = (uint16_t)(value + part->_lfodat);
                uint16_t slot_block = out_block;
                fm_block_calc(&slot_block, &value);
                value |= slot_block;
                opnset(pmd, (uint16_t)((hi_reg[slot] << 8) | (value >> 8)));
                opnset(pmd, (uint16_t)((lo_reg[slot] << 8) | (value & 0xff)));
            }
            return;
        }
    }

    if ((part->lfoswi & 1) != 0) fnum = (uint16_t)(fnum + part->lfodat);
    if ((part->lfoswi & 0x10) != 0) fnum = (uint16_t)(fnum + part->_lfodat);
    fm_block_calc(&block, &fnum);

    fnum |= block;
    reg = (uint8_t)(reg + 0xa3);
    opnset(pmd, (uint16_t)((reg << 8) | (fnum >> 8)));
    reg = (uint8_t)(reg - 4);
    opnset(pmd, (uint16_t)((reg << 8) | (fnum & 0xff)));
}

// :4549
static void fm_block_calc(uint16_t *block, uint16_t *fnum) {
    for (;;) {
        if ((*fnum & 0x8000) != 0) goto od1;
        if (*fnum < 0x26a) goto od1;
        if (*fnum < 0x26a * 2) return;
        *block = (uint16_t)(*block + 0x0800);
        if (*block == 0x4000) {
            *block = 0x3800;
            if (*fnum < 0x800) return;
            *fnum = 0x7ff;
            return;
        }
        *fnum = (uint16_t)(*fnum - 0x26a);
        continue;

od1:
        // The octave cannot drop below zero. The original subtracts and then
        // overwrites CX on the borrow path, so it happens after the check.
        if (*block < 0x0800) {
            *block = 0;
            if ((*fnum & 0x8000) == 0 && *fnum >= 8) return;
            *fnum = 8;
            return;
        }
        *block = (uint16_t)(*block - 0x0800);
        *fnum = (uint16_t)(*fnum + 0x26a);
    }
}

// :4588
static void otodasip(struct pmd *pmd, struct pmd_qq *part) {
    uint16_t fnum = part->fnum;
    if (fnum == 0) return;

    fnum = (uint16_t)(fnum + part->porta_num);

    if ((part->extendmode & 1) == 0) {
        fnum = (uint16_t)(fnum - part->detune);
        if ((part->lfoswi & 1) != 0) fnum = (uint16_t)(fnum - part->lfodat);
        if ((part->lfoswi & 0x10) != 0) fnum = (uint16_t)(fnum - part->_lfodat);
    } else {
        // Extended detune.
        if (part->detune != 0) {
            int32_t product = (int16_t)fnum * part->detune;
            int16_t delta = (int16_t)(((uint32_t)product << 4) >> 16);
            if (delta < 0) delta--;
            else delta++;
            fnum = (uint16_t)(fnum - (uint16_t)delta);
        }

        int16_t delta = 0;
        if ((part->lfoswi & 0x11) != 0) {
            if ((part->lfoswi & 1) != 0) delta = part->lfodat;
            if ((part->lfoswi & 0x10) != 0) delta = (int16_t)(delta + part->_lfodat);
            if (delta != 0) {
                int32_t product = (int16_t)fnum * delta;
                int16_t d = (int16_t)(((uint32_t)product << 4) >> 16);
                if (d < 0) d--;
                else d++;
                fnum = (uint16_t)(fnum - (uint16_t)d);
            }
        }
    }

    // :4677 tonesetp
    if (fnum >= 0x1000) fnum = (int16_t)fnum < 0 ? 0 : 0xfff;
    {
        uint8_t reg = (uint8_t)((pmd->partb - 1) * 2);
        opnset44(pmd, (uint16_t)((reg << 8) | (fnum & 0xff)));
        reg++;
        opnset44(pmd, (uint16_t)((reg << 8) | (fnum >> 8)));
    }
}

// :4726
static void volset(struct pmd *pmd, struct pmd_qq *part) {
    uint8_t slots = part->slotmask;
    if (slots == 0) return;

    uint8_t volume = part->volpush;
    if (volume != 0) volume--;
    else volume = part->volume;

    if (part != &pmd->part_e) {
        uint8_t voldown = pmd->fm_voldown;
        if (voldown != 0) {
            uint16_t product = (uint16_t)(uint8_t)(-voldown) * volume;
            volume = (uint8_t)(product >> 8);
        }
        uint8_t fade = pmd->fadeout_volume;
        if (fade >= 2) {
            fade >>= 1;
            uint16_t product = (uint16_t)(uint8_t)(-fade) * volume;
            volume = (uint8_t)(product >> 8);
        }
    }

    uint8_t vol[4] = {0x80, 0x80, 0x80, 0x80};
    uint8_t mask = 0;
    uint8_t lfo_mask = slots;

    volume = (uint8_t)~volume;
    slots &= part->carrier;
    mask |= slots;
    if ((slots & 0x80) != 0) vol[0] = volume;
    if ((slots & 0x40) != 0) vol[1] = volume;
    if ((slots & 0x20) != 0) vol[2] = volume;
    if ((slots & 0x10) != 0) vol[3] = volume;

    if (volume != 0xff) {
        if ((part->lfoswi & 2) != 0) {
            uint8_t covered = (uint8_t)(part->volmask & lfo_mask);
            mask |= covered;
            fmlfo_sub((uint8_t)part->lfodat, covered, vol);
        }
        if ((part->lfoswi & 0x20) != 0) {
            uint8_t covered = (uint8_t)(part->_volmask & lfo_mask);
            mask |= covered;
            fmlfo_sub((uint8_t)part->_lfodat, covered, vol);
        }
    }

    uint8_t reg = (uint8_t)(0x4c - 1 + pmd->partb);
    uint8_t *slot[4] = {&part->slot4, &part->slot3, &part->slot2, &part->slot1};
    for (int i = 0; i < 4; i++) {
        if ((mask & 0x80) != 0) {
            uint8_t tl = *slot[i];
            uint8_t total = vol[i];
            unsigned sum = (unsigned)total + tl;
            total = sum > 0xff ? 255 : (uint8_t)sum;
            if (total < 0x80) total = 0;
            else total -= 0x80;
            opnset(pmd, (uint16_t)((reg << 8) | total));
        }
        mask = (uint8_t)(mask << 1);
        if (i == 0) reg = (uint8_t)(reg - 8);
        else if (i == 1) reg = (uint8_t)(reg + 4);
        else if (i == 2) reg = (uint8_t)(reg - 8);
    }
}

// :4864
static void fmlfo_sub(uint8_t delta, uint8_t mask, uint8_t *vol) {
    for (int i = 0; i < 4; i++) {
        if ((mask & 0x80) == 0) {
            mask = (uint8_t)(mask << 1);
            continue;
        }
        mask = (uint8_t)(mask << 1);
        if ((delta & 0x80) == 0) {
            uint8_t v = vol[i];
            uint8_t res = (uint8_t)(v - delta);
            if (v < delta) vol[i] = 0;
            else vol[i] = res;
        } else {
            uint8_t v = vol[i];
            uint8_t res = (uint8_t)(v - delta);
            if (v >= delta) vol[i] = 0xff;
            else vol[i] = res;
        }
    }
}

// :4890
static void volsetp(struct pmd *pmd, struct pmd_qq *part) {
    if (part->envf == 3) return;
    if (part->envf == 0xff && part->eenv_count == 0) return;

    uint8_t volume = part->volpush;
    if (volume != 0) volume--;
    else volume = part->volume;

    uint8_t voldown = pmd->ssg_voldown;
    if (voldown != 0) {
        uint16_t product = (uint16_t)(uint8_t)(-voldown) * volume;
        volume = (uint8_t)(product >> 8);
    }

    uint8_t fade = pmd->fadeout_volume;
    if (fade != 0) {
        uint16_t product = (uint16_t)(uint8_t)(-fade) * volume;
        volume = (uint8_t)(product >> 8);
    }

    // :4921 psg_env_calc
    if (volume == 0) goto pv_out;

    if (part->envf == 0xff) {
        uint8_t env_volume = part->eenv_volume;
        if (env_volume == 0) {
            volume = 0;
            goto pv_out;
        }
        uint16_t product = (uint16_t)volume * (uint8_t)(env_volume + 1);
        volume = (uint8_t)product;
        bool carry = (volume & 1) != 0;
        volume >>= 1;
        carry = (volume & 1) != 0;
        volume >>= 1;
        carry = (volume & 1) != 0;
        volume >>= 1;
        carry = (volume & 1) != 0;
        volume >>= 1;
        if (carry) volume++;
    } else {
        int16_t sum = (int16_t)(int8_t)volume + (int8_t)part->penv;
        if (sum < 0) {
            volume = 0;
            goto pv_out;
        }
        volume = (uint8_t)sum;
        if (volume == 0) goto pv_out;
        if (volume >= 16) volume = 15;
    }

    // :4965 volume LFO
    if ((part->lfoswi & 0x22) != 0) {
        int16_t delta = 0;
        if ((part->lfoswi & 2) != 0) delta = part->lfodat;
        if ((part->lfoswi & 0x20) != 0) delta = (int16_t)(delta + part->_lfodat);
        int16_t sum = (int16_t)volume + delta;
        if (sum < 0) volume = 0;
        else if (sum >= 16) volume = 15;
        else volume = (uint8_t)sum;
    }

pv_out:
    {
        uint8_t reg = (uint8_t)(pmd->partb + 8 - 1);
        opnset44(pmd, (uint16_t)((reg << 8) | volume));
    }
}

// :4998
static void keyon(struct pmd *pmd, struct pmd_qq *part) {
    if (part->onkai == 0xff) return;
    uint8_t channel = (uint8_t)(pmd->partb - 1);
    uint8_t index = channel;
    uint8_t *state;
#if board2
    if (pmd->fmsel != 0) state = &pmd->ura_key1 + index;
    else state = &pmd->omote_key1 + index;
#else
    state = &pmd->omote_key1 + index;
#endif
    uint8_t keys = (uint8_t)(*state | part->slotmask);
    if (part->sdelay_c != 0) keys &= part->sdelay_m;
    *state = keys;
    channel |= keys;
#if board2
    if (pmd->fmsel != 0) channel |= 0x04;
#endif
    opnset44(pmd, (uint16_t)((0x28 << 8) | channel));
}

// :5039
static void keyonp(struct pmd *pmd, struct pmd_qq *part) {
    if (part->onkai == 0xff) return;
    uint8_t mix, channel;
    psgmsk(pmd, &mix, &channel);
    mix |= channel;
    channel &= part->psgpat;
    channel = (uint8_t)~channel;
    mix &= channel;
    opnset44(pmd, (uint16_t)((7 << 8) | mix));

    uint8_t noise = pmd->psnoi;
    if (noise == pmd->psnoi_last) return;
    if ((pmd->psgefcnum & 0x80) == 0) return;
    opnset44(pmd, (uint16_t)((6 << 8) | noise));
    pmd->psnoi_last = noise;
}

// :5074
static void psgmsk(struct pmd *pmd, uint8_t *mix, uint8_t *channel) {
    uint8_t partb = pmd->partb;
    // The original sets the carry and rotates it into AL once per part, so the
    // part's own channel bit is 1 << (partb - 1), not 1 << partb.
    *mix = (uint8_t)(partb != 0 ? 1u << (partb - 1) : 0);
    *channel = *mix;
    *mix <<= 3;
    *channel |= *mix;
    *mix = get07(pmd);
}

// :5091
static void keyoff(struct pmd *pmd, struct pmd_qq *part) {
    if (part->onkai == 0xff) return;
    keyoff_main(pmd, part);
}

// :5095
// kof1: the register write, also used when silencing a masked part.
static void keyoff_main(struct pmd *pmd, struct pmd_qq *part) {
    uint8_t channel = (uint8_t)(pmd->partb - 1);
    uint8_t index = channel;
    uint8_t *state;
#if board2
    if (pmd->fmsel != 0) state = &pmd->ura_key1 + index;
    else state = &pmd->omote_key1 + index;
#else
    state = &pmd->omote_key1 + index;
#endif
    uint8_t keys = (uint8_t)(~part->slotmask & *state);
    *state = keys;
    channel |= keys;
#if board2
    if (pmd->fmsel != 0) channel |= 0x04;
#endif
    opnset44(pmd, (uint16_t)((0x28 << 8) | channel));
}

// :5125
static void keyoffp(struct pmd *pmd, struct pmd_qq *part) {
    (void)pmd;
    if (part->onkai == 0xff) return;
    if (part->envf == 0xff) part->eenv_count = 4;
    else part->envf = 2;
}

// :5144
static void neiroset(struct pmd *pmd, struct pmd_qq *part, uint8_t voice) {
    uint8_t *tone;
    toneadr_calc(pmd, part, voice, &tone);
    uint8_t *tl = tone + 4;
    if (!silence_fmpart(pmd, part)) {
        // :5160 neiroset_main
        uint8_t reg = (uint8_t)(0xb0 - 1 + pmd->partb);
        uint8_t alg = tone[24];
        if (pmd->af_check != 0) alg = part->alg_fb;

        if (fm3_active(pmd, part)) {
            if (pmd->af_check == 0) {
                if ((part->slotmask & 0x10) == 0) {
                    uint8_t mask = (uint8_t)(pmd->fm3_alg_fb & 0x38);
                    alg = (uint8_t)((alg & 7) | mask);
                }
                pmd->fm3_alg_fb = alg;
            } else {
                alg = pmd->fm3_alg_fb;
            }
        }
        opnset(pmd, (uint16_t)((reg << 8) | alg));
        part->alg_fb = alg;
        alg &= 7;

        // :5200 check_carrier
        uint8_t carrier = carrier_table[alg];
        if ((part->volmask & 0x0f) == 0) part->volmask = carrier;
        if ((part->_volmask & 0x0f) == 0) part->_volmask = carrier;
        part->carrier = carrier;
        uint8_t neiromask2 = carrier_table[alg + 8];

        uint8_t neiromask = part->neiromask;
        neiromask2 &= neiromask;

        reg = (uint8_t)(0x30 - 1 + pmd->partb);

        // The original rotates the masks, so a fully-set neiromask walks all
        // sixteen remaining parameters.
        for (int i = 0; i < 4; i++) {
            uint8_t value = *tone++;
            if (neiromask & 0x80) opnset(pmd, (uint16_t)((reg << 8) | value));
            neiromask = (uint8_t)((neiromask << 1) | (neiromask >> 7));
            reg = (uint8_t)(reg + 4);
        }
        for (int i = 0; i < 4; i++) {
            uint8_t value = *tone++;
            if (neiromask2 & 0x80) opnset(pmd, (uint16_t)((reg << 8) | value));
            neiromask2 = (uint8_t)((neiromask2 << 1) | (neiromask2 >> 7));
            reg = (uint8_t)(reg + 4);
        }
        for (int i = 0; i < 16; i++) {
            uint8_t value = *tone++;
            if (neiromask & 0x80) opnset(pmd, (uint16_t)((reg << 8) | value));
            neiromask = (uint8_t)((neiromask << 1) | (neiromask >> 7));
            reg = (uint8_t)(reg + 4);
        }

    }
    neiroset_tl(part, tl);
}

// :5258
static void neiroset_tl(struct pmd_qq *part, const uint8_t *tl) {
    part->slot1 = tl[0];
    part->slot3 = tl[1];
    part->slot2 = tl[2];
    part->slot4 = tl[3];
}

// :5274
static void toneadr_calc(struct pmd *pmd, struct pmd_qq *part, uint8_t voice, uint8_t **tone) {
    if (pmd->prg_flg == 0 && part != &pmd->part_e) {
        *tone = pmd->tondat + (unsigned)voice * 32;
        return;
    }
    uint8_t *p = pmd->prgdat_adr;
    if (part == &pmd->part_e) p = pmd->prgdat_adr2;
    while (*p != voice) p += 26;
    *tone = p + 1;
}

// :5307
static bool silence_fmpart(struct pmd *pmd, struct pmd_qq *part) {
    uint8_t mask = part->neiromask;
    if (mask == 0) return true;

    uint8_t reg = (uint8_t)(pmd->partb + 0x3f);
    for (int i = 0; i < 4; i++) {
        if (mask & 0x80) {
            opnset(pmd, (uint16_t)((reg << 8) | 127));
            opnset(pmd, (uint16_t)(((reg + 0x40) << 8) | 127));
        }
        mask = (uint8_t)(mask << 1);
        reg = (uint8_t)(reg + 4);
    }
    keyoff_main(pmd, part);
    return false;
}

// :5341
static bool lfo(struct pmd *pmd, struct pmd_qq *part) {
    if (part->delay != 0) {
        part->delay--;
        return false;
    }

    // :5348
    if ((part->extendmode & 2) != 0) {
        uint8_t count = (uint8_t)(pmd->TimerAtime - pmd->lastTimerAtime);
        if (count == 0) return false;
        int16_t old = part->lfodat;
        do {
            lfo_main(pmd, part);
            count--;
        } while (count != 0);
        return part->lfodat != old;
    }

    {
        int16_t old = part->lfodat;
        lfo_main(pmd, part);
        return part->lfodat != old;
    }
}

// :5375
static void lfo_main(struct pmd *pmd, struct pmd_qq *part) {
    (void)pmd;
    if (part->speed != 1) {
        if (part->speed == 0xff) return;
        part->speed--;
        return;
    }

    // :5383 lfo2
    part->speed = part->speed2;
    switch (part->lfo_wave) {
    case 0:
    case 4:
    case 5:
        // :5408 lfo_sankaku
        {
            uint16_t value;
            if (part->lfo_wave == 5) {
                int8_t step = (int8_t)part->step;
                uint8_t mag = (uint8_t)(step < 0 ? -step : step);
                value = (uint16_t)((int16_t)step * (int16_t)mag);
            } else {
                value = (uint16_t)(int16_t)(int8_t)part->step;
            }
            part->lfodat = (int16_t)(part->lfodat + (int16_t)value);
            if (part->lfodat == 0) md_inc(part);
        }
        {
            // :5429 lfo3
            uint8_t time = part->time;
            if (time != 0xff) {
                time--;
                if (time == 0) {
                    time = part->time2;
                    if (part->lfo_wave != 4) time = (uint8_t)(time + time);
                    part->time = time;
                    part->step = (uint8_t)(0 - part->step);
                    return;
                }
            }
            part->time = time;
            return;
        }

    case 1:
        // :5434 lfo_nokogiri
        part->lfodat = (int16_t)(part->lfodat + (int16_t)(int8_t)part->step);
        {
            // :5450 nk_lfo3
            uint8_t time = part->time;
            if (time != 0xff) {
                time--;
                if (time == 0) {
                    part->lfodat = (int16_t)(0 - part->lfodat);
                    md_inc(part);
                    time = part->time2;
                    time = (uint8_t)(time + time);
                }
            }
            part->time = time;
            return;
        }

    case 6:
        // :5454 lfo_oneshot
        {
            uint8_t time = part->time;
            if (time == 0) return;
            if (time != 0xff) {
                time--;
                part->time = time;
            }
            part->lfodat = (int16_t)(part->lfodat + (int16_t)(int8_t)part->step);
            return;
        }

    case 2:
        // :5470 lfo_kukei
        {
            uint16_t value = (uint16_t)((int16_t)(int8_t)part->step *
                                        (int16_t)(int8_t)part->time);
            part->lfodat = (int16_t)value;
            md_inc(part);
            part->step = (uint8_t)(0 - part->step);
            return;
        }

    default:
        // :5479 not_nokogiri / lfo_random, waves 3 and above
        {
            uint8_t step = part->step;
            if ((step & 0x80) != 0) step = (uint8_t)(0 - step);
            uint16_t x = (uint16_t)((unsigned)step * part->time);
            uint16_t r = rnd(pmd, (uint16_t)(x + x));
            part->lfodat = (int16_t)(r - x);
            return;
        }
    }
}

// :5499
static void md_inc(struct pmd_qq *part) {
    part->mdspd--;
    if (part->mdspd != 0) return;
    part->mdspd = part->mdspd2;

    uint8_t count = part->mdc;
    if (count == 0) return;
    if ((count & 0x80) == 0) part->mdc = (uint8_t)(count - 1);

    // :5512 mdi21
    uint8_t step = part->step;
    if ((step & 0x80) != 0) {
        step = (uint8_t)(0 - step);
        step = (uint8_t)(step + part->mdepth);
        if ((step & 0x80) != 0) {
            step = 0;
            if ((part->mdepth & 0x80) == 0) step = 0x81;
            part->step = step;
            return;
        }
        step = (uint8_t)(0 - step);
        part->step = step;
        return;
    }
    step = (uint8_t)(step + part->mdepth);
    if ((step & 0x80) != 0) {
        step = 0;
        if ((part->mdepth & 0x80) == 0) step = 0x7f;
        part->step = step;
        return;
    }
    part->step = step;
}

// :5548
static uint16_t rnd(struct pmd *pmd, uint16_t range) {
    uint16_t span = range;
    uint32_t product = (uint32_t)259 * pmd->seed;
    uint16_t value = (uint16_t)((uint16_t)product + 3) & 0x7fff;
    pmd->seed = value;
    uint32_t p2 = (uint32_t)value * span;
    return (uint16_t)(p2 / 32767);
}

// :5568
static uint8_t lfoinitp(struct pmd *pmd, struct pmd_qq *part, uint8_t note) {
    uint8_t pitch_class = note & 0x0f;
    if (pitch_class == 0x0c) {
        note = part->onkai_def;
        pitch_class = note & 0x0f;
    }
    part->onkai_def = note;

    if (pitch_class != 0x0f) {
        part->porta_num = 0;
        if ((pmd->tieflag & 1) == 0) {
            if (part->envf == 0xff) {
                // :5612 extenv_init, the extended SSG envelope
                part->eenv_arc = (uint8_t)(part->eenv_ar - 16);
                uint8_t v = (uint8_t)(part->eenv_dr - 16);
                if ((v & 0x80) != 0) v = (uint8_t)(v + v);
                part->eenv_drc = v;
                v = (uint8_t)(part->eenv_sr - 16);
                if ((v & 0x80) != 0) v = (uint8_t)(v + v);
                part->eenv_src = v;
                part->eenv_rrc = (uint8_t)((part->eenv_rr + part->eenv_rr) - 16);
                part->eenv_volume = part->eenv_al;
                part->eenv_count = 1;
                ext_ssgenv_main(part);
            } else {
                // :5593 seinit
                part->envf = 0;
                part->penv = 0;
                uint8_t pattern = part->patb;
                part->pat = pattern;
                if (pattern == 0) {
                    part->envf = 1;
                    part->penv = part->pv2;
                }
                part->pr1 = part->pr1b;
                part->pr2 = part->pr2b;
            }
            lfin1(pmd, part);
            return note;
        }
    }

    // :5584 lfo_exitp
    soft_env(pmd, part);
    lfo_exit_common(pmd, part);
    return note;
}

// :5645
static uint8_t lfoinit(struct pmd *pmd, struct pmd_qq *part, uint8_t note) {
    uint8_t pitch_class = note & 0x0f;
    if (pitch_class == 0x0c) {
        note = part->onkai_def;
        pitch_class = note & 0x0f;
    }
    part->onkai_def = note;
    if (pitch_class != 0x0f) {
        part->porta_num = 0;
        if ((pmd->tieflag & 1) == 0) {
            lfin1(pmd, part);
            return note;
        }
    }

    // :5665 lfo_exit
    lfo_exit_common(pmd, part);
    return note;
}

// :5661
// The shared tail of lfoinit and lfoinitp.
static void lfo_exit_common(struct pmd *pmd, struct pmd_qq *part) {
    if ((part->lfoswi & 3) != 0) lfo(pmd, part);
    if ((part->lfoswi & 0x30) != 0) {
        lfo_change(part);
        lfo(pmd, part);
        lfo_change(part);
    }
}

// :5684 lfin1, shared by lfoinit and lfoinitp.
static void lfin1(struct pmd *pmd, struct pmd_qq *part) {
#if board2
    part->hldelay_c = part->hldelay;
    if (part->hldelay != 0) {
        opnset(pmd, (uint16_t)(((pmd->partb + 0xb3) << 8) |
                               (part->fmpan & 0xc0)));
    }
#endif
    part->sdelay_c = part->sdelay;

    uint8_t lfosw = part->lfoswi;
    if ((lfosw & 3) != 0) {
        if ((lfosw & 4) == 0) lfoinit_main(part);
        lfo(pmd, part);
    }
    if ((lfosw & 0x30) != 0) {
        if ((lfosw & 0x40) == 0) {
            lfo_change(part);
            lfoinit_main(part);
            lfo_change(part);
        }
        lfo_change(part);
        lfo(pmd, part);
        lfo_change(part);
    }
}

// :5737
static void lfoinit_main(struct pmd_qq *part) {
    part->lfodat = 0;
    part->delay = part->delay2;
    part->speed = part->speed2;
    part->step = part->step2;
    part->time = part->time2;
    part->mdc = part->mdc2;

    if (part->lfo_wave == 2 || part->lfo_wave == 3) {
        part->speed = 1;
        return;
    }
    part->speed++;
}

// :5761
static bool soft_env(struct pmd *pmd, struct pmd_qq *part) {
    if ((part->extendmode & 4) != 0) {
        uint8_t count = (uint8_t)(pmd->TimerAtime - pmd->lastTimerAtime);
        if (count == 0) return false;
        uint8_t changed = 0;
        do {
            if (soft_env_main(part)) changed = 1;
            count--;
        } while (count != 0);
        return changed != 0;
    }
    return soft_env_main(part);
}

// :5778
static bool soft_env_main(struct pmd_qq *part) {
    if (part->envf == 0xff) return ext_ssgenv_main(part);
    uint8_t old = part->penv;
    soft_env_sub(part, &old);
    return old != part->penv;
}

// :5789
static void soft_env_sub(struct pmd_qq *part, uint8_t *value) {
    (void)value;
    if (part->envf == 0) {
        // Attack
        part->pat--;
        if (part->pat != 0) return;
        part->envf = 1;
        part->penv = part->pv2;
        return;
    }
    if (part->envf == 2) goto se3;

    // Decay
    if (part->pr1 == 0) return;
    part->pr1--;
    if (part->pr1 != 0) return;
    part->pr1 = part->pr1b;
    part->penv--;

se4:
    if (part->penv >= (uint8_t)-15) return;
    if (part->penv < 15) return;
se5:
    part->penv = (uint8_t)-15;
    return;

se3:
    // Release
    if (part->pr2 == 0) goto se5;
    part->pr2--;
    if (part->pr2 != 0) return;
    part->pr2 = part->pr2b;
    part->penv--;
    goto se4;
}

// :5835
static bool ext_ssgenv_main(struct pmd_qq *part) {
    uint8_t count = part->eenv_count;
    if (count == 0) return false;
    uint8_t old = part->eenv_volume;
    uint8_t volume = part->eenv_volume;
    esm_sub(part, &count, &volume);
    return old != part->eenv_volume;
}

// :5849
static void esm_sub(struct pmd_qq *part, uint8_t *count, uint8_t *value) {
    (void)value;
    (*count)--;
    if (*count != 0) goto esm_dr_check;

    // Attack Rate
    {
        uint8_t rate = (uint8_t)(part->eenv_arc - 1);
        if ((rate & 0x80) != 0) goto arc_count_check;
        rate++;
        part->eenv_volume = (uint8_t)(part->eenv_volume + rate);
        if (part->eenv_volume < 15) {
            part->eenv_arc = (uint8_t)(part->eenv_ar - 16);
            return;
        }
        part->eenv_volume = 15;
        part->eenv_count++;
        if (part->eenv_sl != 15) return;
        part->eenv_count++;
        return;
arc_count_check:
        if (part->eenv_ar == 0) return;
        part->eenv_arc++;
        return;
    }

esm_dr_check:
    (*count)--;
    if (*count != 0) goto esm_sr_check;

    // Decay Rate
    {
        uint8_t rate = (uint8_t)(part->eenv_drc - 1);
        if ((rate & 0x80) != 0) goto drc_count_check;
        rate++;
        uint8_t volume = (uint8_t)(part->eenv_volume - rate);
        bool borrow = part->eenv_volume < rate;
        part->eenv_volume = volume;
        if (borrow) goto dr_slset;
        if (part->eenv_volume < part->eenv_sl) goto dr_slset;
        uint8_t scaled = (uint8_t)(part->eenv_dr - 16);
        if ((scaled & 0x80) != 0) scaled = (uint8_t)(scaled + scaled);
        part->eenv_drc = scaled;
        return;
dr_slset:
        part->eenv_volume = part->eenv_sl;
        part->eenv_count++;
        return;
drc_count_check:
        if (part->eenv_dr == 0) return;
        part->eenv_drc++;
        return;
    }

esm_sr_check:
    (*count)--;
    if (*count != 0) goto esm_rr;

    // Sustain Rate
    {
        uint8_t rate = (uint8_t)(part->eenv_src - 1);
        if ((rate & 0x80) != 0) goto src_count_check;
        rate++;
        uint8_t volume = (uint8_t)(part->eenv_volume - rate);
        if (part->eenv_volume < rate) {
            part->eenv_volume = 0;
        } else {
            part->eenv_volume = volume;
        }
        uint8_t scaled = (uint8_t)(part->eenv_sr - 16);
        if ((scaled & 0x80) != 0) scaled = (uint8_t)(scaled + scaled);
        part->eenv_src = scaled;
        return;
src_count_check:
        if (part->eenv_sr == 0) return;
        part->eenv_src++;
        return;
    }

esm_rr:
    // Release Rate
    {
        uint8_t rate = (uint8_t)(part->eenv_rrc - 1);
        if ((rate & 0x80) != 0) goto rrc_count_check;
        rate++;
        uint8_t volume = (uint8_t)(part->eenv_volume - rate);
        if (part->eenv_volume < rate) part->eenv_volume = 0;
        else part->eenv_volume = volume;
        part->eenv_rrc = (uint8_t)((part->eenv_rr + part->eenv_rr) - 16);
        return;
rrc_count_check:
        if (part->eenv_rr == 0) return;
        part->eenv_rrc++;
        return;
    }
}

// :5967
static void fadeout(struct pmd *pmd) {
    if (pmd->pause_flag == 1) return;
    uint8_t speed = pmd->fadeout_speed;
    if (speed == 0) return;

    unsigned sum = (unsigned)speed + pmd->fadeout_volume;
    if ((speed & 0x80) == 0) {
        // :5978 fadeout_end
        if (sum > 0xff) {
            pmd->fadeout_volume = 255;
            pmd->fadeout_speed = 0;
            if (pmd->fade_stop_flag == 1) pmd->music_flag |= 2;
            return;
        }
    } else {
        // :5987 fade_in
        if (sum <= 0xff) {
            pmd->fadeout_volume = 0;
            pmd->fadeout_speed = 0;
#if board2
            volset2rf(pmd, pmd->rhyvol);
#endif
            return;
        }
    }
    pmd->fadeout_volume = (uint8_t)sum;
}

// :6005
static void setint(struct pmd *pmd) {
    pmd->tempo_d = 200;
    pmd->tempo_d_push = 200;
    calc_tb_tempo(pmd);
    settempo_b(pmd);

    opnset44(pmd, 0x2500);
    opnset44(pmd, 0x2400); // Timer A: 9216us
    opnset44(pmd, 0x273f); // timer enable

    pmd->opncount = 0;
    pmd->syousetu = 0;
    pmd->syousetu_lng = 96;
}

// :6040
static void silence(struct pmd *pmd) {
    static const uint8_t fmoff_nef[] = {
        0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14, 0xff,
    };
    static const uint8_t fmoff_ef[] = {0, 1, 4, 5, 8, 9, 12, 13, 0xff};
#if board2
    sel44(pmd);
    // :6045 oploop masks both chips, the first chip's first.
    for (uint8_t bank = 2; bank != 0; bank--) {
#endif
        const uint8_t *table;
        if (pmd->fm_effec_flag == 1
#if board2
            && bank == 1
#endif
        ) {
            table = fmoff_ef;
        } else {
            table = fmoff_nef;
        }
        for (;;) {
            uint8_t reg = *table++;
            if (reg == 0xff) break;
            opnset(pmd, (uint16_t)(((reg + 0x80) << 8) | 0xff));
        }

#if board2
        sel46(pmd);
    }
#endif

    {
        uint16_t cmd = 0x2800;
        uint8_t count = 3;
#if !board2
        if (pmd->fm_effec_flag == 1) count--;
#endif
        do {
            opnset44(pmd, cmd);
            cmd = (uint16_t)(cmd + 1); // the channel number is the data byte
            count--;
        } while (count != 0);
    }
#if board2
    {
        uint16_t cmd = 0x2804;
        uint8_t count = 3;
        if (pmd->fm_effec_flag == 1) count--;
        do {
            opnset44(pmd, cmd);
            cmd = (uint16_t)(cmd + 1);
            count--;
        } while (count != 0);
    }
#endif

    if (pmd->effon != 0) {
        // :6107 psg_ef
        uint8_t value = (uint8_t)((get07(pmd) & 0x3f) | 0x9b);
        opnset44(pmd, (uint16_t)((0x07 << 8) | value));
    } else {
        if (pmd->ppsdrv_flag != 0 && pmd->pps != NULL &&
            pmd->pps->keyoff != NULL) {
            pmd->pps->keyoff(pmd->user_data);
        }
        opnset44(pmd, 0x07bf); // PSG KEYOFF
    }

    // :6118 s_pcm
#if board2
    // :6126 a missing ADPCM RAM skips the second chip's whole reset block,
    // not just the PAN/PCM writes at its start.
    if (pmd->pcmflag == 0
#if adpcm && !ademu
        && pmd->pcm_gs_flag != 1
#endif
    ) {
#if adpcm && !ademu
        opnset46(pmd, 0x0102);
        opnset46(pmd, 0x0001);
#endif
        opnset46(pmd, 0x1080);
        opnset46(pmd, 0x1018);
#if pcm
        stop_86pcm(pmd);
#endif
    }
#if ppz
    {
        const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
        if (drv != NULL) {
            drv->fifo_interrupt_stop(pmd->user_data);
            for (uint8_t chan = 0; chan < 8; chan++) {
                drv->keyoff(chan, pmd->user_data);
            }
        }
    }
#endif
#endif
}

// The original brackets every register write with pushf/cli/sti and restores
// the interrupt flag afterwards. The host owns that exclusion, so these
// helpers perform the I/O only.
//
// :6173
static void opnset44(struct pmd *pmd, uint16_t cmd) {
    rdychk(pmd, pmd->fm1_port1);
    outb(pmd, pmd->fm1_port1, (uint8_t)(cmd >> 8));
    _waitP(pmd);
    outb(pmd, pmd->fm1_port2, (uint8_t)cmd);
}

#if board2
// :6200
static void opnset46(struct pmd *pmd, uint16_t cmd) {
    rdychk(pmd, pmd->fm2_port1);
    outb(pmd, pmd->fm2_port1, (uint8_t)(cmd >> 8));
    _waitP(pmd);
    outb(pmd, pmd->fm2_port2, (uint8_t)cmd);
}
#endif

// :6226
static void opnset(struct pmd *pmd, uint16_t cmd) {
    rdychk(pmd, pmd->fm_port1);
    outb(pmd, pmd->fm_port1, (uint8_t)(cmd >> 8));
    _waitP(pmd);
    outb(pmd, pmd->fm_port2, (uint8_t)cmd);
}

// :6253
// Reads PSG register 07h. The original requires its callers to run with
// interrupts disabled.
static uint8_t get07(struct pmd *pmd) {
    rdychk(pmd, pmd->fm1_port1);
    outb(pmd, pmd->fm1_port1, 7);
    _waitP(pmd);
    return inb(pmd, pmd->fm1_port2);
}

// :6408
uint8_t *pmd_get_musdat_adr(struct pmd *pmd) { return pmd->dataarea; }

// :6422
uint8_t *pmd_get_tondat_adr(struct pmd *pmd) { return pmd->tondat; }

// :6429
uint8_t *pmd_get_efcdat_adr(struct pmd *pmd) { return pmd->efcdat; }

// :6436
uint8_t pmd_get_fv(struct pmd *pmd) {
    int60_enter(pmd, 0x08);
    uint8_t value = pmd->board != 0 ? pmd->fadeout_volume : PMD_NO_BOARD;
    int60_exit(pmd);
    return value;
}

// :6441
void pmd_set_fv(struct pmd *pmd, uint8_t vol) {
    int60_enter(pmd, 0x19);
    if (pmd->board != 0) {
        pmd->fadeout_volume = vol;
#if board2
        // :6446 A silent master volume still restores the rhythm volume.
        if (vol == 0) volset2rf(pmd, pmd->rhyvol);
#endif
    }
    int60_exit(pmd);
}

// :6452
struct pmd_drv_info pmd_drv_chk(struct pmd *pmd) {
    int60_enter(pmd, 0x09);
    struct pmd_drv_info info;
    info.board = PMD_DRV_BOARD_ABSENT;
    info.version = vers;
    info.version_char = (uint8_t)verc[0];
    if (pmd->board != 0) {
#if board2 && ppz
#if ademu
        info.board = PMD_DRV_BOARD_PPZE;
#else
        info.board = PMD_DRV_BOARD_PPZ;
#endif
#elif board2 && pcm
        info.board = PMD_DRV_BOARD_86;
#elif board2
        info.board = PMD_DRV_BOARD_B2;
#else
        info.board = PMD_DRV_BOARD_PMD;
#endif
    }
    int60_exit(pmd);
    return info;
}

// :6482
union pmd_pcm_table *pmd_get_pcm_adr(struct pmd *pmd) {
#if board2
    return &pmd->pcm_table;
#else
    // The default and VA1 builds have no PCM driver, and so no table.
    (void)pmd;
    return NULL;
#endif
}

// :6488
struct pmd_qq **pmd_get_workadr(struct pmd *pmd) { return pmd->part_data_table; }

// :6494
uint8_t pmd_get_fmefc_num(struct pmd *pmd) {
    int60_enter(pmd, 0x11);
    uint8_t value = pmd->board != 0 ? pmd->fm_effec_num : PMD_NO_BOARD;
    int60_exit(pmd);
    return value;
}

// :6499
uint8_t pmd_get_pcmefc_num(struct pmd *pmd) {
    int60_enter(pmd, 0x12);
    uint8_t value = pmd->board != 0 ? pmd->pcm_effec_num : PMD_NO_BOARD;
    int60_exit(pmd);
    return value;
}

// :6504
void pmd_set_fm_int(struct pmd *pmd, struct pmd_int_handler handler) {
    int60_enter(pmd, 0x13);
    if (pmd->board != 0) {
        pmd->fmint = handler;
        pmd->intfook_flag |= 1;
        // :6510 The callback forbids resident removal until it is cleared.
        pmd->rescut_cant |= 0x80;
        if (handler.fn == NULL) {
            pmd->intfook_flag &= (uint8_t)~1;
            pmd->rescut_cant &= (uint8_t)~0x80;
        }
    }
    int60_exit(pmd);
}

// :6518
void pmd_set_efc_int(struct pmd *pmd, struct pmd_int_handler handler) {
    int60_enter(pmd, 0x14);
    if (pmd->board != 0) {
        pmd->efcint = handler;
        pmd->intfook_flag |= 2;
        pmd->rescut_cant |= 0x40;
        if (handler.fn == NULL) {
            pmd->intfook_flag &= (uint8_t)~2;
            pmd->rescut_cant &= (uint8_t)~0x40;
        }
    }
    int60_exit(pmd);
}

// :6532
struct pmd_int_handler pmd_get_fm_int(struct pmd *pmd) {
    int60_enter(pmd, 0x1f);
    struct pmd_int_handler handler = pmd->fmint;
    int60_exit(pmd);
    return handler;
}

// :6539
struct pmd_int_handler pmd_get_efc_int(struct pmd *pmd) {
    int60_enter(pmd, 0x20);
    struct pmd_int_handler handler = pmd->efcint;
    int60_exit(pmd);
    return handler;
}

// :6546
struct pmd_psgefcnum pmd_get_psgefcnum(struct pmd *pmd) {
    int60_enter(pmd, 0x15);
    struct pmd_psgefcnum result;
    if (pmd->board != 0) {
        result.psgefcnum = pmd->psgefcnum;
        result.effon = pmd->effon;
    } else {
        result.psgefcnum = PMD_NO_BOARD;
        result.effon = PMD_NO_BOARD;
    }
    int60_exit(pmd);
    return result;
}

// :6553
uint8_t pmd_get_ppsdrv_flag(struct pmd *pmd) {
    int60_enter(pmd, 0x17);
    uint8_t value = pmd->board != 0 ? pmd->ppsdrv_flag : PMD_NO_BOARD;
    int60_exit(pmd);
    return value;
}

// :6558
// The original checks INT 64h for PPSDRV's resident signature; the vtable
// pointer is that check in the port.
void pmd_set_ppsdrv_flag(struct pmd *pmd, uint8_t flag) {
    bool ok = int60_enter(pmd, 0x18);
    if (ok && pmd->board != 0) {
        pmd->ppsdrv_flag = 0;
        if (pmd->pps != NULL) pmd->ppsdrv_flag = flag;
    }
    int60_exit(pmd);
}

// :6574
char *pmd_get_mus_name(struct pmd *pmd) { return pmd->mus_filename; }

// :6580
struct pmd_size pmd_get_size(struct pmd *pmd) {
    struct pmd_size size;
    size.music = pmd->mmldat_lng;
    size.voice = pmd->voicedat_lng;
    size.effect = pmd->effecdat_lng;
    return size;
}

// :6592
// Returns both axes. The original brackets each PSG access with cli/sti;
// the host owns that exclusion.
static struct pmd_joy get_joy(struct pmd *pmd) {
    struct pmd_joy joy;
    uint16_t port1 = pmd->fm1_port1;
    uint16_t port2 = pmd->fm1_port2;

    // :6600 mode set
    opnset44(pmd, (uint16_t)((0x07 << 8) | ((get07(pmd) & 0x3f) | 0x80)));

    // :6605 joystick init (1)
    rdychk(pmd, port1);
    outb(pmd, port1, 0x0f);
    _wait(pmd);
    outb(pmd, port2, (uint8_t)((inb(pmd, port2) & 0x0f) | 0x80));

    // :6615 joystick data (1)
    rdychk(pmd, port1);
    outb(pmd, port1, 0x0e);
    _wait(pmd);
    joy.joystick1 = (uint8_t)(inb(pmd, port2) & 0x3f);

    // :6625 joystick init (2)
    rdychk(pmd, port1);
    outb(pmd, port1, 0x0f);
    _wait(pmd);
    outb(pmd, port2, (uint8_t)((inb(pmd, port2) & 0x0f) | 0xc0));

    // :6635 joystick data (2)
    rdychk(pmd, port1);
    outb(pmd, port1, 0x0e);
    _wait(pmd);
    joy.joystick2 = (uint8_t)(inb(pmd, port2) & 0x3f);
    return joy;
}

// :6592
struct pmd_joy pmd_get_joy(struct pmd *pmd) {
    int60_enter(pmd, 0x16);
    struct pmd_joy joy;
    if (pmd->board != 0) {
        joy = get_joy(pmd);
    } else {
        joy.joystick1 = PMD_NO_BOARD;
        joy.joystick2 = PMD_NO_BOARD;
    }
    int60_exit(pmd);
    return joy;
}

// :6663
void pmd_pause_on(struct pmd *pmd) {
    bool ok = int60_enter(pmd, 0x1a);
    if (ok && pmd->board != 0 && pmd->play_flag != 0) {
        pmd->play_flag = 0;
        pmd->pause_flag = 1;
        silence(pmd);
    }
    int60_exit(pmd);
}

// :6675
void pmd_pause_off(struct pmd *pmd) {
    bool ok = int60_enter(pmd, 0x1b);
    if (!ok || pmd->board == 0) {
        int60_exit(pmd);
        return;
    }
    if (pmd->play_flag != 0 || pmd->pause_flag != 1) {
        int60_exit(pmd);
        return;
    }

#if board2
    // :6680 Neither chip is mid-song here, so the select stays put.
    sel44(pmd);
#endif
    pmd->partb = 1;
    neiro_reset(pmd, &pmd->parts[part1]);
    pmd->partb = 2;
    neiro_reset(pmd, &pmd->parts[part2]);
#if !board2
    if (pmd->fm_effec_flag != 1) {
#else
    {
#endif
        pmd->partb = 3;
        neiro_reset(pmd, &pmd->parts[part3]);
        neiro_reset(pmd, &pmd->parts[part3b]);
        neiro_reset(pmd, &pmd->parts[part3c]);
        neiro_reset(pmd, &pmd->parts[part3d]);
    }

#if board2
    sel46(pmd);
    pmd->partb = 1;
    neiro_reset(pmd, &pmd->parts[part4]);
    pmd->partb = 2;
    neiro_reset(pmd, &pmd->parts[part5]);
    if (pmd->fm_effec_flag != 1) {
        pmd->partb = 3;
        neiro_reset(pmd, &pmd->parts[part6]);
    }
#endif

    // :6732 pauoff_exit
    pmd->pause_flag = 0;
    pmd->play_flag = 1;
    int60_exit(pmd);
}

// :6741
static uint8_t *get_memo(struct pmd *pmd, uint8_t n) {
    uint8_t *src = pmd->mmlbuf;
    if ((uint16_t)(src[0] | (src[1] << 8)) != 0x1a) {
        return NULL; // no voice data: the memo table address is unavailable
    }

    src = pmd->mmlbuf + (uint16_t)(src[0x18] | (src[0x19] << 8)) - 4;
    uint8_t version = src[2];
    uint8_t marker = src[3];
    if (version != 0x40 && (marker != 0xfe || version < 0x41)) return NULL;

    // :6782 The memo table starts one entry further in MC 4.2 and again in
    // 4.8.
    if (version >= 0x42) n++;
    if (version >= 0x48) n++;
    src = pmd->mmlbuf + (uint16_t)(src[0] | (src[1] << 8));

    // :6792 One-based, and an underflowed count walks the whole table.
    n++;
    uint16_t offset;
    do {
        offset = (uint16_t)(src[0] | (src[1] << 8));
        if (offset == 0) return NULL;
        src += 2;
    } while (--n != 0);

    return pmd->mmlbuf + offset;
}

// :6741
uint8_t *pmd_get_memo(struct pmd *pmd, uint8_t n) {
    int60_enter(pmd, 0x1d);
    uint8_t *memo = get_memo(pmd, n);
    int60_exit(pmd);
    return memo;
}

// :6797
uint8_t pmd_ff_music(struct pmd *pmd, uint16_t bar) {
    bool ok = int60_enter(pmd, 0x1c);
    // Without a board the original leaves AL untouched; the typed interface
    // reports an explicit sentinel instead. The stop state check inside
    // ff_music_main cannot reject a call, only report AL=2.
    uint8_t result = PMD_FF_MUSIC_NO_BOARD;
    if (!ok) result = PMD_FF_MUSIC_REJECTED;
    else if (pmd->board != 0) result = ff_music_main(pmd, bar);
    int60_exit(pmd);
    return result;
}

// :6820
// Fast-forwards to `bar`. The original masks the FM interrupt around this
// call; the host owns that exclusion.
static uint8_t ff_music_main(struct pmd *pmd, uint16_t bar) {
    if (pmd->status2 == 0xff) return PMD_FF_MUSIC_STOPPED;

    pmd->skip_flag = 1;
    bool skip = bar > pmd->syousetu;
    if (!skip) {
        // :6835 Restart from the top before skipping forward.
        pmd->skip_flag = 2;
        if (pmd->effon == 1) effend(pmd);
        data_init2(pmd);
        play_init(pmd);
        opn_init(pmd);
        skip = bar != 0;
    }

    if (skip) {
        // :6844 ffm_main
        maskon_all(pmd);
#if board2
        opnset44(pmd, 0x10ff); // dump the rhythm source
#endif

        // :6856 The fadeout and rhythm mask are suspended for the skip.
        uint8_t fadeout_volume = pmd->fadeout_volume;
        uint8_t rhythmmask = pmd->rhythmmask;
        pmd->fadeout_volume = 0xff;
        pmd->rhythmmask = 0;

        uint16_t target = bar;
        for (;;) {
            mmain(pmd);
            syousetu_count(pmd);
            if (pmd->status2 == 0xff) {
                // :6886 ffm_exit1
                maskoff_all(pmd);
                pmd->skip_flag = 0;
                return PMD_FF_MUSIC_PAST;
            }
            if (target <= pmd->syousetu) break;
        }

        pmd->fadeout_volume = fadeout_volume;
        pmd->rhythmmask = rhythmmask;
#if board2
        if (fadeout_volume == 0) volset2rf(pmd, pmd->rhyvol);
#endif
        maskoff_all(pmd);
    } else {
        silence(pmd);
    }

    // :6881 ffm_exit0b
    stb_ff(pmd, (uint8_t)(pmd->ff_tempo - 1));
    pmd->skip_flag = 0;
    return PMD_FF_MUSIC_OK;
}

// :6902
static void maskon_all(struct pmd *pmd) {
    const uint8_t(*entry)[3] = part_table;
    for (unsigned i = 0; i < max_part1; i++) {
        struct pmd_qq *part = &pmd->parts[i];
#if ppz
        // :6910 The PPZ8 build's work array continues with the PPZ8 parts, so
        // the Rhythm and Effect entries are skipped here.
        if ((*entry)[0] == 0xff) entry += 2;
#endif
        uint8_t partb = (*entry)[1];
        uint8_t source = (*entry)[2];
        entry++;

        part->partmask |= 0x80;
        if (part->partmask != 0x80) continue; // already masked elsewhere
        maskon_main(pmd, part, partb, source);
    }
}

// :6935
static void maskoff_all(struct pmd *pmd) {
    const uint8_t(*entry)[3] = part_table;
    for (unsigned i = 0; i < max_part1; i++) {
        struct pmd_qq *part = &pmd->parts[i];
#if ppz
        if ((*entry)[0] == 0xff) entry += 2;
#endif
        uint8_t partb = (*entry)[1];
        uint8_t source = (*entry)[2];
        entry++;

        part->partmask &= 0x7f;
        if (part->partmask != 0) continue; // still masked elsewhere
        maskoff_main(pmd, part, partb, source);
    }
}

// :6967
// Masks (`n` < 80h) or unmasks (`n` >= 80h) one part, keying off or restoring
// its sound.
static void part_mask(struct pmd *pmd, uint8_t n) {
    uint8_t index = n & 0x7f;
#if ppz
    if (index >= 16 + 8) return;
#else
    if (index >= 16) return;
#endif
    if ((n & 0x80) != 0) {
        part_on(pmd, index);
        return;
    }

    const uint8_t(*entry)[3] = &part_table[index];
    int8_t work = (int8_t)(*entry)[0];
    if (work < 0) {
        rhythm_mask(pmd);
        return;
    }
    uint8_t partb = (*entry)[1];
    uint8_t ongen = (*entry)[2];

    struct pmd_qq *part = pmd->part_data_table[(uint8_t)work];
    uint8_t previous = part->partmask;
    part->partmask |= 1;
    if (previous != 0) return; // already masked
    if (pmd->play_flag == 0) return; // the music is stopped

    maskon_main(pmd, part, partb, ongen);
}

// :6967
void pmd_part_mask(struct pmd *pmd, uint8_t part) {
    bool ok = int60_enter(pmd, 0x1e);
    if (ok && pmd->board != 0) part_mask(pmd, part);
    int60_exit(pmd);
}

// :7001
// The original walks AH down through the sound sources; the switch keeps that
// order, including the deletes that make a source meaningless in a variant.
static void maskon_main(struct pmd *pmd, struct pmd_qq *part, uint8_t partb, uint8_t source) {
    switch (source) {
    case 0: // pm_fm1
        pmd->partb = partb;
#if board2
        sel44(pmd);
#endif
        (void)silence_fmpart(pmd, part);
        return;
#if board2
    case 1: // pm_fm2
        pmd->partb = partb;
        sel46(pmd);
        (void)silence_fmpart(pmd, part);
        return;
#endif
    case 2: { // pm_ssg
        uint8_t mix, channel;
        pmd->partb = partb;
        psgmsk(pmd, &mix, &channel);
        opnset44(pmd, (uint16_t)((0x07 << 8) | (mix | channel)));
        return;
    }
#if board2
    case 3: // pm_pcm
#if adpcm
#if ademu
        if (pmd->adpcm_emulate == 1) {
            const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
            if (drv != NULL) drv->keyoff(7, pmd->user_data);
        }
#else
        opnset46(pmd, 0x0102); // PAN=0, x8 bit mode
        opnset46(pmd, 0x0001); // PCM reset
#endif
#endif
#if pcm
        stop_86pcm(pmd);
#endif
        return;
#endif
    case 4: // pm_drums
        if (pmd->psgefcnum < 11) effend(pmd);
        return;
#if ppz
    case 5: // pm_ppz
#if ademu
        if (partb == 7 && pmd->adpcm_emulate == 1) return;
#endif
        {
            const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
            if (drv != NULL) drv->keyoff(partb, pmd->user_data);
        }
        return;
#endif
    default:
        // The original's dec chain falls through here for the sources a
        // variant does not have.
        return;
    }
}

// :7100
static void rhythm_mask(struct pmd *pmd) {
    pmd->rhythmmask = 0;
#if board2
    opnset44(pmd, 0x10ff); // dump the rhythm source
#endif
}

// :7111
// Unmasks one part and restores its voice, given the part number.
static void part_on(struct pmd *pmd, uint8_t n) {
    const uint8_t(*entry)[3] = &part_table[n];
    int8_t work = (int8_t)(*entry)[0];
    if (work < 0) {
        pmd->rhythmmask = 0xff;
        return;
    }
    uint8_t partb = (*entry)[1];
    uint8_t ongen = (*entry)[2];

    struct pmd_qq *part = pmd->part_data_table[(uint8_t)work];
    if (part->partmask == 0) return; // not masked
    part->partmask &= (uint8_t)~1;
    if (part->partmask != 0) return; // still masked by an effect
    if (pmd->play_flag == 0) return; // the music is stopped

    maskoff_main(pmd, part, partb, ongen);
}

// :7135
static void maskoff_main(struct pmd *pmd, struct pmd_qq *part, uint8_t partb, uint8_t source) {
    switch (source) {
    case 0: // po_fm1
        pmd->partb = partb;
#if board2
        sel44(pmd);
#endif
        if (part->address != NULL) neiro_reset(pmd, part);
        return;
#if board2
    case 1: // po_fm2
        pmd->partb = partb;
        sel46(pmd);
        if (part->address != NULL) neiro_reset(pmd, part);
        return;
#endif
    default:
        // Only the FM sources need their voice restored.
        return;
    }
}

// :7354
static void fm_effect_on(struct pmd *pmd, uint8_t efcnum) {
    if (pmd->fm_effec_flag != 0) fm_effect_off(pmd);

    pmd->fm_effec_num = efcnum;
    pmd->fm_effec_flag = 1;

    pmd->partb = 3;
#if board2
    pmd->parts[part6].partmask |= 2;
#else
    pmd->parts[part3].partmask |= 2;
    pmd->parts[part3b].partmask |= 2;
    pmd->parts[part3c].partmask |= 2;
    pmd->parts[part3d].partmask |= 2;
#endif

    struct pmd_qq *part = &pmd->part_e;
    memset(part, 0, sizeof(*part));

    uint16_t offset = (uint16_t)(pmd->efcdat[2 * efcnum] |
                                (pmd->efcdat[2 * efcnum + 1] << 8));
    part->address = pmd->efcdat + offset;
    part->leng = 1; // starts on the next count
    part->volume = 108; // FM volume default
    part->slotmask = 0xf0;
    part->neiromask = 0xff;
#if board2
    part->fmpan = 0xc0; // centre
    sel46(pmd);
    opnset(pmd, 0xb6c0);
#else
    pmd->ch3mode_push = pmd->ch3mode;
    pmd->ch3mode = 0x3f;
#endif
}

// :7354
void pmd_fm_effect_on(struct pmd *pmd, uint8_t efcnum) {
    bool ok = int60_enter(pmd, 0x0c);
    if (ok && pmd->board != 0) fm_effect_on(pmd, efcnum);
    int60_exit(pmd);
}

// :7419
static void fm_effect_off(struct pmd *pmd) {
    if (pmd->fm_effec_flag == 0) return;

    pmd->fm_effec_num = 0xff;
    pmd->fm_effec_flag = 0;

#if board2
    sel46(pmd);
#endif
    pmd->partb = 3;
    (void)silence_fmpart(pmd, &pmd->part_e);

    if (pmd->play_flag == 0) return; // the music is stopped

#if board2
    neiro_reset(pmd, &pmd->parts[part6]);
#else
    neiro_reset(pmd, &pmd->parts[part3]);
    neiro_reset(pmd, &pmd->parts[part3b]);
    neiro_reset(pmd, &pmd->parts[part3c]);
    neiro_reset(pmd, &pmd->parts[part3d]);

    // :7452 Restore the FM3 mode the effect overrode, without a reset.
    pmd->ch3mode = pmd->ch3mode_push;
    opnset44(pmd, (uint16_t)((0x27 << 8) | (pmd->ch3mode & 0xcf)));
#endif
}

// :7419
void pmd_fm_effect_off(struct pmd *pmd) {
    bool ok = int60_enter(pmd, 0x0d);
    if (ok && pmd->board != 0) fm_effect_off(pmd);
    int60_exit(pmd);
}

// :7492
// The FM timer interrupt handler. The host installs it through
// pmd_hw.install_fm_timer_handler and calls it when the FM IRQ fires.
static void opnint(struct pmd *pmd) {
    // :7493 The original's cli/cld, PIC mask (:7517-7522), EOI (:7533), old
    // INT 5h handler chaining (:7578-7595), and private stack switch
    // (:7501-7508) are host-owned.
    // :7499 int5_flag is a nesting counter, not a reentrancy guard.
    pmd->int5_flag++;

#if vsync
    // :7524-7531 stop VSync for the duration of the handler.
    uint8_t vsync_save = inb(pmd, 2);
    outb(pmd, 2, (uint8_t)(vsync_save | 4));
#endif

    // :7538 INT_check, retried from the top after each handler runs.
    for (;;) {
#if pcm
        // :7540 Check the PCM86 FIFO interrupt bit.
        if ((inb(pmd, 0xa468) & 0x10) != 0) {
            fifo_main(pmd);
            continue;
        }
#endif
        // :7556 _ppz
        ppz_int_hook(pmd);
        // :7561-7566
        if ((rdychk(pmd, pmd->fm1_port1) & 3) != 0) {
            FM_Timer_main(pmd);
            continue;
        }
        break;
    }

    // :7571 opnint_fin

#if vsync
    // :7606-7613 `or dl,11111011b` then `and al,dl`: the saved VSync bit is
    // restored only when the live read still has it set.
    outb(pmd, 2, (uint8_t)(inb(pmd, 2) & (vsync_save | 0xfb)));
#endif

    // :7633
    pmd->int5_flag--;
}

// :7663
// Confirms which timer fired, resets both timer flags, and dispatches to the
// per-timer handlers.
static void FM_Timer_main(struct pmd *pmd) {
    // :7669-7678 Read the status through the 27h address and write ch3mode
    // back: writing it clears both timer flags.
    (void)rdychk(pmd, pmd->fm1_port1);
    outb(pmd, pmd->fm1_port1, 0x27);
    _wait(pmd);
    uint8_t ch3mode = pmd->ch3mode;
    uint8_t status = inb(pmd, pmd->fm1_port1);
    outb(pmd, pmd->fm1_port2, ch3mode);

    // :7680
    status &= 3;

    // :7685-7688 The original re-enables interrupts unless disint is 1; the
    // host owns that exclusion.

    // :7701-7712 `dec ah; jz` twice: 1 = Timer A, 2 = Timer B, and anything
    // else (including the impossible 0) falls through to both, Timer B first.
    // BUG: a 0 status therefore runs both handlers instead of being ignored,
    // though opnint's own check makes it unreachable in practice.
    if (status == 1) {
        TimerA_main(pmd);
    } else if (status == 2) {
        TimerB_main(pmd);
    } else {
        TimerB_main(pmd);
        TimerA_main(pmd);
    }
}

// :7725
// Timer B work: the music start/stop handshake and the sequencer tick.
#if sync
// The MIDI sync build advances the music from the sync source, so the timer
// handler's Timer B path is inert; the AH=FFh entry point (:7728 opnint_sub)
// runs the same work through pmd_opnint_sub.
static void TimerB_main(struct pmd *pmd) { (void)pmd; }

// :7728
void pmd_opnint_sub(struct pmd *pmd) {
#else
static void TimerB_main(struct pmd *pmd) {
#endif
    // :7730
    pmd->TimerBflag = 1;

    // :7732-7742
    if (pmd->music_flag != 0) {
        if ((pmd->music_flag & 1) != 0) mstart(pmd);
        if ((pmd->music_flag & 2) != 0) mstop(pmd);
    }

    // :7744-7752
    if (pmd->play_flag != 0) {
        mmain(pmd);
        settempo_b(pmd);
        syousetu_count(pmd);
        pmd->lastTimerAtime = pmd->TimerAtime;
    }
    // :7753
    pmd->TimerBflag = 0;

    // :7755-7757
    if ((pmd->intfook_flag & 1) != 0 && pmd->fmint.fn != NULL) {
        pmd->fmint.fn(pmd->fmint.user_data);
    }
}

// :7764
// Timer A work: the bar counter, the effect drivers, and the stop-key check.
static void TimerA_main(struct pmd *pmd) {
    // :7765
    pmd->TimerAflag = 1;

    // :7767
    pmd->TimerAtime++;

    // :7769-7773
    if ((pmd->TimerAtime & 7) == 0) {
        fadeout(pmd);
        rew(pmd);
    }

    // :7776-7783
    if (pmd->effon != 0 &&
        (pmd->ppsdrv_flag == 0 || (pmd->psgefcnum & 0x80) == 0)) {
        effplay(pmd);
    }

    // :7786-7788
    if (pmd->fm_effec_flag != 0) fm_efcplay(pmd);

    // :7791-7818 CTRL+ESC (non-VA) stops at the next Timer B.
    if (pmd->key_check != 0 && pmd->play_flag != 0 && check_esc(pmd)) {
        pmd->music_flag |= 2;
        pmd->fadeout_flag = 0;
    }

    // :7820
    pmd->TimerAflag = 0;

    // :7822-7824
    if ((pmd->intfook_flag & 2) != 0 && pmd->efcint.fn != NULL) {
        pmd->efcint.fn(pmd->efcint.user_data);
    }
}

// :7803
// The CTRL+ESC (non-VA) or CTRL+ESC VA check used by TimerA_main.
static bool check_esc(struct pmd *pmd) {
    if (pmd->key_check == 0) return false;
    uint8_t state = read_key_state(pmd);
#if va
    uint8_t port = key_port8(state);
    return (port & pmd->esc_sp_key) == pmd->esc_sp_key &&
           (state & PMD_KEY_ESC) != 0;
#else
    uint8_t status = state & 0x1f;
    return (status & pmd->esc_sp_key) == pmd->esc_sp_key &&
           (state & PMD_KEY_ESC) != 0;
#endif
}

// :7808
// Reads the host's normalized key state.
static uint8_t read_key_state(struct pmd *pmd) {
    if (pmd->hw == NULL || pmd->hw->read_key_state == NULL) return 0;
    return pmd->hw->read_key_state(pmd->user_data);
}

#if va
// :7808
// The VA variant's port 8 is active low; translate the normalized bits to
// the original key positions (CTRL bit 7, SHIFT bit 6, CAPS bit 5, GRPH
// bit 4, KANA bit 3).
static uint8_t key_port8(uint8_t state) {
    uint8_t port = 0;
    if (state & PMD_KEY_CTRL) port |= 0x80;
    if (state & PMD_KEY_SHIFT) port |= 0x40;
    if (state & PMD_KEY_CAPS) port |= 0x20;
    if (state & PMD_KEY_GRPH) port |= 0x10;
    if (state & PMD_KEY_KANA) port |= 0x08;
    return port;
}
#endif

// :7831
// Counts bars.
static void syousetu_count(struct pmd *pmd) {
    uint8_t count = (uint8_t)(pmd->opncount + 1);
    if (count == pmd->syousetu_lng) {
        count = 0;
        pmd->syousetu++;
    }
    pmd->opncount = count;
}

// :7844
// Sets Timer B from the current tempo or the fast-forward tempo.
static void settempo_b(struct pmd *pmd) {
    if (check_grph(pmd, pmd->grph_sp_key)) stb_ff(pmd, pmd->ff_tempo);
    else stb_ff(pmd, pmd->tempo_d);
}

// :7852
// Writes the Timer B speed when it changed. settempo_b's tempo path and
// ff_music_main's fast-forward exit share it.
static void stb_ff(struct pmd *pmd, uint8_t tempo) {
    if (tempo == pmd->TimerB_speed) return;
    pmd->TimerB_speed = tempo;
    opnset44(pmd, (uint16_t)((0x26 << 8) | tempo));
}

// :7864
// Rewinds one bar when the rewind key is held.
static void rew(struct pmd *pmd) {
    if (!check_grph(pmd, pmd->rew_sp_key)) return;
    uint16_t bar = pmd->syousetu;
    uint8_t threshold = (uint8_t)(pmd->syousetu_lng >> 2);
    if (pmd->opncount < threshold && bar != 0) bar--;
    ff_music_main(pmd, bar);
}

// :7887
static bool check_grph(struct pmd *pmd, uint8_t mask) {
    if (pmd->key_check == 0) return false;
    uint8_t state = read_key_state(pmd);
#if va
    uint8_t port = key_port8(state);
    return (port & mask) == mask && (port & 0x10) != 0;
#else
    uint8_t status = state & 0x1f;
    return (status & mask) == mask && (status & 0x08) != 0;
#endif
}

// :8385
// The original continues into `resident_exit` (INT 21h/AH=31h) and stays
// resident; pmd_resident_main's return is the port's residency transition.
static void resident_exit_end(struct pmd *pmd) {
    // :8391 voice area
    if (pmd->voicedat_lng != 0) {
        memset(pmd->tondat, 0, (size_t)pmd->voicedat_lng * 1024);
    }

    // :8399 effect area: 128 little-endian 0100h entries and then 80h.
    if (pmd->effecdat_lng != 0) {
        for (unsigned i = 0; i < 128; i++) {
            pmd->efcdat[2 * i] = 0x00;
            pmd->efcdat[2 * i + 1] = 0x01;
        }
        pmd->efcdat[256] = 0x80;
    }
}

// :8555
// The PMDOPT option scan: the original uses opt_sp_push to remember that a
// usage error is not fatal here.
static bool _set_option(struct pmd *pmd, const struct pmd_option *table, const char *cmd) {
    return set_option(pmd, table, cmd, true);
}

// :8604
// The original counts the resident size in 64-byte paragraphs and compares it
// against the largest MCB block it can get. The host owns the memory, so the
// resident data area itself is the bound: the largest allowed configuration
// ends exactly at the end of dataarea. This check must stay unconditional,
// because the 40KB limit at :8582 only runs for a command line, while PMDOPT
// can request buffers that do not fit.
static void memchk_init(struct pmd *pmd) {
    unsigned paragraphs = (unsigned)pmd->mmldat_lng * 64;
    paragraphs += (unsigned)pmd->voicedat_lng * 64;
    paragraphs += (unsigned)pmd->effecdat_lng * 64;
    if (paragraphs > sizeof(pmd->dataarea) / 16) {
        pmderr_main(pmd, pmderror_mes7);
    }
}

#if !va
// :8707
// Writes the initial register values to every candidate port whose 0Eh
// interrupt-number bits match the board int_init detected.
static void opninit_loop(struct pmd *pmd) {
    for (unsigned i = 0; i < 4; i++) {
        uint16_t port = (uint16_t)(0x088 + (i << 8));

        uint8_t id = 0xff;
        bool present = false;
        for (unsigned n = 0; n < 256; n++) {
            id &= inb(pmd, port);
            if ((id & 0x80) == 0) {
                present = true;
                break;
            }
        }
        if (!present) continue; // no sound chip on this port

        rdychk(pmd, port);
        outb(pmd, port, 0x0e);
        if ((inb(pmd, port + 2) & 0xc0) != pmd->opn_0eh) continue;

        opnset_fmc(pmd, port, 0x2900);
        opnset_fmc(pmd, port, 0x2400);
        opnset_fmc(pmd, port, 0x2500);
        opnset_fmc(pmd, port, 0x2600);
        opnset_fmc(pmd, port, 0x273f);
    }
}
#endif

// :8845
// Detects the FM board, selects its ports, and sets up the wait counts and the
// ADPCM state.
static void fm_check(struct pmd *pmd) {
    if (!port_check(pmd)) {
        // :8884
        pmd->board = 0;
        if (pmd->message_flag != 0) {
            print_mes(pmd, mes_warning);
            print_mes(pmd, mes_not_board);
        }
        return;
    }

    // :8852
    pmd->fm_port1 = pmd->fm1_port1;
    pmd->fm_port2 = pmd->fm1_port2;
#if !va
    // :8860 writes the detected port into the resident "Port 88H" text.
    pmd->mes_int[mes_int_port_num] = (char)('0' + (pmd->fm1_port2 >> 8));
#endif

    // :8866
    wait_check(pmd);

    // :8872
#if board2 && adpcm
    opnset44(pmd, 0x2983); // 2608 on
#if ademu
    pmd->pcm_gs_flag = 1; // ADPCM is emulated and not used
#else
    adpcm_ram_check(pmd); // ADRAMCHK.ASM
#endif
#else
    pmd->pcm_gs_flag = 1; // ADPCM is not used
#endif
    pmd->board = 1;
}

// :8899
// Reads the board's interrupt level, reports the chip, and initializes the
// PPSDRV/PPZ8 state. DOS vector installation and the PIC mask/EOI setup
// (:9020) are host-owned; pmd_resident_main installs the handler through
// pmd_hw.install_fm_timer_handler.
static void int_init(struct pmd *pmd) {
    // :8904
    rdychk(pmd, pmd->fm1_port1);
    outb(pmd, pmd->fm1_port1, 7);
    _wait(pmd); // PSG write wait
    outb(pmd, pmd->fm1_port2, 0x3f);

    rdychk(pmd, pmd->fm1_port1);
    outb(pmd, pmd->fm1_port1, 0x0e);
    _wait(pmd); // PSG read wait
    uint8_t level = inb(pmd, pmd->fm1_port2) & 0xc0;
#if !va
    pmd->opn_0eh = level;
#endif
    // :8929 The original rotates the two interrupt-number bits into a 4-byte
    // table index and then multiplies by 4; the two steps cancel out.
    pmd->mes_int[mes_int_int_num] = vector_table[level >> 6].num;
    pmd->int_level = vector_table[level >> 6].level;

    // :8942
    if (pmd->message_flag != 0) {
        print_mes(pmd, pmd->mes_int);
        print_mes(pmd, pmd->ongen != 0 ? pmd->mes_ongen1 : mes_ongen0);
#if board2 && !ademu
#if pcm
        print_mes(pmd, mes_pcm);
#endif
#if adpcm
        print_mes(pmd, pmd->pcm_gs_flag != 0 ? mes_only : mes_adpcm);
#endif
#endif
        print_mes(pmd, mes_int2);
    }

    // :8971 PPSDRV check and the kp_rhythm_flag default it drives.
    if (pmd->ppsdrv_flag != 0xff) {
        if (pmd->kp_rhythm_flag == 0xff) {
            pmd->kp_rhythm_flag = (uint8_t)(pmd->ppsdrv_flag ^ 1);
        }
    } else if (ppsdrv_check(pmd)) {
        pmd->ppsdrv_flag = 1;
        if (pmd->kp_rhythm_flag == 0xff) pmd->kp_rhythm_flag = 0;
    } else {
        pmd->ppsdrv_flag = 0;
        if (pmd->kp_rhythm_flag == 0xff) pmd->kp_rhythm_flag = 1;
    }
    if (pmd->message_flag != 0 && pmd->ppsdrv_flag == 1) {
        print_mes(pmd, mes_ppsdrv);
    }

#if ppz
    // :9008 PPZ8 negotiation. The 0410h handshake returns PPZ8's interrupt
    // level; the directly callable FM entry from 0409h is represented by
    // pmd_drv_ppz8.fm_int, so both the level match and a non-NULL entry are
    // required for the direct call (ppz_call_seg >= 2). 1901h forbids removal.
    if (ppz8_check(pmd)) {
        pmd->ppz_call_seg = 1;
        if (pmd->ppz8->get_irq_level(pmd->user_data) ==
                (uint8_t)(pmd->int_level + 8) &&
            pmd->ppz8->fm_int != NULL) {
            pmd->ppz_call_seg = 2;
        }
        pmd->ppz8->set_removal_protection(true, pmd->user_data);
        if (pmd->message_flag != 0) print_mes(pmd, mes_ppz8);
    }
#endif
}

// :9083
// PPSDRV is available when the host supplies its vtable; the original checks
// INT 64h's resident signature instead.
static bool ppsdrv_check(struct pmd *pmd) { return pmd->pps != NULL; }

#if ppz
// :9110
// Same as ppsdrv_check for PPZ8 (INT 7Fh in the original).
static bool ppz8_check(struct pmd *pmd) { return pmd->ppz8 != NULL; }
#endif

// :9136
// Enables the OPN interrupt and resets the part work for playback. The
// original's intset and PIC unmask (:9155-9160) are host-owned; see below.
static void opnint_start(struct pmd *pmd) {
    if (pmd->board == 0) return;

    // :9141 Part work all reset.
    memset(pmd->parts, 0, sizeof(pmd->parts[0]) * max_part1);
    pmd->rhythmmask = 0xff;
    pmd->rhydmy = 0xff;

    data_init(pmd);
    opn_init(pmd);
    opnset44(pmd, 0x07bf); // 07h port init
    mstop(pmd);
    setint(pmd);

    // :9155 The original calls intset(int_level) and unmasks the PIC; the
    // host owns the interrupt source and its mask.

    opnset44(pmd, 0x2983);
}

#if !va
// :9174
// OPN out for the 088/188/288/388 initialization: address to DX, data to DX+2.
static void opnset_fmc(struct pmd *pmd, uint16_t port, uint16_t cmd) {
    // The original delays 256 `loop $` iterations before each write
    // (:9177-9178, :9181-9182); the host backend owns the chip timing.
    outb(pmd, port, (uint8_t)(cmd >> 8));
    outb(pmd, port + 2, (uint8_t)cmd);
}
#endif

// :9196
// Probes the FM board and fills in fm1_port1/2, fm2_port1/2, and ongen.
// Returns false when no board was found (the original's carry flag). A NULL hw
// models a host without a board and is rejected before any vtable dereference.
static bool port_check(struct pmd *pmd) {
    if (pmd->hw == NULL) return false;
    // The original brackets its probe sequences with cli/sti and uses inline
    // `loop $` settle delays; the host owns exclusion and chip timing.
#if va
    pmd->fm1_port1 = 0x44;
    pmd->fm1_port2 = 0x45;
    pmd->fm2_port1 = 0x46;
    pmd->fm2_port2 = 0x47;
    // :9205 VA+NORM check (PMDVA1).
    pmd->ongen = 0;
    outb(pmd, 0x44, 0x0b);
    outb(pmd, 0x45, 0xaa);
    if (inb(pmd, 0x45) != 0xaa) return false;
    // :9218 VA+BOARD2 check (PMDVA).
    outb(pmd, 0x44, 0xff);
    uint8_t n = inb(pmd, 0x45);
#if board2
    if (n != 1) return false;
    pmd->ongen = 1;
#else
    if (n == 1) pmd->ongen = 1;
    // :9231 otherwise this is the plain VA board: found with ongen = 0.
#endif
    return true;
#elif board2 && (pcm || ppz)
    // :9240 98+86B check (PMD86/PMDPPZ): an 86 board is required.
    return check_86b(pmd);
#elif board2
    // :9246 98+SPB check (PMDB2).
    if (check_spb(pmd)) return true;
    return check_86b(pmd);
#else
    // :9255 98+NORM check (PMD).
    if (check_spb(pmd)) return true;
    if (check_86b(pmd)) return true;

    // :9262 the 088h-388h YM2203-class cards.
    pmd->ongen = 0;
    uint8_t tries = 4;
    uint16_t base = 0x088;
    if (pmd->port_sel != -1) {
        base = (uint16_t)((base & 0xff) | (pmd->port_sel << 8));
        tries = 1;
    }
    for (;;) {
        outb(pmd, base, 0x0b);
        outb(pmd, base + 2, 0xaa);
        if (inb(pmd, base + 2) == 0xaa) {
            set_port(pmd, base);
            return true;
        }
        if (--tries == 0) return false;
        base = (uint16_t)(base + 0x100);
    }
#endif
}

#if !va
// :9314
// Probes for an 86 board at port_sel or 188h/288h and switches the NEC OPNA
// on. Returns false when no board was found (the original's carry flag).
static bool check_86b(struct pmd *pmd) {
    pmd->ongen = 1;
    // :9323 The original recognises an EPSON machine through absolute reads of
    // the ROM at 0FD80:0002/0004 and then rejects the 86 board below a
    // PC-286VE. Those PC-98-specific reads are omitted, so epson_flag stays 0
    // and an EPSON host is treated like any other.
    // :9334 No NEC OPNA ID port means no 86 board.
    uint8_t id = inb(pmd, 0xa460);
    if (id == 0xff) return false;
    outb(pmd, 0x5f, id);
    outb(pmd, 0xa460, (uint8_t)(id & 0xfc)); // NEC OPNA off

    uint8_t tries = 2;
    uint16_t base = 0x188;
    if (pmd->port_sel != -1) {
        base = (uint16_t)((base & 0xff) | (pmd->port_sel << 8));
        tries = 1;
    }
    for (;;) {
        outb(pmd, base, 0xff);
        if (inb(pmd, base + 2) == 1) { // YM2608?
            uint8_t mask = inb(pmd, base + 4);
            if ((uint8_t)((inb(pmd, base + 6) & mask) + 1) == 0) break;
        }
        if (--tries == 0) return false;
        base = (uint16_t)(base + 0x100);
    }

    // :9133
    id = inb(pmd, 0xa460);
    outb(pmd, 0x5f, id);
    outb(pmd, 0xa460, (uint8_t)((id & 0xfc) | 1)); // NEC OPNA on
    uint8_t v = inb(pmd, 0xa66e);
    outb(pmd, 0x5f, v);
    outb(pmd, 0xa66e, (uint8_t)(v & 0xfe)); // a66e B0: 1 = mute
    // :9390 The original's optional EPSON 24KHz display-mode write (`out 6Eh`
    // with AL=1) is omitted together with the ROM reads above: it only runs
    // when epson_flag is set, which is now always 0.
#if pcm
    // :9152 Stop PCM playback before the port is reused.
    stop_86pcm(pmd);
#endif
    set_port(pmd, base);
    return true;
}

#if !pcm && !ppz
// :9406
// Probes for an SPB (YM2608) at port_sel or 088h-388h. Returns false when no
// board was found.
static bool check_spb(struct pmd *pmd) {
    pmd->ongen = 1;
    // The EPSON ROM reads are omitted as in check_86b (:9318-9323). Unlike
    // check_86b, an EPSON machine only skips the OPNA off write here.
    uint8_t id = inb(pmd, 0xa460);
    if (id != 0xff) {
        outb(pmd, 0x5f, id);
        outb(pmd, 0xa460, (uint8_t)(id & 0xfc)); // NEC OPNA off
    }

    uint8_t tries = 4;
    uint16_t base = 0x088;
    if (pmd->port_sel != -1) {
        base = (uint16_t)((base & 0xff) | (pmd->port_sel << 8));
        tries = 1;
    }
    for (;;) {
        outb(pmd, base, 0xff);
        if (inb(pmd, base + 2) == 1) { // YM2608?
            uint8_t mask = inb(pmd, base + 4);
            if ((uint8_t)((inb(pmd, base + 6) & mask) + 1) != 0) {
                set_port(pmd, base);
                return true;
            }
        }
        if (--tries == 0) return false;
        base = (uint16_t)(base + 0x100);
    }
}
#endif

// :9468
// Sets fm1/fm2 from the base port a probe matched.
static void set_port(struct pmd *pmd, uint16_t base) {
    pmd->fm1_port1 = base;
    pmd->fm1_port2 = (uint16_t)(base + 2);
    pmd->fm2_port1 = (uint16_t)(base + 4);
    pmd->fm2_port2 = (uint16_t)(base + 6);
}
#endif

// :9511
static void put_status(struct pmd *pmd) {
    // :9515 data buffer sizes
    print_mes(pmd, changemes_01);
    put8(pmd, pmd->mmldat_lng);
    print_mes(pmd, changemes_02);
    put8(pmd, pmd->voicedat_lng);
    print_mes(pmd, changemes_03);
    put8(pmd, pmd->effecdat_lng);

    // :9531 volume adjustments
    print_mes(pmd, changemes_1);
    put8(pmd, pmd->_fm_voldown);
    print_mes(pmd, crlf);
    print_mes(pmd, changemes_2);
    put8(pmd, pmd->_ssg_voldown);
    print_mes(pmd, crlf);
#if board2
    print_mes(pmd, changemes_3);
    put8(pmd, pmd->_pcm_voldown);
    print_mes(pmd, crlf);
#if ppz
    print_mes(pmd, changemes_3z);
    put8(pmd, pmd->_ppz_voldown);
    print_mes(pmd, crlf);
#endif
    print_mes(pmd, changemes_4);
    put8(pmd, pmd->_rhythm_voldown);
    print_mes(pmd, crlf);

    // :9578 SSG drum rhythm
    print_mes(pmd, changemes_5);
    print_mes(pmd, pmd->kp_rhythm_flag != 0 ? changemes_5a : changemes_5b);
#endif

    // :9586
    print_mes(pmd, changemes_6);
    print_mes(pmd, pmd->key_check != 0 ? changemes_6a : changemes_6b);

    // :9596 key masks
    print_mes(pmd, changemes_7);
    put8(pmd, sp_key_display(pmd->esc_sp_key));
    print_mes(pmd, crlf);
    print_mes(pmd, changemes_8);
    put8(pmd, sp_key_display(pmd->grph_sp_key));
    print_mes(pmd, crlf);
    print_mes(pmd, changemes_8b);
    put8(pmd, sp_key_display(pmd->rew_sp_key));
    print_mes(pmd, crlf);

    // :9624
    print_mes(pmd, changemes_9);
    print_mes(pmd, pmd->fade_stop_flag != 0 ? changemes_6a : changemes_6b);

    // :9632
    print_mes(pmd, changemes_10);
    print_mes(pmd, pmd->disint != 0 ? changemes_6a : changemes_6b);

    // :9640
    print_mes(pmd, changemes_11);
    print_mes(pmd, pmd->ppsdrv_flag != 0 ? changemes_6a : changemes_6b);
#if ppz
    // :9647
    print_mes(pmd, changemes_11_);
    print_mes(pmd, pmd->ppz_call_seg != 0 ? changemes_6a : changemes_6b);
#endif

    // :9656
    print_mes(pmd, changemes_12);
    print_16(pmd, pmd->wait_clock);
    print_mes(pmd, changemes_12a);
    print_16(pmd, (uint16_t)(pmd->wait_clock * pmd->wait1_clock));
    print_mes(pmd, changemes_12b);

    // :9668
    print_mes(pmd, changemes_13);
    print_16(pmd, pmd->ff_tempo);
    print_mes(pmd, crlf);

#if board2 && adpcm && !ademu
    // :9677
    print_mes(pmd, changemes_14);
    if (pmd->adpcm_wait == 0) {
        print_mes(pmd, changemes_14a);
    } else if (pmd->adpcm_wait == 1) {
        print_mes(pmd, changemes_14b);
    } else {
        print_mes(pmd, changemes_14c);
    }
#endif
#if board2 && pcm
    // :9690
    print_mes(pmd, changemes_15);
    print_mes(pmd, pmd->_pcm86_vol != 0 ? changemes_15b : changemes_15a);

    // :9700
    print_mes(pmd, changemes_16);
    print_mes(pmd, changemes_16a[pmd->p86_freq]);
    if (pmd->p86_freq != 8) print_mes(pmd, changemes_16b);
#endif
}

// :9598
// The VA variants store key masks rotated; the status shows the three-bit
// value.
static uint8_t sp_key_display(uint8_t key) {
#if va
    return (uint8_t)(((key << 3) | (key >> 5)) & 0x07);
#else
    return key;
#endif
}

// :9715
// Prints `value` as a decimal number without leading zeros.
static void put8(struct pmd *pmd, uint8_t value) {
    bool printed = false;
    p8_oneset(pmd, &value, 100, &printed);
    p8_oneset(pmd, &value, 10, &printed);
    print_char(pmd, (char)('0' + value));
}

// :9726
static void p8_oneset(struct pmd *pmd, uint8_t *value, uint8_t digit, bool *printed) {
    char c = '0';
    while (*value >= digit) {
        *value = (uint8_t)(*value - digit);
        c++;
    }
    if (*printed || c != '0') {
        print_char(pmd, c);
        *printed = true;
    }
}

// :9753
// /R. The original's resident_cut_main never returns: it prints mes_cut and
// exits. The port's removal sequence runs here, and pmd_change_main reports
// PMD_CHANGE_REMOVED instead.
static bool resident_cut_main(struct pmd *pmd, const char **cmd, bool from_env) {
    (void)cmd;
    (void)from_env;
    pmd_resident_cut(pmd);
    return false;
}

// :9927
// Scans `cmd` for slash/dash options and dispatches them through `table`
// (:9939). Returns true at the end of the command line, false when parsing
// must stop: a bad PMDOPT option (`usage`) or /R.
static bool set_option(struct pmd *pmd, const struct pmd_option *table, const char *cmd, bool from_env) {
    for (;;) {
        char c = *cmd++;
        if (c == '/' || c == '-') {
            c = to_upper(*cmd++);
            const struct pmd_option *entry = table;
            for (;;) {
                if (entry->name == 0) return usage(pmd, from_env);
                if (entry->name == c) break;
                entry++;
            }
            if (!entry->fn(pmd, &cmd, from_env)) return false;
            continue;
        }
        if (is_cntrl(c)) return true;
        if (c == ' ') continue;
        return usage(pmd, from_env);
    }
}

// :9958
static bool muslng_get(struct pmd *pmd, const char **cmd, bool from_env) {
    uint8_t n;
    if (!get_comline_number(cmd, &n)) return usage(pmd, from_env);
    pmd->mmldat_lng = n;
    return true;
}

// :9967
static bool voilng_get(struct pmd *pmd, const char **cmd, bool from_env) {
    uint8_t n;
    if (!get_comline_number(cmd, &n)) return usage(pmd, from_env);
    pmd->voicedat_lng = n;
    return true;
}

// :9976
static bool efclng_get(struct pmd *pmd, const char **cmd, bool from_env) {
    uint8_t n;
    if (!get_comline_number(cmd, &n)) return usage(pmd, from_env);
    pmd->effecdat_lng = n;
    return true;
}

// :9985
// /DF, /DS, /DP, /DZ, /DR.
static bool fmvd_set(struct pmd *pmd, const char **cmd, bool from_env) {
    char c = to_upper(*(*cmd)++);
    uint8_t n;
    if (!get_comline_number(cmd, &n)) return usage(pmd, from_env);
    switch (c) {
    case 'F':
        pmd->fm_voldown = n;
        pmd->_fm_voldown = n;
        return true;
    case 'S':
        pmd->ssg_voldown = n;
        pmd->_ssg_voldown = n;
        return true;
    case 'P':
        pmd->pcm_voldown = n;
        pmd->_pcm_voldown = n;
        return true;
    case 'Z':
        pmd->ppz_voldown = n;
        pmd->_ppz_voldown = n;
        return true;
    case 'R':
        pmd->rhythm_voldown = n;
        pmd->_rhythm_voldown = n;
        return true;
    default:
        return usage(pmd, from_env);
    }
}

// :10046
// /K, /KG, /KR, /KE, and /K-.
static bool keycheck(struct pmd *pmd, const char **cmd, bool from_env) {
    char c = **cmd;
    if (c == '-') {
        (*cmd)++;
        pmd->key_check = 1;
        return true;
    }
    c = to_upper(c);
    if (c == 'G') {
        (*cmd)++;
        return grph_special_set(pmd, cmd, from_env);
    }
    if (c == 'R') {
        (*cmd)++;
        return rew_special_set(pmd, cmd, from_env);
    }
    if (c == 'E') {
        (*cmd)++;
        return esc_special_set(pmd, cmd, from_env);
    }
    pmd->key_check = 0;
    return true;
}

// :10067
// The original ignores get_comline_number's carry flag here, so a missing
// number stores 0 rather than being a usage error.
static bool grph_special_set(struct pmd *pmd, const char **cmd, bool from_env) {
    uint8_t n;
    (void)from_env;
    (void)get_comline_number(cmd, &n);
#if va
    n = (uint8_t)(((n >> 3) | (n << 5)) & 0xe0); // ror al,3 + mask
#endif
    pmd->grph_sp_key = n;
    return true;
}

// :10081
// The original ignores get_comline_number's carry flag here, so a missing
// number stores 0 rather than being a usage error.
static bool rew_special_set(struct pmd *pmd, const char **cmd, bool from_env) {
    uint8_t n;
    (void)from_env;
    (void)get_comline_number(cmd, &n);
#if va
    n = (uint8_t)(((n >> 3) | (n << 5)) & 0xe0);
#endif
    pmd->rew_sp_key = n;
    return true;
}

// :10095
// The original ignores get_comline_number's carry flag here, so a missing
// number stores 0 rather than being a usage error.
static bool esc_special_set(struct pmd *pmd, const char **cmd, bool from_env) {
    uint8_t n;
    (void)from_env;
    (void)get_comline_number(cmd, &n);
#if va
    n = (uint8_t)(((n >> 3) | (n << 5)) & 0xe0);
#endif
    pmd->esc_sp_key = n;
    return true;
}

// :10109
static bool disint_set(struct pmd *pmd, const char **cmd, bool from_env) {
    (void)from_env;
    if (**cmd == '-') {
        (*cmd)++;
        pmd->disint = 0;
    } else {
        pmd->disint = 1;
    }
    return true;
}

// :10122
static bool notshot_set(struct pmd *pmd, const char **cmd, bool from_env) {
    (void)from_env;
    if (**cmd == '-') {
        (*cmd)++;
        pmd->kp_rhythm_flag = 1;
    } else {
        pmd->kp_rhythm_flag = 0;
    }
    return true;
}

// :10135
static bool notstop_set(struct pmd *pmd, const char **cmd, bool from_env) {
    (void)from_env;
    if (**cmd == '-') {
        (*cmd)++;
        pmd->fade_stop_flag = 1;
    } else {
        pmd->fade_stop_flag = 0;
    }
    return true;
}

// :10148
static bool ppsdrv_reset(struct pmd *pmd, const char **cmd, bool from_env) {
    (void)from_env;
    if (**cmd != '-') {
        pmd->ppsdrv_flag = 0;
        return true;
    }
    (*cmd)++;
    pmd->ppsdrv_flag = ppsdrv_check(pmd) ? 1 : 0;
    return true;
}

// :10164
static bool ppz_reset(struct pmd *pmd, const char **cmd, bool from_env) {
    (void)from_env;
    // The original runs this with interrupts disabled (pushf/cli/popf); the
    // host owns that exclusion.
#if ppz
    if (**cmd == '-') {
        (*cmd)++;
        if (ppz8_check(pmd)) {
            // :10204 the same 0410h/0409h negotiation as int_init
            pmd->ppz_call_seg = 1;
            if (pmd->ppz8->get_irq_level(pmd->user_data) ==
                    (uint8_t)(pmd->int_level + 8) &&
                pmd->ppz8->fm_int != NULL) {
                pmd->ppz_call_seg = 2;
            }
            pmd->ppz8->set_removal_protection(true, pmd->user_data); // 1901h
        } else {
            pmd->ppz_call_seg = 0;
        }
    } else {
        if (ppz8_check(pmd)) {
            pmd->ppz8->fifo_interrupt_stop(pmd->user_data); // 12h
            for (uint8_t chan = 0; chan < 8; chan++) {
                pmd->ppz8->keyoff(chan, pmd->user_data); // 0200h-0207h
            }
            pmd->ppz8->set_removal_protection(false, pmd->user_data); // 1900h
        }
        pmd->ppz_call_seg = 0; // :10193
    }
#else
    (void)pmd;
    if (**cmd == '-') (*cmd)++;
#endif
    return true;
}

// :10228
static bool waitclk_set(struct pmd *pmd, const char **cmd, bool from_env) {
    uint8_t n;
    if (!get_comline_number(cmd, &n)) {
        wait_check(pmd);
        return true;
    }
    if (n == 0) return usage(pmd, from_env);
    pmd->wait_clock = n;
    return true;
}

// :10245
static bool fftempo_set(struct pmd *pmd, const char **cmd, bool from_env) {
    uint8_t n;
    if (!get_comline_number(cmd, &n)) {
        pmd->ff_tempo = 250;
        return true;
    }
    if (n == 0) return usage(pmd, from_env);
    pmd->ff_tempo = n;
    return true;
}

// :10259
// /A. The original ignores the carry flag, so a missing number sets 0.
static bool adpcmwait_set(struct pmd *pmd, const char **cmd, bool from_env) {
    uint8_t n;
    (void)get_comline_number(cmd, &n);
    if (n >= 3) return usage(pmd, from_env);
    pmd->adpcm_wait = n;
    return true;
}

// :10269
static bool pcm86vol_set(struct pmd *pmd, const char **cmd, bool from_env) {
    (void)from_env;
    bool match = **cmd != '-';
    if (!match) (*cmd)++;
    pmd->pcm86_vol = match ? 1 : 0;
    pmd->_pcm86_vol = match ? 1 : 0;
    return true;
}

// :10284
static bool portsel_set(struct pmd *pmd, const char **cmd, bool from_env) {
    uint8_t n;
    if (!get_comline_number(cmd, &n)) return usage(pmd, from_env);
#if va
    // :10291 the VA variants accept the option but have a fixed port.
    (void)pmd;
#else
    if (n >= 4) return usage(pmd, from_env);
    pmd->port_sel = (int8_t)n;
    // :10299 re-probe the board on the selected port.
    fm_check(pmd);
#endif
    return true;
}

// :10304
static bool message_clear_set(struct pmd *pmd, const char **cmd, bool from_env) {
    (void)cmd;
    (void)from_env;
    pmd->message_flag = 0;
    return true;
}

// :10314
// Reads a decimal number through *cmd. On success the cursor stops on the first
// non-digit; otherwise it is left untouched. The value is 0 on failure, which
// is what the original returns in AL with the carry flag set. Digits wrap at
// 255 like the original's 8-bit BL.
static bool get_comline_number(const char **cmd, uint8_t *out) {
    uint8_t n = 0;
    bool found = false;
    for (;;) {
        if (!is_digit(**cmd)) break;
        n = (uint8_t)(n * 10 + *(*cmd)++ - '0');
        found = true;
    }
    *out = n;
    return found;
}

// :10350
// Prints `value` as a decimal number without leading zeros.
static void print_16(struct pmd *pmd, uint16_t value) {
    bool printed = false;
    p16_oneset(pmd, &value, 10000, &printed);
    p16_oneset(pmd, &value, 1000, &printed);
    p16_oneset(pmd, &value, 100, &printed);
    p16_oneset(pmd, &value, 10, &printed);
    print_char(pmd, (char)('0' + value));
}

// :10366
static void p16_oneset(struct pmd *pmd, uint16_t *value, uint16_t digit, bool *printed) {
    char c = '0';
    while (*value >= digit) {
        *value = (uint16_t)(*value - digit);
        c++;
    }
    if (*printed || c != '0') {
        print_char(pmd, c);
        *printed = true;
    }
}

// :10440
// PMDC_NORETURN rather than void: the option table holds bool handlers, and
// this one never returns to its caller.
PMDC_NORETURN static bool pmderr_5(struct pmd *pmd, const char **cmd, bool from_env) {
    (void)cmd;
    (void)from_env;
    pmderr_main(pmd, pmderror_mes5);
}

// :10444
PMDC_NORETURN static bool pmderr_6(struct pmd *pmd, const char **cmd, bool from_env) {
    (void)cmd;
    (void)from_env;
    pmderr_main(pmd, pmderror_mes6);
}

// :10452
PMDC_NORETURN static void pmderr_main(struct pmd *pmd, const char *mes) {
    print_mes(pmd, mes);
    error_exit(pmd, 1);
}

// :10462
// /H, and /HZ through the Z check.
static bool hz_usage(struct pmd *pmd, const char **cmd, bool from_env) {
    char c = to_upper(*(*cmd)++);
    if (c == 'Z') return hz_set(pmd, cmd, from_env);
    return usage(pmd, from_env);
}

// :10467
// Prints the usage text and exits with status 255. While parsing PMDOPT the bad
// option is reported instead and parsing is abandoned, so the command line
// still applies (:10469).
static bool usage(struct pmd *pmd, bool from_env) {
    if (from_env) {
        print_mes(pmd, mes_warning);
        print_mes(pmd, mes_kankyo);
        return false;
    }
    print_mes(pmd, mes_usage);
    error_exit(pmd, 255);
}

// :10485
// /HZ: downloads one of the HZDATA.INC frequency tables into the resident
// PCM86 table.
static bool hz_set(struct pmd *pmd, const char **cmd, bool from_env) {
    uint8_t n;
    if (!get_comline_number(cmd, &n)) return usage(pmd, from_env);
    if (n >= 9) return usage(pmd, from_env);
#if board2 && pcm
    pmd->p86_freq = n;
    // :10499 the auto setting (8) copies the resident table onto itself, which
    // leaves the current frequency table unchanged.
    if (n != 8) {
        memcpy(pmd->pcm_tune_data, hzdata[n], sizeof(pmd->pcm_tune_data));
    }
#endif
    return true;
}

// :10513
// WAIT.INC measures the CPU's `loop $` speed through the PIT, which is a
// DOS/PC-specific calibration and is intentionally not implemented: the delay
// loops (_wait, _waitP, _rwait) remain no-delay points. Both counts are 1
// rather than 0 because ADRAMCHK.ASM loops on wait_clock directly, and an 8086
// LOOP with CX=0 runs 65536 times. The VA build's hardcoded V30 estimate
// (:10514) is replaced by the same placeholder because no delay is performed.
// The original returns AX/CX; the port stores both fields directly.
static void wait_check(struct pmd *pmd) {
    pmd->wait_clock = 1;
    pmd->wait1_clock = 1;
}

#if board2 && adpcm && !ademu
// ADRAMCHK.ASM:11
// Checks the ADPCM RAM and sets pcm_gs_flag (0 = ADPCM available).
static void adpcm_ram_check(struct pmd *pmd) {
    uint8_t check_data[32];
    uint8_t wait = pmd->adpcm_wait;

    pmd->adpcm_wait = 2;
    memset(check_data, 0, sizeof(check_data));
    if (pcmstore(pmd, check_data)) {
        // ADRAMCHK.ASM:61 found_ymf288
        // ADRAMCHK.ASM:62 overwrites the "2608" in mes_ongen1 with "F288".
        memcpy(pmd->mes_ongen1 + 2, "F288", 4);
        pmd->pcm_gs_flag = 1;
        return;
    }

    // ADRAMCHK.ASM:29 The saved speed skips the faster settings.
    if (wait != 2 && wait != 1) {
        pmd->adpcm_wait = 0;
        pcmstore(pmd, (const uint8_t *)check_code);
        pcmread(pmd, check_data);
        if (pcmcompare(check_data)) {
            pmd->pcm_gs_flag = 0; // ADRAMCHK.ASM:57 the ADPCM RAM is present
            return;
        }
    }
    if (wait != 2) {
        pmd->adpcm_wait = 1;
        pcmstore(pmd, (const uint8_t *)check_code);
        pcmread(pmd, check_data);
        if (pcmcompare(check_data)) {
            pmd->pcm_gs_flag = 0; // ADRAMCHK.ASM:57 the ADPCM RAM is present
            return;
        }
    }
    pmd->adpcm_wait = 2;
    pcmstore(pmd, (const uint8_t *)check_code);
    pcmread(pmd, check_data);
    pmd->pcm_gs_flag = pcmcompare(check_data) ? 0 : 1; // ADRAMCHK.ASM:57, ADRAMCHK.ASM:64
}

// ADRAMCHK.ASM:72
// Sends 32 bytes of `data` to the ADPCM RAM through the 86 board. Returns true
// when the low-speed BRDY poll times out, which is how the original detects a
// YMF288 (ADRAMCHK.ASM:137).
static bool pcmstore(struct pmd *pmd, const uint8_t *data) {
    opnset46(pmd, 0x0001);
    opnset46(pmd, 0x1017); // all masked except BRDY
    opnset46(pmd, 0x1080);
    opnset46(pmd, 0x0060);
    opnset46(pmd, 0x0102); // x8
    opnset46(pmd, 0x0cff);
    opnset46(pmd, 0x0dff);
    opnset46(pmd, 0x02ff); // address 1fffh
    opnset46(pmd, 0x031f);
    opnset46(pmd, 0x04ff);
    opnset46(pmd, 0x05ff);

    uint16_t port1 = pmd->fm2_port1;
    uint16_t port2 = pmd->fm2_port2;
    unsigned count = 32;

    // The original's cli_sub/sti_sub (ADRAMCHK.ASM:252, ADRAMCHK.ASM:269) only
    // mask the PIC for the duration of the transfer; the host owns that
    // exclusion.
    if (pmd->adpcm_wait == 0) {
        // ADRAMCHK.ASM:210 high-speed
        (void)inb(pmd, port1);
        while (inb(pmd, port1) & 0x80) {
        }
        outb(pmd, port1, 8); // PCMDAT
        _waitP(pmd);
        for (; count != 0; count--) {
            outb(pmd, port2, *data++);
            while ((inb(pmd, port1) & 0x08) == 0) { // BRDY
            }
        }
    } else if (pmd->adpcm_wait == 1) {
        // ADRAMCHK.ASM:179 medium speed
        (void)inb(pmd, port1);
        while (inb(pmd, port1) & 0x80) {
        }
        outb(pmd, port1, 8); // PCMDAT
        for (; count != 0; count--) {
            _waitP(pmd);
            outb(pmd, port2, *data++);
            while ((inb(pmd, port1) & 0x08) == 0) { // BRDY
            }
        }
    } else {
        // ADRAMCHK.ASM:116 low speed
        for (; count != 0; count--) {
            while (inb(pmd, port1) & 0x80) {
            }
            outb(pmd, port1, 8); // PCMDAT
            _waitP(pmd);
            outb(pmd, port2, *data++);

            (void)inb(pmd, port1);
            unsigned polls = 0;
            while ((inb(pmd, port1) & 0x08) == 0) { // BRDY
                if (++polls == 10000) {
                    pcmst_exit(pmd);
                    return true;
                }
            }

            while (inb(pmd, port1) & 0x80) {
            }
            outb(pmd, port1, 0x10);
            _waitP(pmd);
            outb(pmd, port2, 0x80); // BRDY reset
        }
    }
    pcmst_exit(pmd);
    return false;
}

// ADRAMCHK.ASM:237
static void pcmst_exit(struct pmd *pmd) {
    opnset46(pmd, 0x1000);
    opnset46(pmd, 0x1080);
    opnset46(pmd, 0x0001);
}

// ADRAMCHK.ASM:284
// Reads 32 bytes of ADPCM RAM back into `data`.
static void pcmread(struct pmd *pmd, uint8_t *data) {
    opnset46(pmd, 0x0001);
    opnset46(pmd, 0x1000);
    opnset46(pmd, 0x1080);
    opnset46(pmd, 0x0020);
    opnset46(pmd, 0x0102); // x8
    opnset46(pmd, 0x0cff);
    opnset46(pmd, 0x0dff);
    opnset46(pmd, 0x02ff); // address 1fffh
    opnset46(pmd, 0x031f);
    opnset46(pmd, 0x04ff);
    opnset46(pmd, 0x05ff);

    pget(pmd);
    pget(pmd);

    uint16_t port1 = pmd->fm2_port1;
    for (unsigned count = 0; count < 32; count++) {
        while (inb(pmd, port1) & 0x80) {
        }
        outb(pmd, port1, 8);
        (void)inb(pmd, port1);
        while ((inb(pmd, port1) & 0x08) == 0) { // BRDY
        }
        (void)inb(pmd, port1);
        while (inb(pmd, port1) & 0x80) {
        }
        *data++ = inb(pmd, pmd->fm2_port2);
        opnset46(pmd, 0x1080);
    }
    opnset46(pmd, 0x0001);
}

// ADRAMCHK.ASM:354
// Reads one byte through the ADPCM data register. The original calls this
// twice after the transfer setup, discarding both reads.
static void pget(struct pmd *pmd) {
    while (inb(pmd, pmd->fm2_port1) & 0x80) {
    }
    outb(pmd, pmd->fm2_port1, 8);
    _waitP(pmd);
    (void)inb(pmd, pmd->fm2_port2);
    opnset46(pmd, 0x1080);
}

// ADRAMCHK.ASM:378
// Returns true when the read-back matches the test pattern.
static bool pcmcompare(const uint8_t *data) {
    return memcmp(check_code, data, sizeof(check_code) - 1) == 0;
}
#endif

// :8417
// The original's comstart prints the title and then either falls into
// resident_main or dispatches to change_main; the host's lifecycle decides
// which path runs, so each entry point prints the banner itself.
void pmd_resident_main(struct pmd *pmd, char *cmdline) {
    // :8390
    print_mes(pmd, mes_title);

    // :8481 The option defaults, the empty data area, the rhythm shot flags,
    // the part work areas, and the interrupt/effect state come from pmd_init.

    // :8517
    fm_check(pmd);

    // :8542
    char *env = pmd->sys->getenv("PMDOPT", pmd->user_data);
    if (env != NULL) {
        // A bad PMDOPT option prints a warning and abandons the environment
        // options; the command line still applies.
        (void)_set_option(pmd, resident_option, env);
    }

    // :8569
    if (*cmdline != '\0') {
        (void)set_option(pmd, resident_option, cmdline, false);

        // :8582 At least 1KB of music data and at most 40KB for the three
        // buffers, with the original's 8-bit carry checks.
        unsigned total = pmd->mmldat_lng;
        if (total < 1) pmderr_main(pmd, pmderror_mes2);
        total += pmd->voicedat_lng;
        if (total > 0xff) pmderr_main(pmd, pmderror_mes3);
        total += pmd->effecdat_lng;
        if (total > 0xff) pmderr_main(pmd, pmderror_mes3);
        if (total >= 41) pmderr_main(pmd, pmderror_mes3);

        // :8596 resmes_set writes the "PMD ver." marker into the PSP command
        // tail for the resident check; the host's lifecycle replaces that
        // check.
    }

    // :8604
    memchk_init(pmd);

    // :8639
    // The resident_size bookkeeping is folded into memchk_init; the host owns
    // the allocation.
    pmd->mmlbuf = pmd->dataarea + 1;
    pmd->tondat = pmd->dataarea + pmd->mmldat_lng * 1024;
    pmd->efcdat = pmd->tondat + pmd->voicedat_lng * 1024;

    if (pmd->board != 0) {
        // :8687
        int_init(pmd);

#if va
        // :8690 The original loads AX with the five timer values but opnset44
        // takes its argument from DX, which mask_eoi_set (:9049) left at the
        // master or slave PIC command port. The writes therefore repeat that
        // port word; setint writes the timer registers later anyway.
        uint16_t pic_cmd = (uint16_t)((pmd->int_level & 8) != 0 ? 0x0184 : 0x0188);
        for (unsigned i = 0; i < 5; i++) opnset44(pmd, pic_cmd);
#else
        // :8707
        opninit_loop(pmd);
#endif

        // :8752 The INT 5h vector save/install is omitted: the host owns the
        // interrupt source and installs the driver's handler here.
        pmd->hw->install_fm_timer_handler(opnint, pmd->user_data);

        // :8790
        opnint_start(pmd);
    }

    // :8773 The INT 60h vector save/install is omitted: the typed public
    // functions replace the register dispatcher.

    // :8800
    if (pmd->message_flag != 0) {
        print_mes(pmd, mes_wait1);
        print_16(pmd, pmd->wait_clock);
        print_mes(pmd, mes_wait2);
        print_16(pmd, (uint16_t)(pmd->wait_clock * pmd->wait1_clock));
        print_mes(pmd, mes_wait3);

        // :8814
        print_mes(pmd, mes_exit);
        if (pmd->key_check != 0) print_mes(pmd, mes_key);
    }

    // :8825-8833 Releasing the DOS environment block is host-owned.

    // :8385
    resident_exit_end(pmd);
}

// :9490
enum pmd_change_result pmd_change_main(struct pmd *pmd, char *cmdline) {
    // :8390
    print_mes(pmd, mes_title);

    if (*cmdline != '\0') {
        if (!set_option(pmd, status_option, cmdline, false)) {
            // resident_cut_main (/R) stops the parse after removing the
            // driver. The only other early stop is a PMDOPT usage error, which
            // cannot happen while parsing the command line.
            return PMD_CHANGE_REMOVED;
        }
        print_mes(pmd, changemes_0);
    }

    put_status(pmd);
    return PMD_CHANGE_OK;
}

// :9753
void pmd_resident_cut(struct pmd *pmd) {
    // :9755 A registered FM/effect callback forbids removal.
    if (pmd->rescut_cant != 0) {
        print_mes(pmd, cantcut_mes);
        error_exit(pmd, 1);
    }

    // :9768 The original's cli covers this whole sequence; the host owns that
    // exclusion, so removing the timer handler first is what makes it safe.
    // :9818 vector_ret tears the FM interrupt down only when a board was
    // found, the same condition resident_main installed the handler under.
    if (pmd->board != 0) {
        pmd->hw->remove_fm_timer_handler(pmd->user_data);
    }

    // :9769 With no PPSDRV resident, the INT 60h AH=18h call resets its flag;
    // with PPSDRV resident the original skips it.
    if (pmd->pps == NULL) pmd->ppsdrv_flag = 0;

    // :9777 The rest are the original's nested INT 60h calls: stop the FM
    // effect unless get_fmefc_num says none is playing, stop the SSG effect
    // when get_psgefcnum reports one, then stop the music.
    if ((uint8_t)(pmd->fm_effec_num + 1) != 0) pmd_fm_effect_off(pmd);
    if (pmd->effon != 0) pmd_effoff(pmd);
    (void)pmd_mstop_f(pmd);

    // :9813 vector_ret. The handler removal above is its INT 5h vector
    // restoration. The PIC mask restore, the INT 60h vector restoration, and
    // the DOS memory release are omitted; the resident copy of fm1_port1/2 it
    // reloads is the same struct in the port.
    if (pmd->board != 0) opnset44(pmd, 0x2730); // FM int reset

#if ppz
    // :9801 Release PPZ8's removal protection.
    if (pmd->ppz8 != NULL) {
        pmd->ppz8->set_removal_protection(false, pmd->user_data);
    }
#endif

    // :9806 The original exits here.
    print_mes(pmd, mes_cut);
}
