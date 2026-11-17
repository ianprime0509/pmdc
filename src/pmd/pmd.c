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
static_assert(PMD_MAX_PARTS >= max_part1,
              "PMD_MAX_PARTS must cover max_part1");

// :8275
enum {
    fm = 0,
    fm2 = 1,
    psg = 2,
    rhythm = 3,
};

#if lang == 256 * 'e' + 'n'
#include "strings_en.h"
#else
#include "strings_jp.h"
#endif

// Writes one character (DOS INT 21h/AH=2 in the original).
static void print_char(struct pmd *pmd, char c) {
    pmd->sys->putc(c, pmd->user_data);
}

// The register accesses below go through pmd_hw; these two wrappers only keep
// the transcription of the original's `in`/`out` sequences readable.
static uint8_t inb(struct pmd *pmd, uint16_t port) {
    return pmd->hw->inb(port, pmd->user_data);
}

static void outb(struct pmd *pmd, uint16_t port, uint8_t value) {
    pmd->hw->outb(port, value, pmd->user_data);
}

// :7925
// FM f-number table for one octave (C..B).
static PMDC_UNUSED const uint16_t fnum_data[12] = {
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
static PMDC_UNUSED const uint16_t psg_tune_data[12] = {
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
static PMDC_UNUSED const uint8_t carrier_table[16] = {
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


// :8014
// Rhythm data: PT, pan/volume, and the keyon bit for each rhythm instrument.
static PMDC_UNUSED const uint8_t rhydat[11][3] = {
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

#if board2 && pcm
// :10844
// Translated from HZDATA.INC: /HZ frequency tables 0-7 for the PCM86.
static PMDC_UNUSED const uint8_t hzdata[8][288] = {
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

// TUNEDATA.INC:9
// The default 16.54kHz PCM86 frequency table (96 notes).
static PMDC_UNUSED const uint8_t pcm_tune_data_default[288] = {
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

#if board2 && adpcm && !ademu
// ADRAMCHK.ASM:385
// The ADPCM RAM test pattern. The original's buffer is exactly 32 bytes with no
// terminator; the extra byte here is never sent.
static const char check_code[] = "*+=-PMD ADPCM RAM Check Data-=+*";
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

typedef enum pmd_cmd_result (*pmd_cmd_fn)(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);

// :151
PMDC_NORETURN static void error_exit(struct pmd *pmd, int status);
// :173
static void print_mes(struct pmd *pmd, const char *mes);
// :218
static void _wait(struct pmd *pmd);
// :223
static PMDC_UNUSED void _waitP(struct pmd *pmd);
// :230
static PMDC_UNUSED void _rwait(struct pmd *pmd);
// :242
static uint8_t rdychk(struct pmd *pmd, uint16_t port);
// C-only helpers shared by the sequencer.
//
// These have no labels of their own in the original. The line numbers mark the
// first lodsb/lodsw use and the FM3 check they abstract; the group stays at the
// top of the block because it has no single position in the source order.

// :1078 lodsb
// The sequencer's SI cursor.
static uint8_t lod8(uint8_t **si);
// :534 lodsw
static uint16_t lod16(uint8_t **si);
// :2528
// True when DI is FM3 and the chip is in effect mode.
static bool fm3_active(struct pmd *pmd, struct pmd_qq *di);
// :308
// Counts one INT 60h call and applies the reint_chk class of table entry
// `entry` (the original AH value). Returns false when the call is rejected, in
// which case the caller must not change state.
static bool int60_enter(struct pmd *pmd, uint8_t entry);
// :315
static void int60_exit(struct pmd *pmd);
#if board2
// PCMDRV.ASM:7, PCMDRVE.ASM:7, PCMDRV86.ASM:7
static void pcmmain(struct pmd *pmd, struct pmd_qq *di);
// PCMDRV.ASM:821, PCMDRV86.ASM:1347
static void pcm_effect(struct pmd *pmd, uint8_t al, uint16_t dx, uint8_t ch, uint8_t cl);
#endif
#if board2 && (pcm || adpcm)
// PCMDRV.ASM:185, PCMDRVE.ASM:173, PCMDRV86.ASM:180
static enum pmd_cmd_result commandsm(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// The PCM command table (PCMDRV.ASM:188, PCMDRV86.ASM:183).
static const pmd_cmd_fn cmdtblm[0xff - 0xb1 + 1];
// PCMDRV.ASM:291, PCMDRVE.ASM:279, PCMDRV86.ASM:281
static enum pmd_cmd_result pcm_mml_part_mask(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// PCMDRV.ASM:317, PCMDRVE.ASM:305, PCMDRV86.ASM:304
static enum pmd_cmd_result pcmrepeat_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// PCMDRV.ASM:355, PCMDRVE.ASM:359. PCMDRV86.ASM:246 has no portamento: its
// 0dah entry is jump1.
#if !pcm
static enum pmd_cmd_result portam(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
#endif
// PCMDRV.ASM:401, PCMDRVE.ASM:405, PCMDRV86.ASM:426
static enum pmd_cmd_result comvolupm(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// PCMDRV.ASM:411, PCMDRVE.ASM:415, PCMDRV86.ASM:436
static enum pmd_cmd_result comvolupm2(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// PCMDRV.ASM:418, PCMDRVE.ASM:422, PCMDRV86.ASM:444
static enum pmd_cmd_result comvoldownm(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// PCMDRV.ASM:425, PCMDRVE.ASM:429, PCMDRV86.ASM:451
static enum pmd_cmd_result comvoldownm2(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// PCMDRV.ASM:437, PCMDRVE.ASM:441, PCMDRV86.ASM:467
static enum pmd_cmd_result pansetm(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// PCMDRV.ASM:449, PCMDRVE.ASM:465, PCMDRV86.ASM:491
static enum pmd_cmd_result pansetm_ex(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// PCMDRV.ASM:467, PCMDRVE.ASM:483, PCMDRV86.ASM:531
static enum pmd_cmd_result com_at_m(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// PCMDRV.ASM:489, PCMDRVE.ASM:512, PCMDRV86.ASM:564
static void volsetm(struct pmd *pmd, struct pmd_qq *di);
// PCMDRV.ASM:606, PCMDRVE.ASM:630, PCMDRV86.ASM:707
static void keyonm(struct pmd *pmd, struct pmd_qq *di);
// PCMDRV.ASM:676, PCMDRVE.ASM:650, PCMDRV86.ASM:722
static void keyoffm(struct pmd *pmd, struct pmd_qq *di);
// PCMDRV.ASM:718, PCMDRVE.ASM:665, PCMDRV86.ASM:752
static void otodasim(struct pmd *pmd, struct pmd_qq *di);
// PCMDRV.ASM:771, PCMDRVE.ASM:711, PCMDRV86.ASM:817
static void fnumsetm(struct pmd *pmd, struct pmd_qq *di, uint8_t al);
#endif
#if board2 && pcm
// PCMDRV86.ASM:412
static uint16_t pcm86vol_chk(struct pmd *pmd, uint16_t ax);
// PCMDRV86.ASM:1162
static const uint8_t *get_data_offset(struct pmd *pmd);
// PCMDRV86.ASM:789
static void tone_set2(struct pmd *pmd, uint16_t ax, uint8_t bl);
// PCMDRV86.ASM:534
static void neiro_set(struct pmd *pmd, uint8_t al);
// PCMDRV86.ASM:671
static void pcm86_vol_out(struct pmd *pmd, uint8_t al);
// PCMDRV86.ASM:495
static void set_pcm_pan(struct pmd *pmd, struct pmd_qq *di, uint8_t al, uint8_t ah);
// PCMDRV86.ASM:498
static void set_pcm_pan2(struct pmd *pmd, uint8_t al, uint8_t ah);
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
static void ppzmain(struct pmd *pmd, struct pmd_qq *di);
// PPZDRV.ASM:185
static enum pmd_cmd_result commandsz(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// PPZDRV.ASM:188
static const pmd_cmd_fn cmdtblz[0xff - 0xb1 + 1];
// PPZDRV.ASM:287
static enum pmd_cmd_result ppz_extpartset(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// PPZDRV.ASM:320
static enum pmd_cmd_result ppz_mml_part_mask(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// PPZDRV.ASM:352
static enum pmd_cmd_result ppzrepeat_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// PPZDRV.ASM:392
static const struct pmd_ppz8_voice *ppz_voicetable_calc(struct pmd *pmd, struct pmd_qq *di);
// PPZDRV.ASM:415
static enum pmd_cmd_result portaz(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// PPZDRV.ASM:475
static enum pmd_cmd_result pansetz(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// PPZDRV.ASM:494
static enum pmd_cmd_result pansetz_ex(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// PPZDRV.ASM:514
static enum pmd_cmd_result com_at_z(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// PPZDRV.ASM:542
static void ppz_neiro_reset(struct pmd *pmd, struct pmd_qq *di);
// PPZDRV.ASM:571
static void volsetz(struct pmd *pmd, struct pmd_qq *di);
// PPZDRV.ASM:699
static void keyonz(struct pmd *pmd, struct pmd_qq *di);
// PPZDRV.ASM:720
static void keyoffz(struct pmd *pmd, struct pmd_qq *di);
// PPZDRV.ASM:735
static void otodasiz(struct pmd *pmd, struct pmd_qq *di);
// PPZDRV.ASM:801
static void fnumsetz(struct pmd *pmd, struct pmd_qq *di, uint8_t al);
#endif
#if board2 && (pcm || adpcm)
// The volume calculation shared by the PPZ8 and PCM volume setters
// (PPZDRV.ASM:578, PCMDRV.ASM:496).
static uint8_t pcm_vol_calc(struct pmd *pmd, struct pmd_qq *di, uint8_t voldown, bool square_fade);
#endif
// EFCDRV.ASM:9
// Takes the effect number in al. `di` is the part whose detune and volume the
// PPSDRV path corrects; eff_on passes NULL because its hosei_flag is 0.
static void effgo(struct pmd *pmd, struct pmd_qq *di, uint8_t al);
// EFCDRV.ASM:22
static void eff_on2(struct pmd *pmd, struct pmd_qq *di, uint8_t al);
// EFCDRV.ASM:25
static void eff_on(struct pmd *pmd, uint8_t al);
// EFCDRV.ASM:27
static void eff_main(struct pmd *pmd, struct pmd_qq *di, uint8_t al);
// EFCDRV.ASM:118
static void effplay(struct pmd *pmd);
// EFCDRV.ASM:125
static void efffor(struct pmd *pmd, const uint8_t **si);
// EFCDRV.ASM:178
static void efsnd(struct pmd *pmd, const uint8_t **si, uint8_t *dh, uint8_t *dl);
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
static PMDC_UNUSED void sel46(struct pmd *pmd);
// :1039
static PMDC_UNUSED void sel44(struct pmd *pmd);
#endif
// :1053
static uint8_t *fmmain(struct pmd *pmd, struct pmd_qq *di);
#if ppz
// :250
static void ppz_int_hook(struct pmd *pmd);
#endif
// :1306
static void rest_note(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :1204
static void mnp_ret(struct pmd *pmd, struct pmd_qq *di);
// :1215
static void calc_q(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :1318
static void fmmnp_4(struct pmd *pmd, struct pmd_qq *di);
// :1329
static void psgmain(struct pmd *pmd, struct pmd_qq *di);
// :1508
static bool ssgdrum_check(struct pmd *pmd, struct pmd_qq *di, uint8_t al);
// :1539
static void rhythmmain(struct pmd *pmd, struct pmd_qq *di);
// :1713
static enum pmd_cmd_result commands(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :1717
static enum pmd_cmd_result commandsr(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :1721
static enum pmd_cmd_result commandsp(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :1724
static enum pmd_cmd_result command00(struct pmd *pmd, struct pmd_qq *di, uint8_t **si, const pmd_cmd_fn *table);
// :1745
static const pmd_cmd_fn cmdtbl[0xff - 0xb1 + 1];
// :1843
static const pmd_cmd_fn cmdtblp[0xff - 0xb1 + 1];
// :1939
static const pmd_cmd_fn cmdtblr[0xff - 0xb1 + 1];
// :2035
static enum pmd_cmd_result jump16(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2037
static enum pmd_cmd_result jump6(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2039
static enum pmd_cmd_result jump5(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2041
static enum pmd_cmd_result jump4(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2043
static enum pmd_cmd_result jump3(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2045
static enum pmd_cmd_result jump2(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2047
static enum pmd_cmd_result jump1(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2049
static enum pmd_cmd_result jump0(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2055
static enum pmd_cmd_result special_0c0h(struct pmd *pmd, struct pmd_qq *di, uint8_t **si, uint8_t al);
// :2082
static enum pmd_cmd_result pmd86_s(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2091
static enum pmd_cmd_result vd_fm(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2092
static enum pmd_cmd_result vd_main(struct pmd *pmd, struct pmd_qq *di, uint8_t **si, uint8_t *vol, uint8_t *base, bool add);
// :2097
static enum pmd_cmd_result vd_ssg(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2100
static enum pmd_cmd_result vd_pcm(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2103
static enum pmd_cmd_result vd_rhythm(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2107
static enum pmd_cmd_result vd_ppz(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2111
static enum pmd_cmd_result _vd_fm(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2136
static enum pmd_cmd_result _vd_ssg(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2141
static enum pmd_cmd_result _vd_pcm(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2146
static enum pmd_cmd_result _vd_rhythm(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2151
static enum pmd_cmd_result _vd_ppz(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2159
static enum pmd_cmd_result slot_delay(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2176
static enum pmd_cmd_result fb_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2186
static void fb_set3(struct pmd *pmd, struct pmd_qq *di, uint8_t al);
// :2213
static void _fb_set(struct pmd *pmd, struct pmd_qq *di, uint8_t al);
// :2248
static enum pmd_cmd_result tl_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2357
static enum pmd_cmd_result fm_mml_part_mask(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2378
static enum pmd_cmd_result ssg_mml_part_mask(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2402
static enum pmd_cmd_result rhythm_mml_part_mask(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2417
static void neiro_reset(struct pmd *pmd, struct pmd_qq *di);
// :2478
static enum pmd_cmd_result pdrswitch(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2493
static enum pmd_cmd_result volmask_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2510
static enum pmd_cmd_result _volmask_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2528
static bool ch3_setting(struct pmd *pmd, struct pmd_qq *di);
// :2549
static enum pmd_cmd_result fm3_extpartset(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2576
static void fm3_partinit(struct pmd *pmd, struct pmd_qq *di, uint8_t *address);
// :2597
static enum pmd_cmd_result detune_extend(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2607
static enum pmd_cmd_result lfo_extend(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2618
static enum pmd_cmd_result envelope_extend(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2630
static enum pmd_cmd_result lfowave_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2638
static enum pmd_cmd_result extend_psgenvset(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2674
static enum pmd_cmd_result slotdetune_set2(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2704
static enum pmd_cmd_result slotdetune_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2745
static void ch3mode_set(struct pmd *pmd, struct pmd_qq *di);
// :2823
static void otodasi_cm(struct pmd *pmd, struct pmd_qq *di);
// :2839
static enum pmd_cmd_result slotmask_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2945
static void keyon_sm(struct pmd *pmd, struct pmd_qq *di);
// :2957
static enum pmd_cmd_result ssg_efct_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :2981
static enum pmd_cmd_result fm_efct_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3029
static enum pmd_cmd_result fade_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3037
static enum pmd_cmd_result mdepth_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3045
static enum pmd_cmd_result mdepth_count(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3067
static void porta_calc(struct pmd *pmd, struct pmd_qq *di);
// :3085
static enum pmd_cmd_result porta(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3155
static enum pmd_cmd_result portap(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3200
static enum pmd_cmd_result status_write(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3208
static enum pmd_cmd_result status_add(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3218
static enum pmd_cmd_result vol_one_up_fm(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3229
static enum pmd_cmd_result vol_one_up_psg(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3237
static PMDC_UNUSED enum pmd_cmd_result vol_one_up_pcm(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3248
static enum pmd_cmd_result vol_one_down(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3260
static enum pmd_cmd_result hlfo_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3304
static enum pmd_cmd_result hlfo_onoff(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3318
static enum pmd_cmd_result hlfo_delay(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3328
static enum pmd_cmd_result syousetu_lng_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3334
static enum pmd_cmd_result com_at(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3381
static enum pmd_cmd_result comq(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3386
static enum pmd_cmd_result comq3(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3390
static enum pmd_cmd_result comq4(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3398
static enum pmd_cmd_result comq2(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3406
static enum pmd_cmd_result comv(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3417
static enum pmd_cmd_result comt(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3480
static void calc_tb_tempo(struct pmd *pmd);
// :3502
static void calc_tempo_tb(struct pmd *pmd);
// :3524
static enum pmd_cmd_result comtie(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3531
static enum pmd_cmd_result comd(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3538
static enum pmd_cmd_result comdd(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3545
static enum pmd_cmd_result comstloop(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3561
static enum pmd_cmd_result comedloop(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3590
static enum pmd_cmd_result comexloop(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3613
static enum pmd_cmd_result comlopset(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3620
static enum pmd_cmd_result comshift(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3628
static enum pmd_cmd_result comshift2(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3637
static enum pmd_cmd_result comshift_master(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3646
static enum pmd_cmd_result comvolup(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3657
static enum pmd_cmd_result comvolup2(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3663
static enum pmd_cmd_result comvolupp(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3673
static enum pmd_cmd_result comvolupp2(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3682
static enum pmd_cmd_result comvoldown(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3690
static enum pmd_cmd_result comvoldown2(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3700
static enum pmd_cmd_result comvoldownp(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3708
static enum pmd_cmd_result comvoldownp2(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3720
static enum pmd_cmd_result _lfoset(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3733
static enum pmd_cmd_result _mdepth_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3737
static enum pmd_cmd_result _lfowave_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3741
static enum pmd_cmd_result _lfo_extend(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3745
static enum pmd_cmd_result _lfoset_delay(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3749
static enum pmd_cmd_result _lfoswitch(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3762
static enum pmd_cmd_result _lfoswitch_f(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3769
static void lfo_change(struct pmd_qq *di);
// :3813
static enum pmd_cmd_result lfoset(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3827
static enum pmd_cmd_result lfoset_delay(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3836
static enum pmd_cmd_result lfoswitch(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3847
static enum pmd_cmd_result lfoswitch_f(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3854
static enum pmd_cmd_result psgenvset(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3876
static enum pmd_cmd_result comy(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3885
static enum pmd_cmd_result psgnoise(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3889
static enum pmd_cmd_result psgnoise_move(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3906
static enum pmd_cmd_result psgsel(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3914
static enum pmd_cmd_result panset(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3917
static void panset_main(struct pmd *pmd, struct pmd_qq *di, uint8_t al);
// :3961
static PMDC_UNUSED void calc_panout(struct pmd *pmd, struct pmd_qq *di, uint8_t *dl);
// :3972
static enum pmd_cmd_result panset_ex(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :3994
static PMDC_UNUSED enum pmd_cmd_result rhykey(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :4045
static PMDC_UNUSED void rflag_inc(struct pmd *pmd, uint8_t dl, uint8_t *flags);
// :4064
static PMDC_UNUSED enum pmd_cmd_result rhyvs(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :4077
static PMDC_UNUSED uint8_t *rdat_adr(struct pmd *pmd, uint8_t index);
// :4064
static PMDC_UNUSED void rhyvs_write(struct pmd *pmd, uint8_t al, uint8_t dl);
// :4097
static PMDC_UNUSED enum pmd_cmd_result rhyvs_sft(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :4136
static PMDC_UNUSED enum pmd_cmd_result rpnset(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :4155
static PMDC_UNUSED enum pmd_cmd_result rmsvs(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :4166
static PMDC_UNUSED void volset2r(struct pmd *pmd, uint8_t dl);
// :4169
static PMDC_UNUSED void volset2rf(struct pmd *pmd, uint8_t dl);
// :4184
static PMDC_UNUSED enum pmd_cmd_result rmsvs_sft(struct pmd *pmd, struct pmd_qq *di, uint8_t **si);
// :4208
static uint8_t oshift(struct pmd_qq *di, uint8_t al);
// :4266
static void fnumset(struct pmd *pmd, struct pmd_qq *di, uint8_t al);
// :4302
static void fnumsetp(struct pmd *pmd, struct pmd_qq *di, uint8_t al);
// :4335
static void otodasi(struct pmd *pmd, struct pmd_qq *di);
// :4549
static void fm_block_calc(uint16_t *cx, uint16_t *ax);
// :4588
static void otodasip(struct pmd *pmd, struct pmd_qq *di);
// :4726
static void volset(struct pmd *pmd, struct pmd_qq *di);
// :4864
static void fmlfo_sub(uint8_t al, uint8_t bl, uint8_t *vol);
// :4890
static void volsetp(struct pmd *pmd, struct pmd_qq *di);
// :4998
static void keyon(struct pmd *pmd, struct pmd_qq *di);
// :5039
static void keyonp(struct pmd *pmd, struct pmd_qq *di);
// :5074
static void psgmsk(struct pmd *pmd, uint8_t *al, uint8_t *ah);
// :5091
static void keyoff(struct pmd *pmd, struct pmd_qq *di);
// :5095
static void keyoff_main(struct pmd *pmd, struct pmd_qq *di);
// :5125
static void keyoffp(struct pmd *pmd, struct pmd_qq *di);
// :5144
static void neiroset(struct pmd *pmd, struct pmd_qq *di, uint8_t dl);
// :5258
static void neiroset_tl(struct pmd_qq *di, const uint8_t *bx);
// :5274
static void toneadr_calc(struct pmd *pmd, struct pmd_qq *di, uint8_t dl, uint8_t **bx);
// :5307
static bool silence_fmpart(struct pmd *pmd, struct pmd_qq *di);
// :5341
static bool lfo(struct pmd *pmd, struct pmd_qq *di);
// :5375
static void lfo_main(struct pmd *pmd, struct pmd_qq *di);
// :5499
static void md_inc(struct pmd_qq *di);
// :5548
static uint16_t rnd(struct pmd *pmd, uint16_t ax);
// :5568
static uint8_t lfoinitp(struct pmd *pmd, struct pmd_qq *di, uint8_t al);
// :5645
static uint8_t lfoinit(struct pmd *pmd, struct pmd_qq *di, uint8_t al);
// :5661
static void lfo_exit_common(struct pmd *pmd, struct pmd_qq *di);
// :5684
static void lfin1(struct pmd *pmd, struct pmd_qq *di);
// :5737
static void lfoinit_main(struct pmd_qq *di);
// :5761
static bool soft_env(struct pmd *pmd, struct pmd_qq *di);
// :5778
static bool soft_env_main(struct pmd_qq *di);
// :5789
static void soft_env_sub(struct pmd_qq *di, uint8_t *dl);
// :5835
static bool ext_ssgenv_main(struct pmd_qq *di);
// :5849
static void esm_sub(struct pmd_qq *di, uint8_t *ah, uint8_t *dl);
// :5967
static void fadeout(struct pmd *pmd);
// :6005
static void setint(struct pmd *pmd);
// :6040
static void silence(struct pmd *pmd);
// :6173
static void opnset44(struct pmd *pmd, uint16_t dx);
#if board2
// :6200
static PMDC_UNUSED void opnset46(struct pmd *pmd, uint16_t dx);
#endif
// :6226
static void opnset(struct pmd *pmd, uint16_t dx);
// :6253
static uint8_t get07(struct pmd *pmd);
// :6592
static struct pmd_joy get_joy(struct pmd *pmd);
// :6741
static uint8_t *get_memo(struct pmd *pmd, uint8_t al);
// :6820
static uint8_t ff_music_main(struct pmd *pmd, uint16_t dx);
// :6902
static void maskon_all(struct pmd *pmd);
// :6935
static void maskoff_all(struct pmd *pmd);
// :6967
static void part_mask(struct pmd *pmd, uint8_t al);
// :7001
static void maskon_main(struct pmd *pmd, struct pmd_qq *di, uint8_t al, uint8_t ah);
// :7100
static void rhythm_mask(struct pmd *pmd);
// :7111
static void part_on(struct pmd *pmd, uint8_t ah);
// :7135
static void maskoff_main(struct pmd *pmd, struct pmd_qq *di, uint8_t al, uint8_t ah);
// :7354
static void fm_effect_on(struct pmd *pmd, uint8_t al);
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
// :7808
static uint8_t key_port8(uint8_t state);
// :7831
static void syousetu_count(struct pmd *pmd);
// :7844
static void settempo_b(struct pmd *pmd);
// :7852
static void stb_ff(struct pmd *pmd, uint8_t dl);
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
static void opnset_fmc(struct pmd *pmd, uint16_t dx, uint16_t ax);
#endif
// :9196
static bool port_check(struct pmd *pmd);
#if !va
// :9314
static bool check_86b(struct pmd *pmd);
#if !pcm
// :9406
static PMDC_UNUSED bool check_spb(struct pmd *pmd);
#endif
// :9468
static void set_port(struct pmd *pmd, uint16_t dx);
#endif
// :9511
static void put_status(struct pmd *pmd);
// :9598
static uint8_t sp_key_display(uint8_t key);
// :9715
static void put8(struct pmd *pmd, uint8_t al);
// :9726
static void p8_oneset(struct pmd *pmd, uint8_t *al, uint8_t dl, bool *printed);
// :9753
static bool resident_cut_main(struct pmd *pmd, const char **cmd, bool from_env);
// :9927
static bool set_option(struct pmd *pmd, const struct pmd_option *table, const char *cmd, bool from_env);
// :9928
static char lodsb(const char **cmd);
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
static void print_16(struct pmd *pmd, uint16_t ax);
// :10366
static void p16_oneset(struct pmd *pmd, uint16_t *ax, uint16_t bx, bool *printed);
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
    static_assert(lengthof(default_mes_int) <= lengthof(pmd->mes_int),
                  "mes_int is too small for default_mes_int");
    // :10578
    static_assert(lengthof(default_mes_ongen1) <= lengthof(pmd->mes_ongen1),
                  "mes_ongen1 is too small for default_mes_ongen1");

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

// :230
static void _rwait(struct pmd *pmd) {
    (void)pmd;
    // NOTE: WAIT.INC wait_clock calibration intentionally not implemented.
}

// :242
// Discards one status read, then waits for the BUSY bit to clear. The original
// leaves the final read in AL, which opnint's timer checks inspect; callers
// that write a register discard it.
static uint8_t rdychk(struct pmd *pmd, uint16_t port) {
    uint8_t al;
    (void)inb(pmd, port);
    while ((al = inb(pmd, port)) & 0x80) {
    }
    return al;
}

// C-only helpers shared by the sequencer.
//
// These have no labels of their own in the original. The line numbers mark the
// first lodsb/lodsw use and the FM3 check they abstract; the group stays at the
// top of the block because it has no single position in the source order.

// :1078 lodsb
// Reads through the sequencer's SI cursor.
static uint8_t lod8(uint8_t **si) { return *(*si)++; }

// :534 lodsw
static uint16_t lod16(uint8_t **si) {
    uint16_t value = (uint16_t)((*si)[0] | ((*si)[1] << 8));
    *si += 2;
    return value;
}

// :2528
// True when DI is the selected FM3 part (always the front chip).
static bool fm3_active(struct pmd *pmd, struct pmd_qq *di) {
    (void)di;
    if (pmd->partb != 3) return false;
#if board2
    return pmd->fmsel == 0;
#else
    return di != &pmd->part_e;
#endif
}

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
static void pcmmain(struct pmd *pmd, struct pmd_qq *di) {
    uint8_t *si = di->address;
    if (si == NULL) return;

    if (di->partmask != 0) goto pcmmain_nonplay;

    di->leng--;
    uint8_t al = di->leng;

    // :18 Keyoff when the remaining length reaches Q.
    if ((di->keyoff_flag & 3) == 0 && al <= di->qdat) {
        di->keyoff_flag = 0xff;
        keyoffm(pmd, di);
    }
    if (al != 0) goto mpexitm;

#if !pcm
    // PCMDRV.ASM:29 mp1m0, empty at PCMDRV86.ASM:29 (no portamento).
    di->lfoswi &= 0xf7; // portamento off
#endif

mp1m:
    al = lod8(&si);
    if (al < 0x80) goto mp2m;
    if (al == 0x80) goto mp15m;

    switch (commandsm(pmd, di, &si)) {
    case PMD_CMD_MP1M: goto mp1m;
    case PMD_CMD_PCMMNP_1: goto pcmmnp_1;
    case PMD_CMD_PORTA_RETURNM: goto porta_returnm;
    default: break;
    }
    goto mp1m;

mp15m:
    // :42 End of music, or a loop if "L" was seen.
    si--;
    di->address = si;
    di->loopcheck = 3;
    di->onkai = 0xff;
    if (di->partloop == NULL) goto mpexitm;
    si = di->partloop;
    di->loopcheck = 1;
    goto mp1m;

mp2m:
    // :55 Note: set the pitch and gate.
    al = lfoinitp(pmd, di, al);
    al = oshift(di, al);
    fnumsetm(pmd, di, al);

    al = lod8(&si);
    di->leng = al;
    calc_q(pmd, di, &si);

porta_returnm:
    // :64
    if (di->volpush == 0) goto mp_newm;
    if (di->onkai == 0xff) goto mp_newm;
    pmd->volpush_flag--;
    if (pmd->volpush_flag == 0) goto mp_newm;
    pmd->volpush_flag = 0;
    di->volpush = 0;

mp_newm:
    volsetm(pmd, di);
    otodasim(pmd, di);
    if ((di->keyoff_flag & 1) != 0) keyonm(pmd, di);
    di->keyon_flag++;

    di->address = si;
    pmd->tieflag = 0;
    pmd->volpush_flag = 0;
    di->keyoff_flag = 0;
    if (*si == 0xfb) di->keyoff_flag = 2; // '&' immediately follows
    mnp_ret(pmd, di);
    return;

mpexitm:;
#if pcm
    // :88
    uint8_t cl = di->lfoswi;
    if ((cl & 0x22) != 0) {
        pmd->lfo_switch = 0;
        if ((cl & 2) != 0) {
            (void)lfo(pmd, di);
            pmd->lfo_switch = (uint8_t)(cl & 2);
        }
        if ((cl & 0x20) != 0) {
            lfo_change(di);
            if (lfo(pmd, di)) {
                lfo_change(di);
                pmd->lfo_switch |= di->lfoswi & 0x20;
            } else {
                lfo_change(di);
            }
        }
        if (!soft_env(pmd, di) && (pmd->lfo_switch & 0x22) == 0 &&
            pmd->fadeout_speed == 0) {
            mnp_ret(pmd, di);
            return;
        }
        volsetm(pmd, di);
        mnp_ret(pmd, di);
        return;
    }
    if (!soft_env(pmd, di)) {
        if (pmd->fadeout_speed == 0) {
            mnp_ret(pmd, di);
            return;
        }
    }
    volsetm(pmd, di);
    mnp_ret(pmd, di);
    return;
#else
    // :89 The YM2608 drivers process the tone and volume LFOs and portamento.
    uint8_t cl = di->lfoswi;
    pmd->lfo_switch = (uint8_t)(cl & 8);
    if (cl != 0) {
        if ((cl & 3) != 0) {
            if (lfo(pmd, di)) pmd->lfo_switch |= cl & 3;
        }
        if ((cl & 0x30) != 0) {
            lfo_change(di);
            if (lfo(pmd, di)) {
                lfo_change(di);
                pmd->lfo_switch |= di->lfoswi & 0x30;
            } else {
                lfo_change(di);
            }
        }
        if ((pmd->lfo_switch & 0x19) != 0) {
            if ((pmd->lfo_switch & 8) != 0) porta_calc(pmd, di);
            otodasim(pmd, di);
        }
    }
    // :129 volsm
    if (!soft_env(pmd, di) && (pmd->lfo_switch & 0x22) == 0 &&
        pmd->fadeout_speed == 0) {
        mnp_ret(pmd, di);
        return;
    }
    volsetm(pmd, di);
    mnp_ret(pmd, di);
    return;
#endif

pcmmain_nonplay:;
#if pcm
    // :134
    di->leng--;
    if (di->leng != 0) {
        mnp_ret(pmd, di);
        return;
    }
    if ((di->partmask & 2) != 0) {
        if (pmd->play86_flag == 1) goto pcmmnp_1; // the PCM effect is sounding
        pmd->pcmflag = 0;
        pmd->pcm_effec_num = 0xff;
        di->partmask &= 0xfd;
        if (di->partmask != 0) goto pcmmnp_1;
        neiro_set(pmd, di->voicenum);
        set_pcm_pan(pmd, di, di->fmpan, pmd->revpan);
        goto mp1m; // the part is revived
    }
#elif ademu
    // :142
    di->keyoff_flag = 0xff;
    di->leng--;
    if (di->leng != 0) {
        mnp_ret(pmd, di);
        return;
    }
#else
    // :142
    di->keyoff_flag = 0xff;
    di->leng--;
    if (di->leng != 0) {
        mnp_ret(pmd, di);
        return;
    }
    if ((di->partmask & 2) != 0) {
        // :149 EOS check: the interrupt PCM has finished when bit 2 of the
        // FM2 data port is set.
        uint8_t eos = inb(pmd, pmd->fm2_port1);
        if ((eos & 0x04) == 0) goto pcmmnp_1; // still sounding
        pmd->pcmflag = 0;
        pmd->pcm_effec_num = 0xff;
        di->partmask &= 0xfd;
        if (di->partmask != 0) goto pcmmnp_1;
        // PCMDRV.ASM:316 revives at mp1m, past mp1m0's portamento clear.
        goto mp1m; // the part is revived
    }
#endif

pcmmnp_1:
    al = lod8(&si);
    if (al == 0x80) goto pcmmnp_2;
    if (al < 0x80) goto pcmmnp_3;
    switch (commandsm(pmd, di, &si)) {
    case PMD_CMD_MP1M: goto mp1m;
    case PMD_CMD_PCMMNP_1: goto pcmmnp_1;
    case PMD_CMD_PORTA_RETURNM: goto porta_returnm;
    default: break;
    }
    goto pcmmnp_1;

pcmmnp_2:
    // :167 End of music, or a loop if "L" was seen.
    si--;
    di->address = si;
    di->loopcheck = 3;
    di->onkai = 0xff;
    if (di->partloop == NULL) {
        fmmnp_4(pmd, di);
        return;
    }
    si = di->partloop;
    di->loopcheck = 1;
    goto pcmmnp_1;

pcmmnp_3:
    // :158 A rest goes through the shared rest-note path.
    rest_note(pmd, di, &si);
}
#endif

#if board2 && (pcm || adpcm)
// PCMDRV.ASM:185, PCMDRVE.ASM:173, PCMDRV86.ASM:180
static enum pmd_cmd_result commandsm(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    return command00(pmd, di, si, cmdtblm);
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
static enum pmd_cmd_result pcm_mml_part_mask(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint8_t al = lod8(si);
    if (al >= 2) return special_0c0h(pmd, di, si, al);
    if (al == 0) {
        di->partmask &= 0xbf;
        if (di->partmask != 0) return PMD_CMD_PCMMNP_1;
        return PMD_CMD_MP1M; // the part is revived
    }

    di->partmask |= 0x40;
    if (di->partmask != 0x40) return PMD_CMD_PCMMNP_1;
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
static enum pmd_cmd_result pcmrepeat_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)di;
#if pcm
    // :304 `whole` is the original voice size (the original pushes DX:CX) and
    // `running` the working copy it adjusts while reading the parameters.
    uint32_t whole = ((uint32_t)pmd->_size2 << 16) | pmd->_size1;
    uint32_t running = whole;
    uint32_t size = whole;
    uint32_t repeat_size = whole;
    uint32_t repeat_ofs = ((uint32_t)pmd->_start_ofs2 << 16) | pmd->_start_ofs;
    pmd->repeat_flag = 1;
    pmd->release_flag1 = 0;

    // :320 The first value is the repeat start.
    uint16_t ax = lod16(si);
    if ((int16_t)ax < 0) {
        ax = pcm86vol_chk(pmd, (uint16_t)(0u - ax));
        repeat_size = ax;
        running = (uint32_t)(whole - ax);
        repeat_ofs += running;
    } else {
        ax = pcm86vol_chk(pmd, ax);
        repeat_size = (uint32_t)(whole - ax);
        repeat_ofs += ax;
    }

    // :345 The second value is the repeat end.
    ax = lod16(si);
    if (ax != 0) {
        if ((int16_t)ax < 0) {
            ax = pcm86vol_chk(pmd, (uint16_t)(0u - ax));
            repeat_size = (uint32_t)(repeat_size - ax);
            size = (uint32_t)(size - ax);
        } else {
            ax = pcm86vol_chk(pmd, ax);
            size = ax;
            running = (uint32_t)(running - ax);
            repeat_size = (uint32_t)(repeat_size - running);
        }
    }
    pmd->repeat_size1 = (uint16_t)repeat_size;
    pmd->repeat_size2 = (uint16_t)(repeat_size >> 16);
    pmd->repeat_ofs = (uint16_t)repeat_ofs;
    pmd->repeat_ofs2 = (uint16_t)(repeat_ofs >> 16);
    pmd->_size1 = (uint16_t)size;
    pmd->_size2 = (uint16_t)(size >> 16);

    // :371 The third value is the release start; the original restores the
    // pushed whole size for it.
    ax = lod16(si);
    if (ax == 0x8000) return PMD_CMD_RETURN; // no release
    uint32_t release_size = whole;
    uint32_t release_ofs = ((uint32_t)pmd->_start_ofs2 << 16) | pmd->_start_ofs;
    pmd->release_flag1 = 1;
    if (ax >= 0x8000) {
        ax = pcm86vol_chk(pmd, (uint16_t)(0u - ax));
        release_size = ax;
        running = (uint32_t)(whole - ax);
        release_ofs += running;
    } else {
        ax = pcm86vol_chk(pmd, ax);
        release_size = (uint32_t)(release_size - ax);
        release_ofs += ax;
    }
    pmd->release_size1 = (uint16_t)release_size;
    pmd->release_size2 = (uint16_t)(release_size >> 16);
    pmd->release_ofs = (uint16_t)release_ofs;
    pmd->release_ofs2 = (uint16_t)(release_ofs >> 16);
#elif ademu
    // PCMDRVE.ASM:305 The loop offsets are in 1/64ths of the voice's size.
    uint16_t size = 0;
    di->voicenum |= 0x80;
    {
        const struct pmd_ppz8_voice *voice = ppz_voicetable_calc(pmd, di);
        di->voicenum &= 0x7f;
        if (voice != NULL) size = (uint16_t)(voice->size >> 6);
    }

    uint16_t ax = lod16(si);
    uint16_t start = (int16_t)ax < 0 ? (uint16_t)(ax + size) : ax;
    ax = lod16(si);
    uint16_t end = (ax == 0 || (int16_t)ax < 0) ? (uint16_t)(ax + size) : ax;
    (void)lod16(si); // the release value cannot be set

    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    if (drv != NULL) {
        drv->set_loop(7, (uint32_t)start * 32, (uint32_t)end * 32, pmd->user_data);
    }
#else
    // PCMDRV.ASM:317 The offsets are relative to the voice's start or stop.
    uint16_t ax = lod16(si);
    if ((int16_t)ax < 0) ax = (uint16_t)(ax + pmd->pcmstop);
    else ax = (uint16_t)(ax + pmd->pcmstart);
    pmd->pcmrepeat1 = ax;

    ax = lod16(si);
    if (ax == 0 || (int16_t)ax < 0) ax = (uint16_t)(ax + pmd->pcmstop);
    else ax = (uint16_t)(ax + pmd->pcmstart);
    pmd->pcmrepeat2 = ax;

    ax = lod16(si);
    // :371 A third value of 8000h disables the release but is still stored.
    if (ax != 0x8000) {
        if ((int16_t)ax < 0) ax = (uint16_t)(ax + pmd->pcmstop);
        else ax = (uint16_t)(ax + pmd->pcmstart);
    }
    pmd->pcmrelease = ax;
#endif
    return PMD_CMD_RETURN;
}

// PCMDRV.ASM:355, PCMDRVE.ASM:359
// Portamento for the PCM parts.
#if !pcm
static enum pmd_cmd_result portam(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    if (di->partmask != 0) {
        (*si)++; // the first pitch byte is skipped while masked
        return PMD_CMD_RETURN;
    }

    uint8_t al = lod8(si);
    al = lfoinitp(pmd, di, al);
    al = oshift(di, al);
    fnumsetm(pmd, di, al);

    uint16_t fnum = di->fnum;
    uint8_t onkai = di->onkai;

    al = lod8(si);
    al = oshift(di, al);
    fnumsetm(pmd, di, al);
    uint16_t ax = di->fnum;

    di->onkai = onkai;
    di->fnum = fnum;
    ax = (uint16_t)(ax - fnum);

    di->leng = lod8(si);
    calc_q(pmd, di, si);

    int16_t n = (int16_t)ax;
    di->porta_num2 = (int16_t)(n / (int16_t)di->leng);
    di->porta_num3 = (int16_t)(n % (int16_t)di->leng);
    di->lfoswi |= 8; // portamento on
    return PMD_CMD_PORTA_RETURNM;
}
#endif

// PCMDRV.ASM:401, PCMDRVE.ASM:405, PCMDRV86.ASM:426
static enum pmd_cmd_result comvolupm(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    (void)si;
    unsigned sum = di->volume + 16;
    di->volume = sum > 0xff ? 255 : (uint8_t)sum;
    return PMD_CMD_RETURN;
}

// PCMDRV.ASM:411, PCMDRVE.ASM:415, PCMDRV86.ASM:436
static enum pmd_cmd_result comvolupm2(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    unsigned sum = lod8(si) + di->volume;
    di->volume = sum > 0xff ? 255 : (uint8_t)sum;
    return PMD_CMD_RETURN;
}

// PCMDRV.ASM:418, PCMDRVE.ASM:422, PCMDRV86.ASM:444
static enum pmd_cmd_result comvoldownm(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    (void)si;
    di->volume = di->volume < 16 ? 0 : (uint8_t)(di->volume - 16);
    return PMD_CMD_RETURN;
}

// PCMDRV.ASM:425, PCMDRVE.ASM:429, PCMDRV86.ASM:451
static enum pmd_cmd_result comvoldownm2(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    uint8_t al = lod8(si);
    di->volume = di->volume < al ? 0 : (uint8_t)(di->volume - al);
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
static enum pmd_cmd_result pansetm(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint8_t al = lod8(si);
#if pcm
    // :467 p0 reversed phase, p1 right, p2 left, p3 centre.
    uint8_t pan = 0;
    uint8_t rev = 0;
    if (al == 1) {
        pan = 0x7f;
    } else if (al == 2) {
        pan = 0x80;
    } else if (al != 3) {
        rev = 1;
    }
    set_pcm_pan(pmd, di, pan, rev);
#elif ademu
    // :441 PCMDRVE.ASM:447 masks the value, then rotates it into bits 6-7.
    al &= 3;
    di->fmpan = (uint8_t)(al << 6);
    // The pan value goes straight to PPZ8 unless the ADPCM emulation already
    // took channel 7.
    uint8_t dl = al;
    if (pmd->adpcm_emulate != 1) dl = pcm_pan_data[al];
    {
        const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
        if (drv != NULL) drv->set_pan(7, dl, pmd->user_data);
    }
#else
    // PCMDRV.ASM:441 rotates first, so the mask keeps bits 6-7 only.
    al = (uint8_t)(((al << 6) | (al >> 2)) & 0xc0);
    di->fmpan = al;
    (void)pmd;
#endif
    return PMD_CMD_RETURN;
}

// PCMDRV.ASM:449, PCMDRVE.ASM:465, PCMDRV86.ASM:491
// Pan setting extend: px -127..+127 plus the reversed-phase flag (0c3h).
static enum pmd_cmd_result pansetm_ex(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint8_t al = lod8(si);
#if pcm
    set_pcm_pan(pmd, di, al, *(*si)++);
#else
    (*si)++; // the reversed-phase flag is skipped
    // :449 The extended value is reduced to left, right, or centre.
    if (al == 0) al = 3;
    else if ((int8_t)al < 0) al = 1;
    else al = 2;
    al &= 3;
    di->fmpan = (uint8_t)(al << 6);
#if ademu
    uint8_t dl = al;
    if (pmd->adpcm_emulate != 1) dl = pcm_pan_data[al];
    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    if (drv != NULL) drv->set_pan(7, dl, pmd->user_data);
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
static void set_pcm_pan(struct pmd *pmd, struct pmd_qq *di, uint8_t al, uint8_t ah) {
    di->fmpan = al;
    pmd->revpan = ah;
    set_pcm_pan2(pmd, al, ah);
}

// PCMDRV86.ASM:498
// The balance half of set_pcm_pan, which the PCM effect also uses directly.
static void set_pcm_pan2(struct pmd *pmd, uint8_t al, uint8_t ah) {
    if ((int8_t)al < 0) {
        pmd->pcm86_pan_flag = 1; // left
        al = (uint8_t)((al + 0x80) & 0x7f);
    } else if (al == 0) {
        pmd->pcm86_pan_flag = 3; // middle
    } else {
        pmd->pcm86_pan_flag = 2; // right
        al = (uint8_t)(~al & 0x7f);
    }
    pmd->pcm86_pan_dat = al;
    if ((ah & 1) != 0) pmd->pcm86_pan_flag |= 4; // reversed phase
}
#endif

// PCMDRV.ASM:467, PCMDRVE.ASM:483, PCMDRV86.ASM:531
// Voice change for the PCM parts.
static enum pmd_cmd_result com_at_m(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint8_t al = lod8(si);
#if pcm
    di->voicenum = al;
    neiro_set(pmd, al);
#elif ademu
    di->voicenum = al;
    if (pmd->adpcm_emulate == 1) return PMD_CMD_RETURN;
    pmd->adpcm_emulate = 1;
    {
        const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
        if (drv != NULL) drv->set_adpcm_emulation(true, pmd->user_data);
    }
#if ppz
    di->voicenum |= 0x80;
    pmd->partb = 7; // the PCM part's PPZ8 channel, left as-is by the original
    ppz_neiro_reset(pmd, di);
    di->voicenum &= 0x7f;

    pmd->parts[part10h].partmask |= 0x10;
    di->partmask &= 0xef;
    if (di->partmask == 0) return PMD_CMD_MP1M; // the part is revived
#endif
#else
    di->voicenum = al;
    {
        const struct pmd_pcm_adpcm_voice *v = &pmd->pcm_table.adpcm_data.pcmadrs[al];
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
static void volsetm(struct pmd *pmd, struct pmd_qq *di) {
#if pcm
    uint8_t al = pcm_vol_calc(pmd, di, pmd->pcm_voldown, false);
    pcm86_vol_out(pmd, al);
#elif ademu
    // PCMDRVE.ASM:604 squares the fadeout like PCMDRV.ASM:514.
    uint8_t al = pcm_vol_calc(pmd, di, pmd->pcm_voldown, true);
    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    if (drv != NULL) drv->set_volume(7, al, pmd->user_data);
#else
    // :596 The YM2608 ADPCM volume write, with :514's squared fadeout.
    uint8_t al = pcm_vol_calc(pmd, di, pmd->pcm_voldown, true);
    opnset46(pmd, (uint16_t)((0x0b << 8) | al));
#endif
}

// PCMDRV.ASM:606, PCMDRVE.ASM:630, PCMDRV86.ASM:707
static void keyonm(struct pmd *pmd, struct pmd_qq *di) {
    if (di->onkai == 0xff) return; // a rest
#if pcm
    play_86pcm(pmd);
#elif ademu
    // PCMDRVE.ASM:640 Voice with bit 15 set (the PPZ8 bank select).
    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    if (drv != NULL) {
        drv->keyon(7, (uint16_t)(0x8080 | di->voicenum), pmd->user_data);
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
        opnset46(pmd, (uint16_t)((0x01 << 8) | (uint8_t)(di->fmpan | 2)));
        return;
    }

    opnset46(pmd, 0x00b0); // repeat
    opnset46(pmd, (uint16_t)((0x01 << 8) | (uint8_t)(di->fmpan | 2)));
    opnset46(pmd, (uint16_t)((0x02 << 8) | (uint8_t)pmd->pcmrepeat1));
    opnset46(pmd, (uint16_t)((0x03 << 8) | (uint8_t)(pmd->pcmrepeat1 >> 8)));
    opnset46(pmd, (uint16_t)((0x04 << 8) | (uint8_t)pmd->pcmrepeat2));
    opnset46(pmd, (uint16_t)((0x05 << 8) | (uint8_t)(pmd->pcmrepeat2 >> 8)));
#endif
}

// PCMDRV.ASM:676, PCMDRVE.ASM:650, PCMDRV86.ASM:722
static void keyoffm(struct pmd *pmd, struct pmd_qq *di) {
#if pcm
    if (pmd->release_flag1 == 1) {
        // :723 The release range replaces the playback range.
        pmd->start_ofs = pmd->release_ofs;
        pmd->start_ofs2 = pmd->release_ofs2;
        pmd->size1 = pmd->release_size1;
        pmd->size2 = pmd->release_size2;
        pmd->release_flag2 = 1;
    }
#endif
    if (di->envf == 0xff) {
        if (di->eenv_count == 4) return;
    } else if (di->envf == 2) {
        return;
    }
#if pcm || ademu
    keyoffp(pmd, di);
#else
    // :687 PCMDRV.ASM: an explicit release address restarts the ADPCM at it.
    if (pmd->pcmrelease == 0x8000) {
        keyoffp(pmd, di);
        return;
    }
    opnset46(pmd, 0x0021); // PCM reset
    opnset46(pmd, (uint16_t)((0x02 << 8) | (uint8_t)pmd->pcmrelease));
    opnset46(pmd, (uint16_t)((0x03 << 8) | (uint8_t)(pmd->pcmrelease >> 8)));
    opnset46(pmd, (uint16_t)((0x04 << 8) | (uint8_t)pmd->pcmstop));
    opnset46(pmd, (uint16_t)((0x05 << 8) | (uint8_t)(pmd->pcmstop >> 8)));
    opnset46(pmd, 0x00a0); // play once
    keyoffp(pmd, di);
#endif
}

// PCMDRV.ASM:718, PCMDRVE.ASM:665, PCMDRV86.ASM:752
// Sets the PCM pitch from the fnum, portamento, LFO, and detune.
static void otodasim(struct pmd *pmd, struct pmd_qq *di) {
    if (di->fnum == 0) return;
#if pcm
    // :752 The PCM86 keeps the fractional pitch in fnum2 and scales the
    // detune by it.
    uint16_t ax = di->fnum2;
    uint16_t bx = di->fnum;
    if (pmd->pcm86_vol != 1 && di->detune != 0) {
        uint16_t cx = ax;
        uint16_t dx = bx;
        for (int i = 0; i < 5; i++) {
            dx >>= 1;
            cx = (uint16_t)((cx >> 1) | ((dx & 1) << 15));
        }
        int16_t det = di->detune;
        if (det < 0) {
            uint16_t sum = (uint16_t)(cx + (uint16_t)det);
            if (sum == 0) {
                cx = 1;
            } else if ((unsigned)cx >= (unsigned)(-(int)det)) {
                cx = sum;
            } else {
                cx = 1; // the result must not underflow to zero
            }
        } else {
            uint16_t sum = (uint16_t)(cx + (uint16_t)det);
            cx = sum < cx ? 0xffff : sum;
        }
        uint16_t high = 0;
        for (int i = 0; i < 5; i++) {
            high = (uint16_t)((high << 1) | (cx >> 15));
            cx = (uint16_t)(cx << 1);
        }
        bx = (uint16_t)((bx & 0xffe0) | high);
        ax = (uint16_t)((ax & 0x001f) | cx);
    }
    tone_set2(pmd, ax, (uint8_t)bx);
#else
    uint16_t bx = di->fnum;
    bx = (uint16_t)(bx + di->porta_num);

    int16_t dx = 0;
    if ((di->lfoswi & 0x11) != 0) {
        if ((di->lfoswi & 1) != 0) dx = di->lfodat;
        if ((di->lfoswi & 0x10) != 0) dx = (int16_t)(dx + di->_lfodat);
        // PCM LFO depth is applied four times (PCMDRV.ASM:740).
        dx = (int16_t)(dx + dx);
        dx = (int16_t)(dx + dx);
    }
    dx = (int16_t)(dx + di->detune);

    if (dx < 0) {
        // :754 A borrow clears the tone; the original branches on the carry of
        // the 16-bit add.
        unsigned sum = (unsigned)bx + (unsigned)(uint16_t)dx;
        bx = sum > 0xffff ? (uint16_t)sum : 0;
    } else {
        unsigned sum = (unsigned)bx + (unsigned)dx;
        bx = sum > 0xffff ? 0xffff : (uint16_t)sum;
    }
#if ademu
    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    if (drv != NULL) drv->set_tone(7, bx, pmd->user_data);
#else
    opnset46(pmd, (uint16_t)((0x09 << 8) | (uint8_t)bx));
    opnset46(pmd, (uint16_t)((0x0a << 8) | (uint8_t)(bx >> 8)));
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
// Sets the ADPCM pitch from the note in al.
static void fnumsetm(struct pmd *pmd, struct pmd_qq *di, uint8_t al) {
    (void)pmd;
    uint8_t ah = al & 0x0f;
    if (ah == 0x0f) goto fnrest_m; // a rest
    di->onkai = al;

    uint8_t cl = (uint8_t)(al >> 4);
    uint8_t ch = cl;
    uint8_t shift = cl <= 5 ? (uint8_t)(5 - cl) : 0;

    uint16_t ax = pcm_adpcm_tune_data[ah];
    if (ch >= 6) {
        // o7 and above: double the entry when it does not overflow.
        uint8_t octave = 0x50;
        if ((int16_t)ax >= 0) {
            ax = (uint16_t)(ax << 1);
            octave = 0x60;
        }
        di->onkai = (uint8_t)((di->onkai & 0x0f) | octave);
    } else {
        ax = (uint16_t)(ax >> shift);
    }
    di->fnum = ax;
    return;

fnrest_m:
    di->onkai = 0xff;
    if ((di->lfoswi & 0x11) != 0) return;
    di->fnum = 0;
}
#else
// PCMDRV86.ASM:817
// Sets the PCM86 pitch from the note in al: fnum holds the integer part and
// fnum2 the fraction, both taken from the frequency table.
static void fnumsetm(struct pmd *pmd, struct pmd_qq *di, uint8_t al) {
    uint8_t ah = al & 0x0f;
    if (ah == 0x0f) goto fnrest_m; // a rest

    if (pmd->pcm86_vol == 1 && al >= 0x65) {
        // :823 /S plays everything from 65h up one octave lower.
        al = ah < 5 ? 0x60 : 0x50;
        al |= ah;
    }

    di->onkai = al;
    uint8_t bl = (uint8_t)((al & 0xf0) >> 1);
    bl = (uint8_t)(bl + ((al & 0xf0) >> 2) + ah);
    const uint8_t *entry = pmd->p86_freqtable + (unsigned)bl * 3;
    di->fnum = (uint16_t)(0xff00 | entry[0]);
    di->fnum2 = (uint16_t)(entry[1] | (entry[2] << 8));
    return;

fnrest_m:
    di->onkai = 0xff;
    if ((di->lfoswi & 0x11) != 0) return;
    // PMD.ASM:4296 fnrest clears fnum only; fnum2 keeps its old value.
    di->fnum = 0;
}
#endif

// PPZDRV.ASM:578, PCMDRV.ASM:496
// The volume correction chain shared by the PPZ8 and PCM volume setters:
// voldown, fadeout, envelope, and volume LFO. `square_fade` selects the YM2608
// ADPCM drivers (PCMDRV.ASM:514, PCMDRVE.ASM:604), which square the fade before
// applying it.
static uint8_t pcm_vol_calc(struct pmd *pmd, struct pmd_qq *di, uint8_t voldown, bool square_fade) {
    uint8_t al = di->volpush;
    if (al == 0) al = di->volume;
    uint8_t dl = al;

    if (voldown != 0) {
        dl = (uint8_t)(((uint16_t)dl * (uint8_t)(-voldown)) >> 8);
    }

    uint8_t fade = pmd->fadeout_volume;
    if (fade != 0) {
        uint8_t neg = (uint8_t)(-fade);
        if (square_fade) neg = (uint8_t)(((uint16_t)neg * neg) >> 8);
        dl = (uint8_t)(((uint16_t)dl * neg) >> 8);
    }

    al = dl;
    if (al == 0) return 0;

    if (di->envf == 0xff) {
        // :602 The extended envelope scales by (volume + 1) / 16 and rounds up
        // on the bit the fourth shift drops.
        uint8_t v = di->eenv_volume;
        if (v == 0) return 0;
        uint16_t product = (uint16_t)al * (uint8_t)(v + 1);
        al = (uint8_t)(product >> 4);
        if ((product & 0x08) != 0) al++;
    } else {
        int8_t penv = (int8_t)di->penv;
        if (penv < 0) {
            uint8_t m = (uint8_t)((uint8_t)(-penv) * 16);
            if (al < m) return 0;
            al = (uint8_t)(al - m);
        } else {
            uint8_t m = (uint8_t)((uint8_t)penv * 16);
            uint8_t sum = (uint8_t)(al + m);
            al = sum < al ? 255 : sum;
        }
    }

    if ((di->lfoswi & 0x22) == 0) return al;
    int16_t delta = 0;
    if ((di->lfoswi & 2) != 0) delta = di->lfodat;
    if ((di->lfoswi & 0x20) != 0) delta = (int16_t)(delta + di->_lfodat);

    if (delta < 0) {
        // :628 A borrow clamps the volume to zero; the original branches on
        // the carry of the 16-bit add.
        unsigned sum = (unsigned)al + (unsigned)(uint16_t)delta;
        al = sum > 0xffff ? (uint8_t)sum : 0;
    } else {
        // :622 Any set bit above the low byte clamps to the maximum.
        unsigned sum = (unsigned)al + (unsigned)delta;
        al = sum > 0xff ? 255 : (uint8_t)sum;
    }
    return al;
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
static void ppzmain(struct pmd *pmd, struct pmd_qq *di) {
    uint8_t *si = di->address;
    if (si == NULL) return;

    if (di->partmask != 0) goto ppzmain_nonplay;

    di->leng--;
    uint8_t al = di->leng;

    // :26 Keyoff when the remaining length reaches Q.
    if ((di->keyoff_flag & 3) == 0 && al <= di->qdat) {
        di->keyoff_flag = 0xff;
        keyoffz(pmd, di);
    }
    if (al != 0) goto mpexitz;

    di->lfoswi &= 0xf7; // portamento off

mp1z:
    al = lod8(&si);
    if (al < 0x80) goto mp2z;
    if (al == 0x80) goto mp15z;

    switch (commandsz(pmd, di, &si)) {
    case PMD_CMD_MP1Z: goto mp1z;
    case PMD_CMD_PPZMNP_1: goto ppzmnp_1;
    case PMD_CMD_PORTA_RETURNZ: goto porta_returnz;
    default: break;
    }
    goto mp1z;

mp15z:
    // :49 End of music, or a loop if "L" was seen.
    si--;
    di->address = si;
    di->loopcheck = 3;
    di->onkai = 0xff;
    if (di->partloop == NULL) goto mpexitz;
    si = di->partloop;
    di->loopcheck = 1;
    goto mp1z;

mp2z:
    // :62 Note: set the pitch and gate.
    al = lfoinitp(pmd, di, al);
    al = oshift(di, al);
    fnumsetz(pmd, di, al);

    al = lod8(&si);
    di->leng = al;
    calc_q(pmd, di, &si);

porta_returnz:
    // :71
    if (di->volpush == 0) goto mp_newz;
    if (di->onkai == 0xff) goto mp_newz;
    pmd->volpush_flag--;
    if (pmd->volpush_flag == 0) goto mp_newz;
    pmd->volpush_flag = 0;
    di->volpush = 0;

mp_newz:
    volsetz(pmd, di);
    otodasiz(pmd, di);
    if ((di->keyoff_flag & 1) != 0) keyonz(pmd, di);
    di->keyon_flag++;

    di->address = si;
    pmd->tieflag = 0;
    pmd->volpush_flag = 0;
    di->keyoff_flag = 0;
    if (*si == 0xfb) di->keyoff_flag = 2; // '&' immediately follows
    mnp_ret(pmd, di);
    return;

mpexitz:;
    // :96 LFO, portamento, and fadeout processing.
    uint8_t cl = di->lfoswi;
    pmd->lfo_switch = (uint8_t)(cl & 8);
    if (cl != 0) {
        if ((cl & 3) != 0) {
            if (lfo(pmd, di)) pmd->lfo_switch |= cl & 3;
        }
        if ((cl & 0x30) != 0) {
            lfo_change(di);
            if (lfo(pmd, di)) {
                lfo_change(di);
                pmd->lfo_switch |= di->lfoswi & 0x30;
            } else {
                lfo_change(di);
            }
        }
        if ((pmd->lfo_switch & 0x19) != 0) {
            if ((pmd->lfo_switch & 8) != 0) porta_calc(pmd, di);
            otodasiz(pmd, di);
        }
    }
    // :136 volsz
    if (!soft_env(pmd, di) && (pmd->lfo_switch & 0x22) == 0 &&
        pmd->fadeout_speed == 0) {
        mnp_ret(pmd, di);
        return;
    }
    volsetz(pmd, di);
    mnp_ret(pmd, di);
    return;

ppzmain_nonplay:
    // :149
    di->keyoff_flag = 0xff;
    di->leng--;
    if (di->leng != 0) {
        mnp_ret(pmd, di);
        return;
    }

ppzmnp_1:
    al = lod8(&si);
    if (al == 0x80) goto ppzmnp_2;
    if (al < 0x80) goto ppzmnp_3;
    switch (commandsz(pmd, di, &si)) {
    case PMD_CMD_MP1Z: goto mp1z;
    case PMD_CMD_PPZMNP_1: goto ppzmnp_1;
    case PMD_CMD_PORTA_RETURNZ: goto porta_returnz;
    default: break;
    }
    goto ppzmnp_1;

ppzmnp_2:
    // :163 End of music, or a loop if "L" was seen.
    si--;
    di->address = si;
    di->loopcheck = 3;
    di->onkai = 0xff;
    if (di->partloop == NULL) {
        fmmnp_4(pmd, di);
        return;
    }
    si = di->partloop;
    di->loopcheck = 1;
    goto ppzmnp_1;

ppzmnp_3:
    // :178 A rest goes through the shared rest-note path.
    di->fnum2 = 0;
    rest_note(pmd, di, &si);
}

// PPZDRV.ASM:185
static enum pmd_cmd_result commandsz(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    return command00(pmd, di, si, cmdtblz);
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
static enum pmd_cmd_result ppz_extpartset(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)di;
    for (int i = 0; i < 8; i++) {
        uint16_t ax = lod16(si);
        if (ax == 0) continue;
        struct pmd_qq *part = &pmd->parts[part10a + i];
        part->address = pmd->mmlbuf + ax;
        part->leng = 1; // starts on the next count
        part->keyoff_flag = 0xff; // currently keyed off
        part->mdc = 0xff; // M depth counter (infinite)
        part->mdc2 = 0xff;
        part->_mdc = 0xff;
        part->_mdc2 = 0xff;
        part->onkai = 0xff; // rest
        part->volume = 128; // PCM volume default
        part->fmpan = 5; // centre
    }
    return PMD_CMD_RETURN;
}

// PPZDRV.ASM:320
static enum pmd_cmd_result ppz_mml_part_mask(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint8_t al = lod8(si);
    if (al >= 2) return special_0c0h(pmd, di, si, al);
    if (al == 0) {
        di->partmask &= 0xbf;
        if (di->partmask != 0) return PMD_CMD_PPZMNP_1;
        return PMD_CMD_MP1Z; // the part is revived
    }

    di->partmask |= 0x40;
    if (di->partmask != 0x40) return PMD_CMD_PPZMNP_1;
#if ademu
    // :330 While the PCM part plays through PPZ8's ADPCM emulation, channel 7
    // is the PCM part's, not this part's.
    if (pmd->partb == 7 && pmd->adpcm_emulate == 1) return PMD_CMD_PPZMNP_1;
#endif
    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    if (drv != NULL) drv->keyoff(pmd->partb, pmd->user_data);
    return PMD_CMD_PPZMNP_1;
}

// PPZDRV.ASM:352
// Sets the part's loop range from the PPZ8 voice table.
static enum pmd_cmd_result ppzrepeat_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    const struct pmd_ppz8_voice *voice = ppz_voicetable_calc(pmd, di);
    if (drv != NULL && voice != NULL) {
        uint32_t size = voice->size;
        // Each parameter is an absolute offset unless it is negative, in which
        // case it counts back from the end of the data.
        const uint8_t *p = *si;
        uint32_t start = 0;
        uint32_t end = 0;
        for (int i = 0; i < 2; i++) {
            uint16_t ax = (uint16_t)(p[0] | (p[1] << 8));
            p += 2;
            // PPZDRV.ASM:376 get_loop_ppz8 offsets a negative value by the
            // whole data size and leaves a positive one as it is.
            uint32_t value = ax;
            if ((int16_t)ax < 0) value = (uint32_t)(int32_t)(int16_t)ax + size;
            if (i == 0) start = value;
            else end = value;
        }
        drv->set_loop(pmd->partb, start, end, pmd->user_data);
    }
    *si += 6;
    return PMD_CMD_RETURN;
}

// PPZDRV.ASM:392
// Returns the part's PPZ8 voice table entry, or NULL when the voice is not
// loaded.
static const struct pmd_ppz8_voice *ppz_voicetable_calc(struct pmd *pmd, struct pmd_qq *di) {
    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    if (drv == NULL) return NULL;
    return drv->get_voice(di->voicenum, pmd->user_data);
}

// PPZDRV.ASM:415
// Portamento for the PPZ8 parts (0dah).
static enum pmd_cmd_result portaz(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    if (di->partmask != 0) {
        (*si)++; // the first pitch byte is skipped while masked
        return PMD_CMD_RETURN;
    }

    uint8_t al = lod8(si);
    al = lfoinitp(pmd, di, al);
    al = oshift(di, al);
    fnumsetz(pmd, di, al);

    uint32_t fnum = ((uint32_t)di->fnum2 << 16) | di->fnum;
    uint8_t onkai = di->onkai;

    al = lod8(si);
    al = oshift(di, al);
    fnumsetz(pmd, di, al);
    uint32_t target = ((uint32_t)di->fnum2 << 16) | di->fnum;

    di->onkai = onkai;
    di->fnum = (uint16_t)fnum;
    di->fnum2 = (uint16_t)(fnum >> 16);

    int32_t delta = (int32_t)(target - fnum) >> 4;
    di->leng = lod8(si);
    calc_q(pmd, di, si);

    int16_t n = (int16_t)(uint16_t)delta;
    di->porta_num2 = (int16_t)(n / (int16_t)di->leng);
    di->porta_num3 = (int16_t)(n % (int16_t)di->leng);
    di->lfoswi |= 8; // portamento on
    return PMD_CMD_PORTA_RETURNZ;
}

// PPZDRV.ASM:475
static enum pmd_cmd_result pansetz(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint8_t al = lod8(si);
    al = ppzpandata[al & 3];
    di->fmpan = al;
    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    if (drv != NULL) drv->set_pan(pmd->partb, al, pmd->user_data);
    return PMD_CMD_RETURN;
}

// PPZDRV.ASM:494
// Pan setting extend: px -4..+4 (0c3h), mapped onto PPZ8's 1..9.
static enum pmd_cmd_result pansetz_ex(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint8_t al = lod8(si);
    (*si)++; // the reversed-phase flag is skipped
    if ((int8_t)al >= 0) {
        if (al >= 5) al = 4;
    } else if ((int8_t)al < -4) {
        al = (uint8_t)-4;
    }
    al = (uint8_t)(al + 5);
    di->fmpan = al;
    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    if (drv != NULL) drv->set_pan(pmd->partb, al, pmd->user_data);
    return PMD_CMD_RETURN;
}

// PPZDRV.ASM:514
static enum pmd_cmd_result com_at_z(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint8_t al = lod8(si);
#if ademu
    bool revive = false;
    if (pmd->adpcm_emulate == 1) {
        // :521 Voices 128-255 cannot be emulated; they fall back to 127.
        if ((int8_t)al < 0) al = 127;
        if (pmd->partb == 7) {
            pmd->parts[part10].partmask |= 0x10;
            di->partmask &= 0xef;
            revive = di->partmask == 0;
            pmd->adpcm_emulate = 0;
            const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
            if (drv != NULL) {
                drv->set_adpcm_emulation(false, pmd->user_data);
            }
        }
    }
#endif
    di->voicenum = al;
    ppz_neiro_reset(pmd, di);
#if ademu
    if (revive) return PMD_CMD_MP1Z;
#endif
    return PMD_CMD_RETURN;
}

// PPZDRV.ASM:542
// Loads the voice's loop range and source rate into PPZ8.
static void ppz_neiro_reset(struct pmd *pmd, struct pmd_qq *di) {
    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    if (drv == NULL) return;
    const struct pmd_ppz8_voice *voice = ppz_voicetable_calc(pmd, di);
    if (voice == NULL) return;
    drv->set_loop(pmd->partb, voice->loop_start, voice->loop_end, pmd->user_data);
    drv->set_source_rate(pmd->partb, voice->rate, pmd->user_data);
}

// PPZDRV.ASM:571
static void volsetz(struct pmd *pmd, struct pmd_qq *di) {
    uint8_t al = pcm_vol_calc(pmd, di, pmd->ppz_voldown, false);
    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    if (drv == NULL) return;
    if (al == 0) {
        drv->keyoff(pmd->partb, pmd->user_data); // volume 0 is a keyoff
        return;
    }
    drv->set_volume(pmd->partb, (uint8_t)(al >> 4), pmd->user_data);
}

// PPZDRV.ASM:699
static void keyonz(struct pmd *pmd, struct pmd_qq *di) {
    if (di->onkai == 0xff) return;
    const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
    if (drv == NULL) return;
    // :710 DX = the voice number with bit 7 moved to bit 15 (the bank select).
    uint8_t num = di->voicenum;
    uint16_t voice = (uint16_t)((num & 0x7f) | ((uint16_t)(num & 0x80) << 8));
    drv->keyon(pmd->partb, voice, pmd->user_data);
}

// PPZDRV.ASM:720
static void keyoffz(struct pmd *pmd, struct pmd_qq *di) {
    if (di->envf == 0xff) {
        if (di->eenv_count == 4) return;
    } else if (di->envf == 2) {
        return;
    }
    keyoffp(pmd, di);
}

// PPZDRV.ASM:735
// Sets the PPZ8 tone from the fnum, portamento, LFO, and detune.
static void otodasiz(struct pmd *pmd, struct pmd_qq *di) {
    uint32_t value = ((uint32_t)di->fnum2 << 16) | di->fnum;
    if (value == 0) return;

    if (di->porta_num != 0) value += (uint32_t)((int32_t)di->porta_num * 16);

    int16_t ax = 0;
    if ((di->lfoswi & 0x11) != 0) {
        if ((di->lfoswi & 1) != 0) ax = (int16_t)(ax + di->lfodat);
        if ((di->lfoswi & 0x10) != 0) ax = (int16_t)(ax + di->_lfodat);
    }
    ax = (int16_t)(ax + di->detune);

    // :771 DX = the high bytes of both halves, the pitch the delta scales by.
    int16_t scale = (int16_t)((uint16_t)((value >> 8) & 0xff) |
                              (uint16_t)(((value >> 24) & 0xff) << 8));
    int32_t product = (int32_t)ax * scale;
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
// Sets the PPZ8 pitch from the note in al.
static void fnumsetz(struct pmd *pmd, struct pmd_qq *di, uint8_t al) {
    (void)pmd;
    uint8_t ah = al & 0x0f;
    if (ah == 0x0f) goto fnrestz; // a rest
    di->onkai = al;

    uint16_t ax = ppz_tune_data[ah];
    uint16_t dx = 0;

    uint8_t cl = (uint8_t)((al >> 4) & 0x0f); // the octave
    if (cl < 4) {
        ax = (uint16_t)(ax >> (4 - cl));
        goto ppz_fnumset;
    }
    if (cl == 4) goto ppz_fnumset;
    for (cl -= 4; cl != 0; cl--) {
        dx = (uint16_t)((dx << 1) | (ax >> 15));
        ax = (uint16_t)(ax << 1);
    }
ppz_fnumset:
    di->fnum = ax;
    di->fnum2 = dx;
    return;

fnrestz:
    di->onkai = 0xff;
    if ((di->lfoswi & 0x11) != 0) return;
    di->fnum = 0;
    di->fnum2 = 0;
}
#endif

#if board2 && (pcm || adpcm)
#if pcm
// PCMDRV86.ASM:412
// The /S option scales every explicit size and offset by 32.
static uint16_t pcm86vol_chk(struct pmd *pmd, uint16_t ax) {
    if (pmd->pcm86_vol == 0) return ax;
    unsigned scaled = (unsigned)ax << 5;
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
static void tone_set2(struct pmd *pmd, uint16_t ax, uint8_t bl) {
    pmd->addsize2 = ax;
    pmd->addsize1 = (uint8_t)(bl & 0x1f);
    // :796 Bits 5-7 of BL become the slowest three rate bits, inverted.
    bl = (uint8_t)(((bl << 3) | (bl >> 5)) & 0x07);
    bl ^= 7;
    uint8_t al = inb(pmd, 0xa468);
    outb(pmd, 0x5f, al);
    al &= 0xf8;
    al |= bl;
    outb(pmd, 0xa468, al);
}

// PCMDRV86.ASM:534
// Loads a PCM86 voice's data range from the voice table.
static void neiro_set(struct pmd *pmd, uint8_t al) {
    const struct pmd_pcm86_voice *v = &pmd->pcm_table.pcm86.pcmadrs[al];
    pmd->_start_ofs = (uint16_t)v->offset;
    pmd->_start_ofs2 = (uint16_t)(v->offset >> 16);
    pmd->_size1 = (uint16_t)v->size;
    pmd->_size2 = (uint16_t)(v->size >> 16);
    pmd->repeat_flag = 0;
    pmd->release_flag1 = 0;
}

// PCMDRV86.ASM:671
// Writes a 0..255 volume to the PCM86 volume register.
static void pcm86_vol_out(struct pmd *pmd, uint8_t al) {
    if (pmd->pcm86_vol != 0) {
        // :676 SPB-style volume: the register takes roughly sqrt(al).
        uint8_t ah = al;
        uint8_t sq = 0;
        bool carry = true;
        for (;;) {
            unsigned sub = (unsigned)ah - sq - (carry ? 1 : 0);
            carry = sub > 0xff;
            ah = (uint8_t)sub;
            if (carry) break;
            sub = (unsigned)ah - sq;
            carry = sub > 0xff;
            ah = (uint8_t)sub;
            if (carry) break;
            sq++;
            if (sq == 15) break;
        }
        al = sq;
    } else {
        al >>= 4;
    }
    al = (uint8_t)((al & 0x0f) ^ 0x0f);
    outb(pmd, 0xa466, (uint8_t)(al | 0xa0));
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
    uint16_t si;
    // DI: the remaining size's low word.
    uint16_t di;
    // BP: the bytes written since the last remap.
    uint16_t bp;
};

// PCMDRV86.ASM:1094
// Advances the sample address by one step. Returns true when the transfer is
// finished, i.e. the original's carry.
static bool add_address(struct pmd *pmd, struct pcm_transfer *t) {
    uint16_t sum = (uint16_t)(pmd->addsizew + (uint16_t)pmd->addsize2);
    bool carry = sum < pmd->addsizew;
    pmd->addsizew = sum;

    uint16_t add = pmd->addsize1;
    t->bp = (uint16_t)(t->bp + add + (carry ? 1 : 0));
    t->si = (uint16_t)(t->si + add + (carry ? 1 : 0));

    if (t->si >= 0x4000) {
        // The 16 KB EMS window is exhausted: advance and remap.
        uint32_t offset = ((uint32_t)pmd->start_ofs2 << 16) | pmd->start_ofs;
        offset += t->bp;
        pmd->start_ofs = (uint16_t)offset;
        pmd->start_ofs2 = (uint16_t)(offset >> 16);
        t->bp = 0;
        t->base = get_data_offset(pmd);
        t->si = 0;
    }

    unsigned di = (unsigned)t->di - add - (carry ? 1 : 0);
    bool borrow = di > 0xffff;
    t->di = (uint16_t)di;

    if (!borrow) {
        if (t->di != 0) return false;
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
    t->di = pmd->repeat_size1;
    pmd->start_ofs2 = pmd->repeat_ofs2;
    pmd->start_ofs = pmd->repeat_ofs;
    t->bp = 0;
    t->base = get_data_offset(pmd);
    t->si = 0;
    return false;
}

// PCMDRV86.ASM:910
// Transfers up to `count` stereo samples from the PCM86 data to the FIFO.
static void pcm_trans(struct pmd *pmd, uint16_t count) {
    if (pmd->p86 == NULL) goto zero_trans;
    if (pmd->p86->get_version(pmd->user_data) < 0x10) goto zero_trans;
    if ((pmd->size1 | pmd->size2) == 0) goto zero_trans;

    pmd->p86->push_ems(pmd->user_data);
    {
        struct pcm_transfer t = {
            .base = get_data_offset(pmd),
            .si = 0,
            .di = pmd->size1,
            .bp = 0,
        };
        uint16_t remaining = count;
        bool finished = false;
        for (;;) {
            if (t.base == NULL) {
                finished = true;
                break;
            }
            pcm86_out_sample(pmd, t.base[t.si]);
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
            offset += t.bp;
            pmd->start_ofs = (uint16_t)offset;
            pmd->start_ofs2 = (uint16_t)(offset >> 16);
            pmd->size1 = t.di;
        }
    }
    pmd->p86->pop_ems(pmd->user_data);
    return;

zero_trans:
    // PCMDRV86.ASM:1193 Fill the buffer with silence and stop transferring.
    // The original jumps here before loading dx with the PCM data port, so its
    // writes land on a leftover port; the FIFO data port is used here.
    for (uint16_t i = 0; i < count; i++) {
        outb(pmd, 0xa46c, 0);
        outb(pmd, 0xa46c, 0);
    }
    pmd->trans_flag = 0;
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
    uint8_t al = inb(pmd, 0xa468);
    outb(pmd, 0x5f, al);
    al &= 0xef;
    outb(pmd, 0xa468, al);
    outb(pmd, 0x5f, al);
    al |= 0x10;
    outb(pmd, 0xa468, al);
}

// PCMDRV86.ASM:1205
static void play_86pcm(struct pmd *pmd) {
    // :1209 Stop playback, select the CPU-to-FIFO mode, and reset the FIFO.
    uint8_t al = inb(pmd, 0xa468);
    outb(pmd, 0x5f, al);
    al &= 0x7f;
    outb(pmd, 0xa468, al);
    outb(pmd, 0x5f, al);
    al &= 0xbf;
    outb(pmd, 0xa468, al);
    outb(pmd, 0x5f, al);
    al |= 0x08;
    outb(pmd, 0xa468, al);
    outb(pmd, 0x5f, al);
    al &= 0xf7;
    outb(pmd, 0xa468, al);
    outb(pmd, 0x5f, al);
    al &= 0xdf;
    outb(pmd, 0xa468, al);
    outb(pmd, 0x5f, al);
    al &= 0xef;
    outb(pmd, 0xa468, al);
    outb(pmd, 0xa46a, 0xf2); // pan: 8-bit left and right

    // :1249 The first buffer comes from the voice's start.
    pmd->start_ofs = pmd->_start_ofs;
    pmd->start_ofs2 = pmd->_start_ofs2;
    pmd->size1 = pmd->_size1;
    pmd->size2 = pmd->_size2;
    pmd->addsizew = 0;
    pmd->release_flag2 = 0;
    pcm_trans(pmd, trans_size);

    // :1265 Enable the FIFO interrupt at trans_size bytes.
    al = inb(pmd, 0xa468);
    outb(pmd, 0x5f, al);
    al |= 0x10;
    outb(pmd, 0xa468, al);
    outb(pmd, 0x5f, al);
    al |= 0x20;
    outb(pmd, 0xa468, al);
    outb(pmd, 0xa46a, (uint8_t)(trans_size / 128 - 1));

    // :1285 Start playback.
    al = inb(pmd, 0xa468);
    outb(pmd, 0x5f, al);
    al |= 0x80;
    outb(pmd, 0xa468, al);

    pmd->play86_flag = 1;
    pmd->trans_flag = 1;
}

// PCMDRV86.ASM:1300
// Stops PCM86 playback and leaves the FIFO interrupt disabled.
static void stop_86pcm(struct pmd *pmd) {
    uint8_t al = inb(pmd, 0xa468);
    outb(pmd, 0x5f, al);
    al &= 0x7f;
    outb(pmd, 0xa468, al);

    outb(pmd, 0x5f, al);
    al |= 0x08;
    outb(pmd, 0xa468, al); // FIFO reset
    outb(pmd, 0x5f, al);
    al &= 0xf7;
    outb(pmd, 0xa468, al); // reset released

    outb(pmd, 0x5f, al);
    al &= 0xdf;
    outb(pmd, 0xa468, al); // FIFO interrupt disabled

    outb(pmd, 0x5f, al);
    al &= 0xef;
    outb(pmd, 0xa468, al);
    outb(pmd, 0x5f, al);
    al |= 0x10;
    outb(pmd, 0xa468, al);

    pmd->play86_flag = 0;
    pmd->trans_flag = 0;
}
#endif

#if pcm
// PCMDRV86.ASM:1347
// Plays a PCM effect: AL is the effect number, DX the DeltaN, CH the pan, and
// CL the volume.
static void pcm_effect(struct pmd *pmd, uint8_t al, uint16_t dx, uint8_t ch,
                       uint8_t cl) {
    pmd->parts[part10].partmask |= 2;
    pmd->pcmflag = 1;
    pmd->pcm_effec_num = al;
    pmd->_voice_delta_n = dx;
    pmd->_pcm_volume = cl;
    pmd->_pcmpan = ch;

    stop_86pcm(pmd);

    neiro_set(pmd, pmd->pcm_effec_num);
    set_pcm_pan2(pmd, pmd->_pcmpan, 0);

    // :1366 Fold the DeltaN into the addsize pair: the high byte becomes the
    // rate and the low byte the fraction.
    uint8_t lo = (uint8_t)pmd->_voice_delta_n;
    uint8_t hi = (uint8_t)(pmd->_voice_delta_n >> 8);
    uint8_t bl = (uint8_t)((hi & 0x0f) | ((hi & 0x70) << 1));
    tone_set2(pmd, (uint16_t)(lo << 8), bl);
    pcm86_vol_out(pmd, pmd->_pcm_volume);
    play_86pcm(pmd);
}
#elif ademu
// PCMDRVE.ASM:761 The PPZ8 emulation has no interrupt PCM effect.
static void pcm_effect(struct pmd *pmd, uint8_t al, uint16_t dx, uint8_t ch,
                       uint8_t cl) {
    (void)pmd;
    (void)al;
    (void)dx;
    (void)ch;
    (void)cl;
}
#else
// PCMDRV.ASM:821
static void pcm_effect(struct pmd *pmd, uint8_t al, uint16_t dx, uint8_t ch,
                       uint8_t cl) {
    if (pmd->pcm_gs_flag == 1) return; // the ADPCM is not available

    pmd->parts[part10].partmask |= 2;
    pmd->pcmflag = 1;
    pmd->pcm_effec_num = al;
    pmd->_voice_delta_n = dx;
    pmd->_pcm_volume = cl;
    pmd->_pcmpan = ch;

    {
        const struct pmd_pcm_adpcm_voice *v = &pmd->pcm_table.adpcm_data.pcmadrs[al];
        pmd->_pcmstart = v->start;
        pmd->_pcmstop = v->stop;
    }
    opnset46(pmd, (uint16_t)((0x0b << 8) | pmd->_pcm_volume));
    uint16_t bx = pmd->_voice_delta_n;
    opnset46(pmd, (uint16_t)((0x09 << 8) | (uint8_t)bx));
    opnset46(pmd, (uint16_t)((0x0a << 8) | (uint8_t)(bx >> 8)));

    // PCMDRV.ASM:884 pcm_keyon
    opnset46(pmd, 0x0102); // x8 bit mode
    opnset46(pmd, 0x0021); // PCM reset
    opnset46(pmd, (uint16_t)((0x02 << 8) | (uint8_t)pmd->_pcmstart));
    opnset46(pmd, (uint16_t)((0x03 << 8) | (uint8_t)(pmd->_pcmstart >> 8)));
    opnset46(pmd, (uint16_t)((0x04 << 8) | (uint8_t)pmd->_pcmstop));
    opnset46(pmd, (uint16_t)((0x05 << 8) | (uint8_t)(pmd->_pcmstop >> 8)));
    opnset46(pmd, 0x00a0); // play once
    // :883 PCMDRV.ASM:884 rotates the masked pan into the top two bits.
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
static void effgo(struct pmd *pmd, struct pmd_qq *di, uint8_t al) {
    if (pmd->ppsdrv_flag != 0) {
        al |= 0x80;
        bool same = pmd->last_shot_data == al;
        pmd->last_shot_data = al;
        if (same && pmd->pps != NULL) pmd->pps->keyoff(pmd->user_data);
    }
    pmd->hosei_flag = 3; // pitch and volume correction (the rhythm part)
    eff_main(pmd, di, al);
}

// EFCDRV.ASM:22
// The 'n' command's entry: it corrects the pitch only.
static void eff_on2(struct pmd *pmd, struct pmd_qq *di, uint8_t al) {
    pmd->hosei_flag = 1;
    eff_main(pmd, di, al);
}

// EFCDRV.ASM:25
// The INT 60h entry to eff_main, which it reaches with no pitch or volume
// correction.
static void eff_on(struct pmd *pmd, uint8_t al) {
    pmd->hosei_flag = 0;
    eff_main(pmd, NULL, al);
}

// EFCDRV.ASM:25
void pmd_eff_on(struct pmd *pmd, uint8_t efcnum) {
    bool ok = int60_enter(pmd, 0x03);
    if (ok && pmd->board != 0) eff_on(pmd, efcnum);
    int60_exit(pmd);
}

// EFCDRV.ASM:27
// Starts the effect named by al. A negative effect number goes to PPSDRV when
// PPSDRV is resident and the effect driver is off; everything else uses the
// SSG effect table.
static void eff_main(struct pmd *pmd, struct pmd_qq *di, uint8_t al) {
    if (pmd->effflag != 0) return; // the effects are disabled

    if (pmd->ppsdrv_flag != 0 && (int8_t)al < 0) {
        // :40 ppsdrv
        if (pmd->effon >= 2) return; // an ordinary effect is sounding
        pmd->parts[part9].partmask |= 2;
        pmd->effon = 1; // priority 1 (ppsdrv)
        pmd->psgefcnum = al;

        uint8_t bh = 0;
        uint8_t bl = 15;
        if ((pmd->hosei_flag & 1) != 0) {
            // BH = the low byte of the part's detune
            bh = (uint8_t)di->detune;
            bl = 15;
        }
        if ((pmd->hosei_flag & 2) != 0) {
            if (di->volume < 15) bl = di->volume;
            uint8_t fade = pmd->fadeout_volume;
            if (fade != 0) {
                uint16_t product = (uint16_t)bl * (uint8_t)(-fade);
                bl = (uint8_t)(product >> 8);
            }
        }
        if (bl == 0) return;
        bl ^= 0x0f;
        if (pmd->pps != NULL) {
            struct pmd_pps_keyon keyon = {
                .num = (uint8_t)(al & 0x7f),
                .detune = bh,
                .volume = bl,
            };
            pmd->pps->keyon(keyon, pmd->user_data);
        }
        return;
    }

    // :84 eg_nonppsdrv
    pmd->psgefcnum = al;
    if (al >= eff_count) return; // no such effect; the original reads past the table
    const uint8_t *bx = &efftbl[(unsigned)al * 3];
    if (pmd->effon > bx[0]) return; // a higher-priority effect is sounding

    if (pmd->ppsdrv_flag != 0 && pmd->pps != NULL) {
        pmd->pps->keyoff(pmd->user_data);
    }
    const uint8_t *si = &efftbl[(unsigned)(bx[1] | (bx[2] << 8))];
    uint8_t priority = bx[0];
    pmd->parts[part9].partmask |= 2;
    efffor(pmd, &si); // the first frame
    pmd->effon = priority;
}

// EFCDRV.ASM:118
// Advances the SSG effect by one Timer A tick.
static void effplay(struct pmd *pmd) {
    if (--pmd->effcnt != 0) {
        effsweep(pmd);
        return;
    }
    const uint8_t *si = pmd->effadr;
    efffor(pmd, &si);
}

// EFCDRV.ASM:178
// Reads one data byte and writes it to the register in dh, then steps dh.
static void efsnd(struct pmd *pmd, const uint8_t **si, uint8_t *dh,
                  uint8_t *dl) {
    *dl = *(*si)++;
    opnset44(pmd, (uint16_t)((*dh << 8) | *dl));
    (*dh)++;
}

// EFCDRV.ASM:125
// Plays one effect frame from si and stores the cursor for the sweep ticks.
static void efffor(struct pmd *pmd, const uint8_t **si) {
    uint8_t dh;
    uint8_t dl;

    uint8_t al = *(*si)++;
    if (al == 0xff) {
        effend(pmd);
        return;
    }
    pmd->effcnt = al;

    dh = 4; // tone frequency
    efsnd(pmd, si, &dh, &dl);
    uint8_t cl = dl;
    efsnd(pmd, si, &dh, &dl);
    uint8_t ch = dl;
    pmd->eswthz = (uint16_t)((ch << 8) | cl);

    pmd->eswnhz = **si;
    dh = 6; // noise frequency
    efsnd(pmd, si, &dh, &dl);
    pmd->psnoi_last = dl;

    // The mix data byte: bit 3 is the tone and bit 5 the noise enable.
    al = *(*si)++;
    dl = (uint8_t)(((al << 2) | (al >> 6)) & 0x24);
    uint8_t reg07 = get07(pmd);
    dl = (uint8_t)(dl | (reg07 & 0xdb));
    opnset44(pmd, (uint16_t)((7 << 8) | dl));

    dh = 10; // volume, envelope frequency (two bytes), and envelope pattern
    efsnd(pmd, si, &dh, &dl);
    efsnd(pmd, si, &dh, &dl);
    efsnd(pmd, si, &dh, &dl);
    efsnd(pmd, si, &dh, &dl);

    pmd->eswtst = (uint16_t)(int16_t)(int8_t)*(*si)++;
    al = *(*si)++;
    pmd->eswnst = al;
    pmd->eswnct = al & 0x0f;
    pmd->effadr = *si;
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
    uint8_t dl = get07(pmd);
    dl = (uint8_t)((dl & 0xdb) | 0x24);
    opnset44(pmd, (uint16_t)((7 << 8) | dl));
    pmd->effon = 0;
    pmd->psgefcnum = 0xff;
}

// EFCDRV.ASM:210
// Applies the tone and noise sweeps of the current effect frame.
static void effsweep(struct pmd *pmd) {
    uint16_t ax = (uint16_t)(pmd->eswthz + pmd->eswtst);
    pmd->eswthz = ax;
    opnset44(pmd, (uint16_t)((4 << 8) | (uint8_t)ax));
    opnset44(pmd, (uint16_t)((5 << 8) | (uint8_t)(ax >> 8)));
    // The original rewrites the mix register with its own contents.
    opnset44(pmd, (uint16_t)((7 << 8) | get07(pmd)));

    uint8_t dl = pmd->eswnst;
    if (dl == 0) return; // no noise sweep

    if (--pmd->eswnct != 0) return;
    pmd->eswnct = dl & 0x0f;

    dl = (uint8_t)((int8_t)dl >> 4);
    pmd->eswnhz = (uint8_t)(pmd->eswnhz + dl);
    dl = pmd->eswnhz;
    opnset44(pmd, (uint16_t)((6 << 8) | dl));
    pmd->psnoi_last = dl;
}

// :392
// Plays one Timer A tick of the FM effect through part_e.
static void fm_efcplay(struct pmd *pmd) {
    pmd->prgdat_adr2 = pmd->efcdat + (pmd->efcdat[254] | (pmd->efcdat[255] << 8));

    struct pmd_qq *di = &pmd->part_e;
#if board2
    uint16_t port1 = pmd->fm_port1;
    uint16_t port2 = pmd->fm_port2;
    uint8_t partb = pmd->partb;
    uint8_t fmsel = pmd->fmsel;
    pmd->partb = 3;
    sel46(pmd); // the handler must stay on the FM2 bank even if mmain runs
    uint8_t *si = fmmain(pmd, di);
    pmd->partb = partb;
    pmd->fmsel = fmsel;
    pmd->fm_port1 = port1;
    pmd->fm_port2 = port2;
#else
    uint8_t partb = pmd->partb;
    pmd->partb = 3;
    uint8_t *si = fmmain(pmd, di);
    pmd->partb = partb;
#endif
    if (*si == 0x80 && di->leng == 0) fm_effect_off(pmd);
}

// :377
uint16_t pmd_get_ss(struct pmd *pmd) {
    int60_enter(pmd, 0x05);
    uint16_t ax = pmd->board != 0 ? pmd->syousetu : PMD_NO_BOARD;
    int60_exit(pmd);
    return ax;
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
    uint8_t *si = pmd->mmlbuf;
    pmd->x68_flg = si[-1];

    // :510 The first word of the module is 2 * (max_part2 + 1) for an empty
    // module, in which case there is no program-change area.
    if (si[0] != 2 * (max_part2 + 1)) {
        uint16_t offset = (uint16_t)(si[2 * (max_part2 + 1)] |
                                     (si[2 * (max_part2 + 1) + 1] << 8));
        pmd->prgdat_adr = si + offset;
        pmd->prg_flg = 1;
    } else {
        pmd->prg_flg = 0;
    }

    // :520
    for (unsigned dl = 0; dl < max_part2; dl++) {
        struct pmd_qq *di = pmd->part_data_table[dl];
        uint16_t offset = (uint16_t)(si[0] | (si[1] << 8));
        si += 2;
        uint8_t *address = pmd->mmlbuf + offset;
        if (*address == 0x80) address = NULL;
        di->address = address;
        di->leng = 1;
        di->keyoff_flag = 0xff;
        di->mdc = 0xff;
        di->mdc2 = 0xff;
        di->_mdc = 0xff;
        di->_mdc2 = 0xff;
        di->onkai = 0xff;
        di->onkai_def = 0xff;

        if (dl < 6) {
            // :544 FM1-FM6.
            di->volume = 108;
            di->fmpan = 0xc0;
#if board2
            di->slotmask = 0xf0;
            di->neiromask = 0xff;
#else
            if (dl < 3) {
                di->slotmask = 0xf0;
                di->neiromask = 0xff;
            } else {
                di->partmask |= 0x20; // s0
            }
#endif
        } else if (dl < 9) {
            // :563 PSG1-PSG3.
            di->volume = 8;
            di->psgpat = 7;
            di->envf = 3;
        } else if (dl == 9) {
#if board2
            // :575 Part 9 (OPNA/ADPCM or PCM).
            di->volume = 128;
#if adpcm
            di->fmpan = 0xc0;
#endif
#if pcm
            di->fmpan = 0;
            pmd->pcm86_pan_flag = 0;
            pmd->revpan = 0;
#endif
#endif
        } else if (dl == 10) {
            // :597 Part 10 (rhythm).
            di->volume = 15;
        }
    }

    // :605
    uint16_t offset = (uint16_t)(si[0] | (si[1] << 8));
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
        struct pmd_qq *di = &pmd->parts[i];
        uint8_t dh = di->partmask;
        uint8_t dl = di->keyon_flag;
        memset(di, 0, sizeof(*di));
        di->partmask = dh & 0x0f;
        di->keyon_flag = dl;
        di->onkai = 0xff;
        di->onkai_def = 0xff;
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
    uint16_t dx;

    opnset44(pmd, 0x2983);

    pmd->psnoi = 0;
    if (pmd->effon == 0) {
        opnset44(pmd, 0x0600); // PSG noise
        pmd->psnoi_last = 0;
    }

    // :729 SSG-EG reset.
#if board2
    sel44(pmd);
    for (unsigned bx = 2; bx != 0; bx--) {
        uint8_t cl = 15;
        dx = 0x9000; // SSG-EG registers
        do {
            if ((cl & 3) != 0) opnset(pmd, dx);
            dx = (uint16_t)(dx + 0x0100);
            cl--;
        } while (cl != 0);
        sel46(pmd);
    }
    sel44(pmd);
#else
    {
        uint8_t cl = 15;
        dx = 0x9000; // SSG-EG registers
        do {
            if ((cl & 3) != 0) opnset(pmd, dx);
            dx = (uint16_t)(dx + 0x0100);
            cl--;
        } while (cl != 0);
    }
#endif

#if !board2
    // :745 A YM2203 stops here.
    if (pmd->ongen == 0) return;
#endif

    // :753 PAN and hard LFO defaults for the YM2608.
#if board2
    for (unsigned bx = 2; bx != 0; bx--) {
        // The original assumes mmain cannot interrupt here and skips the
        // initial sel44.
        dx = 0xb4c0;
        uint8_t cx = 3;
        do {
            bool skip = pmd->fm_effec_flag != 0 && cx == 1 && bx == 1;
            if (!skip) opnset(pmd, dx);
            dx = (uint16_t)(dx + 0x0100);
            cx--;
        } while (cx != 0);
        sel46(pmd);
    }
    sel44(pmd);
#else
    dx = 0xb4c0;
    {
        uint8_t cx = 3;
        do {
            bool skip = pmd->fm_effec_flag != 0 && cx == 1;
            if (!skip) opnset(pmd, dx);
            dx = (uint16_t)(dx + 0x0100);
            cx--;
        } while (cx != 0);
    }
#endif

    // :795
    dx = 0x2200;
    pmd->port22h = (uint8_t)dx;
    opnset44(pmd, dx);

#if board2
    // :801 Rhythm defaults: pan middle, volume 15.
    for (int i = 0; i < 6; i++) pmd->rdat[i] = 0xcf;
    opnset44(pmd, 0x10ff); // rhythm all dump

    // :811 Rhythm total level.
    uint8_t dl = 48;
    uint8_t al = pmd->rhythm_voldown;
    if (al != 0) {
        dl = (uint8_t)(dl << 2);
        uint16_t product = (uint16_t)(uint8_t)(-al) * dl;
        dl = (uint8_t)(product >> 8);
        dl >>= 1;
        dl >>= 1;
    }
    pmd->rhyvol = dl;
    opnset44(pmd, (uint16_t)((0x11 << 8) | dl));

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
            for (uint8_t ch = 8; ch != 0; ch--) {
                drv->set_pan((uint8_t)(ch - 1), 5, pmd->user_data);
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

    if (pmd->x68_flg != 0) goto mmain_exit;

    // :967
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

mmain_exit:
    // :994
    if (pmd->loop_work == 0) return;

    // :999
    for (int i = 0; i < max_part1; i++) {
        struct pmd_qq *di = pmd->part_data_table[i];
        if (di->loopcheck == 3) continue;
        di->loopcheck = 0;
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
static PMDC_UNUSED void sel46(struct pmd *pmd) {
    pmd->fm_port1 = pmd->fm2_port1;
    pmd->fm_port2 = pmd->fm2_port2;
    pmd->fmsel = 1;
}

// :1039
static PMDC_UNUSED void sel44(struct pmd *pmd) {
    pmd->fm_port1 = pmd->fm1_port1;
    pmd->fm_port2 = pmd->fm1_port2;
    pmd->fmsel = 0;
}
#endif

// :1053
// The FM part main loop. SI is the part's data cursor and DI its work area;
// the cursor is returned for fm_efcplay, whose end-of-effect test reads the
// live SI (:407).
static uint8_t *fmmain(struct pmd *pmd, struct pmd_qq *di) {
    uint8_t *si = di->address;
    if (si == NULL) return si;

    if (di->partmask != 0) goto fmmain_nonplay;

    // :1058
    di->leng--;
    uint8_t al = di->leng;

    // :1062 Keyoff when the remaining length reaches Q.
    if ((di->keyoff_flag & 3) == 0 && al <= di->qdat) {
        keyoff(pmd, di);
        di->keyoff_flag = 0xff;
    }

    if (al != 0) goto mpexit;

mp10:
    di->lfoswi &= 0xf7; // portamento off

mp1:
    al = lod8(&si);
    if (al < 0x80) goto mp2;
    if (al == 0x80) goto mp15;

    switch (commands(pmd, di, &si)) {
    case PMD_CMD_MP1: goto mp1;
    case PMD_CMD_FMMNP_1: goto fmmnp_1;
    case PMD_CMD_PORTA_RETURN: goto porta_return;
    default: break;
    }
    goto mp1;

mp15:
    // :1089 End of music, or a loop if "L" was seen.
    si--;
    di->address = si;
    di->loopcheck = 3;
    di->onkai = 0xff;
    if (di->partloop == NULL) goto mpexit;
    si = di->partloop;
    di->loopcheck = 1;
    goto mp1;

mp2:
    // :1101 Note: set the pitch and gate.
    al = lfoinit(pmd, di, al);
    al = oshift(di, al);
    fnumset(pmd, di, al);

    al = lod8(&si);
    di->leng = al;
    calc_q(pmd, di, &si);

porta_return:
    // :1110
    if (di->volpush == 0) goto mp_new;
    if (di->onkai == 0xff) goto mp_new;
    pmd->volpush_flag--;
    if (pmd->volpush_flag == 0) goto mp_new;
    pmd->volpush_flag = 0;
    di->volpush = 0;

mp_new:
    volset(pmd, di);
    otodasi(pmd, di);
    keyon(pmd, di);
    di->keyon_flag++;

    di->address = si;
    pmd->tieflag = 0;
    pmd->volpush_flag = 0;
    di->keyoff_flag = 0;
    if (*si == 0xfb) di->keyoff_flag = 2; // '&' immediately follows
    mnp_ret(pmd, di);
    return si;

mpexit:
    // :1134 LFO, portamento, and fadeout processing.
#if board2
    if (di->hldelay_c != 0) {
        di->hldelay_c--;
        if (di->hldelay_c == 0) {
            opnset(pmd, (uint16_t)(((pmd->partb + 0xb3) << 8) | di->fmpan));
        }
    }
#endif
    if (di->sdelay_c != 0) {
        di->sdelay_c--;
        if (di->sdelay_c == 0 && (di->keyoff_flag & 1) == 0) keyon(pmd, di);
    }

    uint8_t cl = di->lfoswi;
    pmd->lfo_switch = cl & 8;
    if (cl != 0) {
        if ((cl & 3) != 0) {
            if (lfo(pmd, di)) pmd->lfo_switch |= cl & 3;
        }
        if ((cl & 0x30) != 0) {
            lfo_change(di);
            if (lfo(pmd, di)) {
                lfo_change(di);
                pmd->lfo_switch |= di->lfoswi & 0x30;
            } else {
                lfo_change(di);
            }
        }
        if ((pmd->lfo_switch & 0x19) != 0) {
            if ((pmd->lfo_switch & 8) != 0) porta_calc(pmd, di);
            otodasi(pmd, di);
        }
        if ((pmd->lfo_switch & 0x22) != 0) goto vol_set;
    }
    if (pmd->fadeout_speed == 0) {
        mnp_ret(pmd, di);
        return si;
    }

vol_set:
    volset(pmd, di);

    mnp_ret(pmd, di);
    return si;

fmmain_nonplay:
    // :1271
    di->keyoff_flag = 0xff;
    di->leng--;
    if (di->leng != 0) {
        mnp_ret(pmd, di);
        return si;
    }

    if ((di->partmask & 2) != 0 && pmd->fm_effec_flag == 0) {
        di->partmask &= 0xfd;
        if (di->partmask == 0) goto mp10;
    }

fmmnp_1:
    al = lod8(&si);
    if (al == 0x80) goto fmmnp_2;
    if (al < 0x80) goto fmmnp_3;
    switch (commands(pmd, di, &si)) {
    case PMD_CMD_MP1: goto mp1;
    case PMD_CMD_FMMNP_1: goto fmmnp_1;
    case PMD_CMD_PORTA_RETURN: goto porta_return;
    default: break;
    }
    goto fmmnp_1;

fmmnp_2:
    // :1291
    si--;
    di->address = si;
    di->loopcheck = 3;
    di->onkai = 0xff;
    if (di->partloop == NULL) { fmmnp_4(pmd, di); return si; }
    si = di->partloop;
    di->loopcheck = 1;
    goto fmmnp_1;

fmmnp_3:
    // :1306 A rest.
    rest_note(pmd, di, &si);
    return si;
}

// :1306
// The rest-note path. The original shares it with every part loop by jumping
// into fmmain's fmmnp_3.
static void rest_note(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    di->fnum = 0;
    di->onkai = 0xff;
    di->onkai_def = 0xff;
    di->leng = lod8(si);
    di->keyon_flag++;
    di->address = *si;

    pmd->volpush_flag--;
    if (pmd->volpush_flag == 0) {
        fmmnp_4(pmd, di);
        return;
    }
    di->volpush = 0;

    pmd->tieflag = 0;
    pmd->volpush_flag = 0;
    mnp_ret(pmd, di);
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
static void mnp_ret(struct pmd *pmd, struct pmd_qq *di) {
    pmd->loop_work &= di->loopcheck;
    ppz_int_hook(pmd);
}

// :1215
// Computes the gate time from the Q/QM/QR commands.
static void calc_q(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    if (**si == 0xc1) {
        (*si)++; // slur
        di->qdat = 0;
        return;
    }

    uint8_t dl = di->qdata;
    if (di->qdatb != 0) {
        uint16_t product = (uint16_t)di->leng * di->qdatb;
        dl = (uint8_t)(dl + (product >> 8));
    }

    if (di->qdat3 != 0) {
        uint16_t max = (uint16_t)((di->qdat3 & 0x7f) + 1);
        uint8_t r = rnd(pmd, max);
        if ((di->qdat3 & 0x80) != 0) {
            if (dl >= r) {
                dl -= r;
            } else {
                dl = 0;
            }
        } else {
            dl = (uint8_t)(dl + r);
        }
    }

    if (di->qdat2 != 0) {
        uint8_t dh = (uint8_t)(di->leng - di->qdat2);
        if ((di->leng < di->qdat2)) {
            di->qdat = 0;
            return;
        }
        if (dl >= dh) dl = dh;
    }
    di->qdat = dl;
}

// :1318
static void fmmnp_4(struct pmd *pmd, struct pmd_qq *di) {
    pmd->tieflag = 0;
    pmd->volpush_flag = 0;
    mnp_ret(pmd, di);
}

// :1329
// The SSG part main loop.
static void psgmain(struct pmd *pmd, struct pmd_qq *di) {
    uint8_t *si = di->address;
    if (si == NULL) return;

    if (di->partmask != 0) goto psgmain_nonplay;

    di->leng--;
    uint8_t al = di->leng;

    if ((di->keyoff_flag & 3) == 0 && al <= di->qdat) {
        keyoffp(pmd, di);
        di->keyoff_flag = 0xff;
    }

    if (al != 0) goto mpexitp;
    di->lfoswi &= 0xf7;

mp1p:
    al = lod8(&si);
    if (al < 0x80) goto mp2p;
    if (al == 0x80) goto mp15p;

mp1cp:
    switch (commandsp(pmd, di, &si)) {
    case PMD_CMD_MP1P: goto mp1p;
    case PMD_CMD_PSGMNP_1: goto psgmnp_1;
    case PMD_CMD_PORTA_RETURNP: goto porta_returnp;
    default: break;
    }
    goto mp1p;

mp15p:
    si--;
    di->address = si;
    di->loopcheck = 3;
    di->onkai = 0xff;
    if (di->partloop == NULL) goto mpexitp;
    si = di->partloop;
    di->loopcheck = 1;
    goto mp1p;

mp2p:
    al = lfoinitp(pmd, di, al);
    al = oshift(di, al);
    fnumsetp(pmd, di, al);

    al = lod8(&si);
    di->leng = al;
    calc_q(pmd, di, &si);

porta_returnp:
    if (di->volpush == 0) goto mp_newp;
    if (di->onkai == 0xff) goto mp_newp;
    pmd->volpush_flag--;
    if (pmd->volpush_flag == 0) goto mp_newp;
    pmd->volpush_flag = 0;
    di->volpush = 0;

mp_newp:
    volsetp(pmd, di);
    otodasip(pmd, di);
    keyonp(pmd, di);
    di->keyon_flag++;

    di->address = si;
    pmd->tieflag = 0;
    pmd->volpush_flag = 0;
    di->keyoff_flag = 0;
    if (*si == 0xfb) di->keyoff_flag = 2;
    mnp_ret(pmd, di);
    return;

mpexitp:;
    // :1410
    uint8_t cl = di->lfoswi;
    al = cl & 8;
    pmd->lfo_switch = al;
    if (cl != 0) {
        if ((cl & 3) != 0) {
            if (lfo(pmd, di)) pmd->lfo_switch |= cl & 3;
        }
        if ((cl & 0x30) != 0) {
            lfo_change(di);
            if (lfo(pmd, di)) {
                lfo_change(di);
                pmd->lfo_switch |= di->lfoswi & 0x30;
            } else {
                lfo_change(di);
            }
        }
        if ((pmd->lfo_switch & 0x19) != 0) {
            if ((pmd->lfo_switch & 8) != 0) porta_calc(pmd, di);
            otodasip(pmd, di);
        }
    }

    if (!soft_env(pmd, di) && (pmd->lfo_switch & 0x22) == 0 &&
        pmd->fadeout_speed == 0) {
        mnp_ret(pmd, di);
    return;
    }
    volsetp(pmd, di);
    mnp_ret(pmd, di);
    return;

psgmain_nonplay:
    // :1463
    di->keyoff_flag = 0xff;
    di->leng--;
    if (di->leng != 0) {
        mnp_ret(pmd, di);
        return;
    }
    di->lfoswi &= 0xf7;

psgmnp_1:
    al = lod8(&si);
    if (al == 0x80) goto psgmnp_2;
    if (al < 0x80) goto psgmnp_4;
    if (al == 0xda && ssgdrum_check(pmd, di, al)) goto mp1cp;
    switch (commandsp(pmd, di, &si)) {
    case PMD_CMD_MP1P: goto mp1p;
    case PMD_CMD_PSGMNP_1: goto psgmnp_1;
    case PMD_CMD_PORTA_RETURNP: goto porta_returnp;
    default: break;
    }
    goto psgmnp_1;

psgmnp_2:
    si--;
    di->address = si;
    di->loopcheck = 3;
    di->onkai = 0xff;
    if (di->partloop == NULL) { fmmnp_4(pmd, di); return; }
    si = di->partloop;
    di->loopcheck = 1;
    goto psgmnp_1;

psgmnp_4:
    if (ssgdrum_check(pmd, di, al)) goto mp2p;
    di->fnum = 0;
    di->onkai = 0xff;
    di->onkai_def = 0xff;
    al = lod8(&si);
    di->leng = al;
    di->keyon_flag++;
    di->address = si;
    pmd->volpush_flag--;
    if (pmd->volpush_flag != 0) di->volpush = 0;
    fmmnp_4(pmd, di);
}

// :1508
// Returns true when the SSG drum should be stopped and the SSG part revived.
static bool ssgdrum_check(struct pmd *pmd, struct pmd_qq *di, uint8_t al) {
    if ((di->partmask & 1) != 0) return false;
    if ((di->partmask & 2) == 0) return false;
    if (pmd->effon >= 2) return false;
    if ((al & 0x0f) == 0x0f) return false;
    if (pmd->effon == 1) effend(pmd);
    di->partmask &= 0xfd;
    if (di->partmask != 0) return false;
    return true;
}

// :1539
// The rhythm part main loop.
static void rhythmmain(struct pmd *pmd, struct pmd_qq *di) {
    uint8_t *si = di->address;
    uint8_t al;
    if (si == NULL) return;
    di->leng--;
    if (di->leng != 0) {
        mnp_ret(pmd, di);
        return;
    }

rhyms0:
    {
        uint8_t *bx = (uint8_t *)pmd->rhyadr;
rhyms00:
        al = *bx++;
        if (al == 0xff) goto reom;
        if ((al & 0x80) != 0) goto rhythmon;
        pmd->kshot_dat = 0;
rlnset:
        al = *bx++;
        pmd->rhyadr = bx;
        di->leng = al;
        di->keyon_flag++;
        { fmmnp_4(pmd, di); return; }

rhythmon:
        if ((al & 0x40) == 0) goto rhy_shot;
        uint8_t *save_si = si;
        si = bx;
        (void)commandsr(pmd, di, &si);
        bx = si;
        si = save_si;
        goto rhyms00;

rhy_shot:
        if (di->partmask == 0) goto r_nonmask;
        pmd->kshot_dat = 0;
        bx++;
        goto rlnset;

r_nonmask:
        {
            uint8_t ah = al;
            al = *bx++;
            uint16_t ax = (uint16_t)(((uint16_t)ah << 8) | al) & 0x3fff;
            pmd->kshot_dat = ax;
            if (ax == 0) goto rlnset;
            pmd->rhyadr = bx;

#if board2
            if (pmd->kp_rhythm_flag != 0) {
                uint16_t bits = ax;
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
                uint16_t bits = ax;
                uint8_t ch;
                if (pmd->fadeout_volume != 0) {
#if board2
                    if (pmd->kp_rhythm_flag != 0) volset2rf(pmd, pmd->rhyvol);
#endif
                    if (pmd->ppsdrv_flag == 0) goto roret;
                }
                ch = 0xff;
rolop:
                ch++;
                if (bits & 1) goto rhygo;
                bits >>= 1;
                goto rolop;
rhygo:
                effgo(pmd, di, ch);
                if (pmd->ppsdrv_flag == 0) goto roret;
                bits >>= 1; // the scanned instrument is consumed
                if (bits == 0) goto roret;
                goto rolop;
            }
roret:
            bx = (uint8_t *)pmd->rhyadr;
            goto rlnset;
        }
    }

reom:
    al = lod8(&si);
    if (al == 0x80) goto rfin;
    if (al < 0x80) goto re00;
    (void)commandsr(pmd, di, &si);
    goto reom;

re00:
    di->address = si;
    {
        uint16_t index = (uint16_t)((uint16_t)al * 2);
        uint16_t offset = (uint16_t)(pmd->radtbl[index] |
                                     (pmd->radtbl[index + 1] << 8));
        pmd->rhyadr = pmd->mmlbuf + offset;
        goto rhyms0;
    }

rfin:
    si--;
    di->address = si;
    di->loopcheck = 3;
    if (di->partloop != NULL) {
        si = di->partloop;
        di->loopcheck = 1;
        goto reom;
    }
    pmd->rhyadr = &pmd->rhydmy;
    { fmmnp_4(pmd, di); return; }
}

// :1713
static enum pmd_cmd_result commands(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    return command00(pmd, di, si, cmdtbl);
}

// :1717
static enum pmd_cmd_result commandsr(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    return command00(pmd, di, si, cmdtblr);
}

// :1721
static enum pmd_cmd_result commandsp(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    return command00(pmd, di, si, cmdtblp);
}

// :1724
static enum pmd_cmd_result command00(struct pmd *pmd, struct pmd_qq *di, uint8_t **si, const pmd_cmd_fn *table) {
    uint8_t al = *(*si - 1);
    if (al < 0xb1) {
        // :1739 The original rewinds SI and writes an end marker.
        (*si)--;
        **si = 0x80;
        return PMD_CMD_RETURN;
    }
    return table[0xff - al](pmd, di, si);
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
static enum pmd_cmd_result jump16(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    (void)di;
    *si += 16;
    return PMD_CMD_RETURN;
}

// :2037
static enum pmd_cmd_result jump6(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    (void)di;
    *si += 6;
    return PMD_CMD_RETURN;
}

// :2039
static enum pmd_cmd_result jump5(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    (void)di;
    *si += 5;
    return PMD_CMD_RETURN;
}

// :2041
static enum pmd_cmd_result jump4(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    (void)di;
    *si += 4;
    return PMD_CMD_RETURN;
}

// :2043
static enum pmd_cmd_result jump3(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    (void)di;
    *si += 3;
    return PMD_CMD_RETURN;
}

// :2045
static enum pmd_cmd_result jump2(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    (void)di;
    *si += 2;
    return PMD_CMD_RETURN;
}

// :2047
static enum pmd_cmd_result jump1(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    (void)di;
    (*si)++;
    return PMD_CMD_RETURN;
}

// :2049
static enum pmd_cmd_result jump0(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    (void)di;
    (void)si;
    return PMD_CMD_RETURN;
}

// :2055
static enum pmd_cmd_result special_0c0h(struct pmd *pmd, struct pmd_qq *di, uint8_t **si, uint8_t al) {
    // The original's f6/f5 entries are unreachable because values below f7
    // are rejected above; they are kept to mirror the original table.
    static const pmd_cmd_fn table[0xff - 0xf5 + 1] = {
        vd_fm, _vd_fm, vd_ssg, _vd_ssg, vd_pcm,
        _vd_pcm, vd_rhythm, _vd_rhythm, pmd86_s, vd_ppz, _vd_ppz,
    };
    if (al < 0xf7) {
        (*si)--;
        **si = 0x80;
        return PMD_CMD_RETURN;
    }
    return table[0xff - al](pmd, di, si);
}

// :2082
static enum pmd_cmd_result pmd86_s(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)di;
    pmd->pcm86_vol = lod8(si) & 1;
    return PMD_CMD_RETURN;
}

// :2091
static enum pmd_cmd_result vd_fm(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    return vd_main(pmd, di, si, &pmd->fm_voldown, &pmd->_fm_voldown, false);
}

// :2092
static enum pmd_cmd_result vd_main(struct pmd *pmd, struct pmd_qq *di, uint8_t **si, uint8_t *vol, uint8_t *base, bool add) {
    (void)pmd;
    (void)di;
    uint8_t al = lod8(si);
    if (!add) {
        *vol = al;
        return PMD_CMD_RETURN;
    }
    if (al == 0) {
        *vol = *base;
        return PMD_CMD_RETURN;
    }
    if ((al & 0x80) != 0) {
        unsigned sum = (unsigned)*vol + al;
        if (sum > 0xff) *vol = (uint8_t)sum;
        else *vol = 0;
    } else {
        unsigned sum = (unsigned)*vol + al;
        *vol = sum > 0xff ? 0xff : (uint8_t)sum;
    }
    return PMD_CMD_RETURN;
}

// :2097
static enum pmd_cmd_result vd_ssg(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    return vd_main(pmd, di, si, &pmd->ssg_voldown, &pmd->_ssg_voldown, false);
}

// :2100
static enum pmd_cmd_result vd_pcm(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    return vd_main(pmd, di, si, &pmd->pcm_voldown, &pmd->_pcm_voldown, false);
}

// :2103
static enum pmd_cmd_result vd_rhythm(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    return vd_main(pmd, di, si, &pmd->rhythm_voldown, &pmd->_rhythm_voldown,
                   false);
}

// :2107
static enum pmd_cmd_result vd_ppz(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    return vd_main(pmd, di, si, &pmd->ppz_voldown, &pmd->_ppz_voldown, false);
}

// :2111
static enum pmd_cmd_result _vd_fm(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    return vd_main(pmd, di, si, &pmd->fm_voldown, &pmd->_fm_voldown, true);
}

// :2136
static enum pmd_cmd_result _vd_ssg(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    return vd_main(pmd, di, si, &pmd->ssg_voldown, &pmd->_ssg_voldown, true);
}

// :2141
static enum pmd_cmd_result _vd_pcm(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    return vd_main(pmd, di, si, &pmd->pcm_voldown, &pmd->_pcm_voldown, true);
}

// :2146
static enum pmd_cmd_result _vd_rhythm(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    return vd_main(pmd, di, si, &pmd->rhythm_voldown, &pmd->_rhythm_voldown,
                   true);
}

// :2151
static enum pmd_cmd_result _vd_ppz(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    return vd_main(pmd, di, si, &pmd->ppz_voldown, &pmd->_ppz_voldown, true);
}

// :2159
static enum pmd_cmd_result slot_delay(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    uint8_t al = lod8(si);
    di->sdelay_m = (uint8_t)((~(al & 0x0f) & 0x0f) << 4);
    al = lod8(si);
    di->sdelay = al;
    di->sdelay_c = al;
    return PMD_CMD_RETURN;
}

// :2176
static enum pmd_cmd_result fb_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint8_t al = lod8(si);
    if ((al & 0x80) != 0) {
        _fb_set(pmd, di, al);
        return PMD_CMD_RETURN;
    }
    al = (uint8_t)((al << 3) | (al >> 5)); // rol al,3
    fb_set3(pmd, di, al);
    return PMD_CMD_RETURN;
}

// :2186
static void fb_set3(struct pmd *pmd, struct pmd_qq *di, uint8_t al) {
    uint8_t dh = (uint8_t)(0xb0 - 1 + pmd->partb);
    uint8_t dl;
    if (fm3_active(pmd, di)) {
        if ((di->slotmask & 0x10) == 0) return;
        dl = (uint8_t)((pmd->fm3_alg_fb & 7) | al);
        pmd->fm3_alg_fb = dl;
    } else {
        dl = (uint8_t)((di->alg_fb & 7) | al);
    }
    opnset(pmd, (uint16_t)((dh << 8) | dl));
    di->alg_fb = dl;
}

// :2213
static void _fb_set(struct pmd *pmd, struct pmd_qq *di, uint8_t al) {
    if ((al & 0x40) == 0) al &= 7;

    uint8_t dl;
    if (fm3_active(pmd, di)) dl = pmd->fm3_alg_fb;
    else dl = di->alg_fb;
    dl = (uint8_t)(((dl >> 3) | (dl << 5)) & 7); // ror dl,3
    al = (uint8_t)(al + dl);
    if ((al & 0x80) != 0) {
        al = 0;
    } else if (al >= 8) {
        al = 0x38;
    } else {
        al = (uint8_t)((al << 3) | (al >> 5)); // fb_set2's rol al,3
    }
    fb_set3(pmd, di, al);
}

// :2248
static enum pmd_cmd_result tl_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint8_t dh = (uint8_t)(0x40 - 1 + pmd->partb);
    uint8_t al = lod8(si);
    uint8_t ah = al & 0x0f;
    uint8_t ch = (uint8_t)(((di->slotmask >> 4) | (di->slotmask << 4)));
    ah &= ch;
    uint8_t dl = lod8(si);
    bool write = di->partmask == 0;

    if ((al & 0x80) == 0) dl &= 0x7f;

    uint8_t *slot[4] = {&di->slot1, &di->slot2, &di->slot3, &di->slot4};
    uint8_t regs[4] = {dh, (uint8_t)(dh + 8), (uint8_t)(dh + 4),
                       (uint8_t)(dh + 12)};
    for (int i = 0; i < 4; i++) {
        if ((ah & (1u << i)) == 0) continue;
        uint8_t value;
        if ((al & 0x80) == 0) {
            value = dl;
        } else {
            value = (uint8_t)(*slot[i] + dl);
            if ((value & 0x80) != 0) {
                if ((dl & 0x80) != 0) value = 0;
                else value = 127;
            }
        }
        *slot[i] = value;
        if (write) opnset(pmd, (uint16_t)((regs[i] << 8) | value));
    }
    return PMD_CMD_RETURN;
}

// :2357
static enum pmd_cmd_result fm_mml_part_mask(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint8_t al = lod8(si);
    if (al >= 2) return special_0c0h(pmd, di, si, al);
    if (al == 0) {
        di->partmask &= 0xbf;
        if (di->partmask != 0) return PMD_CMD_FMMNP_1;
        neiro_reset(pmd, di);
        return PMD_CMD_MP1;
    }
    di->partmask |= 0x40;
    if (di->partmask == 0x40) silence_fmpart(pmd, di);
    return PMD_CMD_FMMNP_1;
}

// :2378
static enum pmd_cmd_result ssg_mml_part_mask(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint8_t al = lod8(si);
    if (al >= 2) return special_0c0h(pmd, di, si, al);
    if (al == 0) {
        di->partmask &= 0xbf;
        if (di->partmask != 0) return PMD_CMD_PSGMNP_1;
        return PMD_CMD_MP1P;
    }
    di->partmask |= 0x40;
    if (di->partmask == 0x40) {
        uint8_t read, mask;
        psgmsk(pmd, &read, &mask);
        uint8_t dl = (uint8_t)(read | mask);
        opnset44(pmd, (uint16_t)((7 << 8) | dl));
    }
    return PMD_CMD_PSGMNP_1;
}

// :2402
static enum pmd_cmd_result rhythm_mml_part_mask(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint8_t al = lod8(si);
    if (al >= 2) return special_0c0h(pmd, di, si, al);
    if (al == 0) di->partmask &= 0xbf;
    else di->partmask |= 0x40;
    return PMD_CMD_RETURN;
}

// :2417
static void neiro_reset(struct pmd *pmd, struct pmd_qq *di) {
    if (di->neiromask == 0) return;

    uint8_t dl = di->voicenum;
    uint16_t slot1 = (uint16_t)(di->slot1 | (di->slot3 << 8));
    uint16_t slot2 = (uint16_t)(di->slot2 | (di->slot4 << 8));
    pmd->af_check = 1;
    neiroset(pmd, di, dl);
    pmd->af_check = 0;
    di->slot1 = (uint8_t)slot1;
    di->slot3 = (uint8_t)(slot1 >> 8);
    di->slot2 = (uint8_t)slot2;
    di->slot4 = (uint8_t)(slot2 >> 8);

    uint8_t al = (uint8_t)(~di->carrier & di->slotmask);
    if (al == 0) goto nr_exit;

    {
        uint8_t dh = (uint8_t)(0x4c - 1 + pmd->partb);
        al = (uint8_t)((al << 1) | (al >> 7)); // rol al,1
        if (al & 1) opnset(pmd, (uint16_t)((dh << 8) | di->slot4));
        dh -= 8;
        al = (uint8_t)((al << 1) | (al >> 7));
        if (al & 1) opnset(pmd, (uint16_t)((dh << 8) | di->slot3));
        dh += 4;
        al = (uint8_t)((al << 1) | (al >> 7));
        if (al & 1) opnset(pmd, (uint16_t)((dh << 8) | di->slot2));
        dh -= 8;
        al = (uint8_t)((al << 1) | (al >> 7));
        if (al & 1) opnset(pmd, (uint16_t)((dh << 8) | di->slot1));
    }

nr_exit:;
#if board2
    {
        uint8_t dh = (uint8_t)(pmd->partb + 0xb3);
        uint8_t pan;
        calc_panout(pmd, di, &pan);
        opnset(pmd, (uint16_t)((dh << 8) | pan));
    }
#endif
}

// :2478
static enum pmd_cmd_result pdrswitch(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)di;
    uint8_t al = lod8(si);
    if (pmd->ppsdrv_flag != 0 && pmd->pps != NULL &&
        pmd->pps->pdr_switch != NULL) {
        pmd->pps->pdr_switch(al, pmd->user_data);
    }
    return PMD_CMD_RETURN;
}

// :2493
static enum pmd_cmd_result volmask_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint8_t al = lod8(si) & 0x0f;
    if (al == 0) {
        al = di->carrier;
    } else {
        al = (uint8_t)((al << 4) | (al >> 4));
        al |= 0x0f;
    }
    di->volmask = al;
    ch3_setting(pmd, di);
    return PMD_CMD_RETURN;
}

// :2510
static enum pmd_cmd_result _volmask_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint8_t al = lod8(si) & 0x0f;
    if (al == 0) {
        al = di->carrier;
    } else {
        al = (uint8_t)((al << 4) | (al >> 4));
        al |= 0x0f;
    }
    di->_volmask = al;
    ch3_setting(pmd, di);
    return PMD_CMD_RETURN;
}

// :2528
static bool ch3_setting(struct pmd *pmd, struct pmd_qq *di) {
    if (!fm3_active(pmd, di)) return false;
    ch3mode_set(pmd, di);
    return true;
}

// :2549
static enum pmd_cmd_result fm3_extpartset(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)di;
    static const int slots[3] = {part3b, part3c, part3d};
    for (int i = 0; i < 3; i++) {
        uint16_t offset = lod16(si);
        if (offset == 0) continue;
        fm3_partinit(pmd, &pmd->parts[slots[i]], pmd->mmlbuf + offset);
    }
    return PMD_CMD_RETURN;
}

// :2576
static void fm3_partinit(struct pmd *pmd, struct pmd_qq *di, uint8_t *address) {
    di->address = address;
    di->leng = 1;
    di->keyoff_flag = 0xff;
    di->mdc = 0xff;
    di->mdc2 = 0xff;
    di->_mdc = 0xff;
    di->_mdc2 = 0xff;
    di->onkai = 0xff;
    di->onkai_def = 0xff;
    di->volume = 108;
    di->fmpan = pmd->parts[part3].fmpan;
    di->partmask |= 0x20;
}

// :2597
static enum pmd_cmd_result detune_extend(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    uint8_t al = lod8(si) & 1;
    di->extendmode = (uint8_t)((di->extendmode & 0xfe) | al);
    return PMD_CMD_RETURN;
}

// :2607
static enum pmd_cmd_result lfo_extend(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    uint8_t al = (uint8_t)((lod8(si) & 1) << 1);
    di->extendmode = (uint8_t)((di->extendmode & 0xfd) | al);
    return PMD_CMD_RETURN;
}

// :2618
static enum pmd_cmd_result envelope_extend(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    uint8_t al = (uint8_t)((lod8(si) & 1) << 2);
    di->extendmode = (uint8_t)((di->extendmode & 0xfb) | al);
    return PMD_CMD_RETURN;
}

// :2630
static enum pmd_cmd_result lfowave_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    di->lfo_wave = lod8(si);
    return PMD_CMD_RETURN;
}

// :2638
static enum pmd_cmd_result extend_psgenvset(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    di->eenv_ar = lod8(si) & 0x1f;
    di->eenv_dr = lod8(si) & 0x1f;
    di->eenv_sr = lod8(si) & 0x1f;
    uint8_t ah = lod8(si);
    di->eenv_rr = ah & 0x0f;
    ah = (uint8_t)((ah << 4) | (ah >> 4)); // rol ah,4
    ah &= 0x0f;
    di->eenv_sl = (uint8_t)(~ah & 0x0f);
    di->eenv_al = lod8(si) & 0x0f;

    if (di->envf != 0xff) {
        di->envf = 0xff;
        di->eenv_count = 4;
        di->eenv_volume = 0;
    }
    return PMD_CMD_RETURN;
}

// :2674
static enum pmd_cmd_result slotdetune_set2(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    if (!fm3_active(pmd, di)) {
        *si += 3;
        return PMD_CMD_RETURN;
    }
    uint8_t bl = lod8(si);
    uint16_t ax = lod16(si);
    if (bl & 1) pmd->slot_detune1 = (uint16_t)(pmd->slot_detune1 + ax);
    if (bl & 2) pmd->slot_detune2 = (uint16_t)(pmd->slot_detune2 + ax);
    if (bl & 4) pmd->slot_detune3 = (uint16_t)(pmd->slot_detune3 + ax);
    if (bl & 8) pmd->slot_detune4 = (uint16_t)(pmd->slot_detune4 + ax);
    pmd->slotdetune_flag =
        (pmd->slot_detune1 | pmd->slot_detune2 | pmd->slot_detune3 |
         pmd->slot_detune4) != 0;
    return PMD_CMD_RETURN;
}

// :2704
static enum pmd_cmd_result slotdetune_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    if (!fm3_active(pmd, di)) {
        *si += 3;
        return PMD_CMD_RETURN;
    }
    uint8_t bl = lod8(si);
    uint16_t ax = lod16(si);
    if (bl & 1) pmd->slot_detune1 = ax;
    if (bl & 2) pmd->slot_detune2 = ax;
    if (bl & 4) pmd->slot_detune3 = ax;
    if (bl & 8) pmd->slot_detune4 = ax;
    pmd->slotdetune_flag =
        (pmd->slot_detune1 | pmd->slot_detune2 | pmd->slot_detune3 |
         pmd->slot_detune4) != 0;
    return PMD_CMD_RETURN;
}

// :2745
static void ch3mode_set(struct pmd *pmd, struct pmd_qq *di) {
    uint8_t al;
    if (di == &pmd->parts[part3]) al = 1;
    else if (di == &pmd->parts[part3b]) al = 2;
    else if (di == &pmd->parts[part3c]) al = 4;
    else al = 8;

    uint8_t ah;
    if ((di->slotmask & 0xf0) == 0) goto cm_clear;
    if ((di->slotmask & 0xf0) != 0xf0) goto cm_set;
    if ((di->volmask & 0x0f) == 0) goto cm_clear;
    if ((di->lfoswi & 1) != 0) goto cm_set;
    if ((di->_volmask & 0x0f) == 0) goto cm_clear;
    if ((di->lfoswi & 0x10) != 0) goto cm_set;

cm_clear:
    al = (uint8_t)~al;
    pmd->slot3_flag &= al;
    if (pmd->slot3_flag != 0) goto cm_set2;
    if (pmd->slotdetune_flag == 1) goto cm_set2;
    ah = 0x3f;
    goto cm_set_main;

cm_set:
    pmd->slot3_flag |= al;
cm_set2:
    ah = 0x7f;

cm_set_main:
#if !board2
    if ((di->partmask & 2) != 0) {
        pmd->ch3mode_push = ah;
        return;
    }
#endif
    if (ah == pmd->ch3mode) return;
    pmd->ch3mode = ah;
    opnset44(pmd, (uint16_t)((0x27 << 8) | (ah & 0xcf)));

    if (ah == 0x3f) return;
    if (di == &pmd->parts[part3]) return;
    otodasi_cm(pmd, &pmd->parts[part3]);
    if (di == &pmd->parts[part3b]) return;
    otodasi_cm(pmd, &pmd->parts[part3b]);
    if (di == &pmd->parts[part3c]) return;
    otodasi_cm(pmd, &pmd->parts[part3c]);
}

// :2823
static void otodasi_cm(struct pmd *pmd, struct pmd_qq *di) {
    if (di->partmask != 0) return;
    otodasi(pmd, di);
}

// :2839
static enum pmd_cmd_result slotmask_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint8_t al = lod8(si);
    uint8_t ah = al;
    al &= 0x0f;
    if (al != 0) {
        di->carrier = (uint8_t)((al << 4) | (al >> 4));
        goto sm_set;
    }

    uint8_t bl;
    if (fm3_active(pmd, di)) {
        bl = pmd->fm3_alg_fb;
    } else {
        uint8_t *bx;
        toneadr_calc(pmd, di, di->voicenum, &bx);
        bl = bx[24];
    }
    bl &= 7;
    di->carrier = carrier_table[bl];

sm_set:
    ah &= 0xf0;
    if (di->slotmask == ah) return PMD_CMD_RETURN;
    di->slotmask = ah;
    if ((ah & 0xf0) != 0) di->partmask &= 0xdf;
    else di->partmask |= 0x20;

    if (ch3_setting(pmd, di)) {
        if (di == &pmd->parts[part3]) goto sm_exit;
        keyon_sm(pmd, &pmd->parts[part3]);
        if (di == &pmd->parts[part3b]) goto sm_exit;
        keyon_sm(pmd, &pmd->parts[part3b]);
        if (di == &pmd->parts[part3c]) goto sm_exit;
        keyon_sm(pmd, &pmd->parts[part3c]);
    }
sm_exit:

    ah = 0;
    al = di->slotmask;
    if (al & 0x80) ah |= 0x11;
    if (al & 0x40) ah |= 0x44;
    if (al & 0x20) ah |= 0x22;
    if (al & 0x10) ah |= 0x88;
    di->neiromask = ah;

    if (di->partmask == 0) return PMD_CMD_MP1;
    return PMD_CMD_FMMNP_1;
}

// :2945
static void keyon_sm(struct pmd *pmd, struct pmd_qq *di) {
    if (di->partmask != 0) return;
    if ((di->keyoff_flag & 1) != 0) return;
    keyon(pmd, di);
}

// :2957
static enum pmd_cmd_result ssg_efct_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint8_t al = lod8(si);
    if (di->partmask != 0) return PMD_CMD_RETURN;
    if (al != 0) {
        eff_on2(pmd, di, al);
    } else {
        effend(pmd);
    }
    return PMD_CMD_RETURN;
}

// :2981
static enum pmd_cmd_result fm_efct_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint8_t al = lod8(si);
    if (di->partmask != 0) return PMD_CMD_RETURN;
#if board2
    uint8_t fmsel = pmd->fmsel;
#endif
    uint8_t partb = pmd->partb;
    if (al != 0) {
        fm_effect_on(pmd, al);
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
static enum pmd_cmd_result fade_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)di;
    pmd->fadeout_flag = 1;
    pmd->fadeout_speed = lod8(si); // fout
    return PMD_CMD_RETURN;
}

// :3037
static enum pmd_cmd_result mdepth_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    uint8_t al = lod8(si);
    di->mdspd = al;
    di->mdspd2 = al;
    di->mdepth = lod8(si);
    return PMD_CMD_RETURN;
}

// :3045
static enum pmd_cmd_result mdepth_count(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    uint8_t al = lod8(si);
    if ((al & 0x80) != 0) {
        al &= 0x7f;
        if (al == 0) al--;
        di->_mdc = al;
        di->_mdc2 = al;
    } else {
        if (al == 0) al--;
        di->mdc = al;
        di->mdc2 = al;
    }
    return PMD_CMD_RETURN;
}

// :3067
static void porta_calc(struct pmd *pmd, struct pmd_qq *di) {
    (void)pmd;
    di->porta_num = (uint16_t)(di->porta_num + di->porta_num2);
    if (di->porta_num3 == 0) return;
    if (di->porta_num3 < 0) {
        di->porta_num3++;
        di->porta_num--;
    } else {
        di->porta_num3--;
        di->porta_num++;
    }
}

// :3085
static enum pmd_cmd_result porta(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    if (di->partmask != 0) {
        lod8(si);
        return PMD_CMD_RETURN;
    }
    uint8_t al = lod8(si);
    al = lfoinit(pmd, di, al);
    al = oshift(di, al);
    fnumset(pmd, di, al);

    uint16_t fnum = di->fnum;
    uint8_t onkai = di->onkai;

    al = lod8(si);
    al = oshift(di, al);
    fnumset(pmd, di, al);
    uint16_t bx = di->fnum;

    di->onkai = onkai;
    di->fnum = fnum;

    // The block bits sit in the high byte of the f-number; BH/CH alias them.
    uint16_t cx = fnum;
    uint16_t ax = 0;
    uint8_t bh = (uint8_t)((bx >> 8) & 0x38);
    uint8_t ch = (uint8_t)((cx >> 8) & 0x38);
    uint8_t diff = (uint8_t)(bh - ch);
    if (diff != 0) {
        // Both are 0x38-masked, so the original's three sar steps are an exact
        // division.
        int8_t octaves = (int8_t)((int8_t)diff / 8);
        ax = (uint16_t)(int16_t)octaves;
        ax = (uint16_t)((int16_t)ax * 0x26a);
    }
    cx &= 0x7ff;
    bx &= 0x7ff;
    bx = (uint16_t)(bx - cx);
    ax = (uint16_t)(ax + bx);

    di->leng = lod8(si);
    calc_q(pmd, di, si);

    {
        int32_t n = (int16_t)ax;
        int32_t d = di->leng;
        di->porta_num2 = (int16_t)(n / d);
        di->porta_num3 = (int16_t)(n % d);
    }
    di->lfoswi |= 8;
    return PMD_CMD_PORTA_RETURN;
}

// :3155
static enum pmd_cmd_result portap(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    if (di->partmask != 0) {
        lod8(si);
        return PMD_CMD_RETURN;
    }
    uint8_t al = lod8(si);
    al = lfoinitp(pmd, di, al);
    al = oshift(di, al);
    fnumsetp(pmd, di, al);

    uint16_t fnum = di->fnum;
    uint8_t onkai = di->onkai;

    al = lod8(si);
    al = oshift(di, al);
    fnumsetp(pmd, di, al);
    uint16_t ax = di->fnum;

    di->onkai = onkai;
    di->fnum = fnum;
    ax = (uint16_t)(ax - fnum);

    di->leng = lod8(si);
    calc_q(pmd, di, si);

    {
        int32_t n = (int16_t)ax;
        int32_t d = di->leng;
        di->porta_num2 = (int16_t)(n / d);
        di->porta_num3 = (int16_t)(n % d);
    }
    di->lfoswi |= 8;
    return PMD_CMD_PORTA_RETURNP;
}

// :3200
static enum pmd_cmd_result status_write(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)di;
    pmd->status = lod8(si);
    return PMD_CMD_RETURN;
}

// :3208
static enum pmd_cmd_result status_add(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)di;
    pmd->status = (uint8_t)(pmd->status + lod8(si));
    return PMD_CMD_RETURN;
}

// :3218
static enum pmd_cmd_result vol_one_up_fm(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    uint8_t al = (uint8_t)(lod8(si) + di->volume);
    if (al >= 128) al = 127;
    di->volpush = (uint8_t)(al + 1);
    pmd->volpush_flag = 1;
    return PMD_CMD_RETURN;
}

// :3229
static enum pmd_cmd_result vol_one_up_psg(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    uint8_t al = (uint8_t)(lod8(si) + di->volume);
    if (al >= 16) al = 15;
    di->volpush = (uint8_t)(al + 1);
    pmd->volpush_flag = 1;
    return PMD_CMD_RETURN;
}

// :3237
static PMDC_UNUSED enum pmd_cmd_result vol_one_up_pcm(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    unsigned sum = (unsigned)lod8(si) + di->volume;
    uint8_t al;
    if (sum > 0xff) al = 254;
    else if (sum < 255) al = (uint8_t)sum;
    else al = 254;
    di->volpush = (uint8_t)(al + 1);
    pmd->volpush_flag = 1;
    return PMD_CMD_RETURN;
}

// :3248
static enum pmd_cmd_result vol_one_down(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    uint8_t ah = lod8(si);
    uint8_t al = di->volume;
    if (al < ah) al = 0;
    else if (al - ah >= 255) al = 254;
    else al = (uint8_t)(al - ah);
    di->volpush = (uint8_t)(al + 1);
    pmd->volpush_flag = 1;
    return PMD_CMD_RETURN;
}

// :3260
static enum pmd_cmd_result hlfo_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint8_t al = lod8(si);
#if board2
    di->fmpan = (uint8_t)((di->fmpan & 0xc0) | al);
    if (fm3_active(pmd, di)) {
        pmd->parts[part3].fmpan = di->fmpan;
        pmd->parts[part3b].fmpan = di->fmpan;
        pmd->parts[part3c].fmpan = di->fmpan;
        pmd->parts[part3d].fmpan = di->fmpan;
    }
    if (di->partmask == 0) {
        uint8_t pan;
        calc_panout(pmd, di, &pan);
        opnset(pmd, (uint16_t)(((pmd->partb + 0xb3) << 8) | pan));
    }
#else
    (void)pmd;
    (void)di;
    (void)al;
#endif
    return PMD_CMD_RETURN;
}

// :3304
static enum pmd_cmd_result hlfo_onoff(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint8_t al = lod8(si);
#if board2
    pmd->port22h = al;
    opnset44(pmd, (uint16_t)(0x2200 | al));
#else
    (void)pmd;
#endif
    (void)pmd;
    (void)al;
    (void)di;
    return PMD_CMD_RETURN;
}

// :3318
static enum pmd_cmd_result hlfo_delay(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint8_t al = lod8(si);
#if board2
    di->hldelay = al;
#else
    (void)pmd;
#endif
    (void)pmd;
    (void)al;
    (void)di;
    return PMD_CMD_RETURN;
}

// :3328
static enum pmd_cmd_result syousetu_lng_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)di;
    pmd->syousetu_lng = lod8(si);
    return PMD_CMD_RETURN;
}

// :3334
static enum pmd_cmd_result com_at(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint8_t al = lod8(si);
    di->voicenum = al;
    uint8_t dl = al;
    if (di->partmask == 0) {
        neiroset(pmd, di, dl);
        return PMD_CMD_RETURN;
    }

    uint8_t *bx;
    toneadr_calc(pmd, di, dl, &bx);
    dl = bx[24];
    di->alg_fb = dl;
    bx += 4;
    neiroset_tl(di, bx);

    if (!fm3_active(pmd, di) || di->neiromask == 0) return PMD_CMD_RETURN;

    if ((di->slotmask & 0x10) != 0) {
        // BUG: the original leaves AL holding the voice number here and stores
        // it as alg_fb. Preserved because it is observable in part state.
        di->alg_fb = di->voicenum;
    } else {
        uint8_t tmp = (uint8_t)(pmd->fm3_alg_fb & 0x38);
        dl = (uint8_t)((dl & 7) | tmp);
        di->alg_fb = tmp;
    }
    pmd->fm3_alg_fb = dl;
    return PMD_CMD_RETURN;
}

// :3381
static enum pmd_cmd_result comq(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    di->qdata = lod8(si);
    di->qdat3 = 0;
    return PMD_CMD_RETURN;
}

// :3386
static enum pmd_cmd_result comq3(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    di->qdat2 = lod8(si);
    return PMD_CMD_RETURN;
}

// :3390
static enum pmd_cmd_result comq4(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    di->qdat3 = lod8(si);
    return PMD_CMD_RETURN;
}

// :3398
static enum pmd_cmd_result comq2(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    di->qdatb = lod8(si);
    return PMD_CMD_RETURN;
}

// :3406
static enum pmd_cmd_result comv(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    di->volume = lod8(si);
    return PMD_CMD_RETURN;
}

// :3417
static enum pmd_cmd_result comt(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)di;
    uint8_t cmd = lod8(si);
    if (cmd < 251) {
        pmd->tempo_d = cmd;
        pmd->tempo_d_push = cmd;
        calc_tb_tempo(pmd);
        return PMD_CMD_RETURN;
    }

    uint8_t a = (uint8_t)(cmd + 1);
    if (a == 0) {
        uint8_t al = lod8(si);
        if (al < 0x18) al = 0x18;
        pmd->tempo_48 = al;
        pmd->tempo_48_push = al;
        calc_tempo_tb(pmd);
        return PMD_CMD_RETURN;
    }

    {
        uint8_t b = (uint8_t)(a + 1);
        uint8_t param = lod8(si);
        uint8_t al = param;
        if (b == 0) {
            // T±: relative tempo_d.
            uint8_t ah = pmd->tempo_d_push;
            unsigned sum = (unsigned)ah + al;
            if ((param & 0x80) != 0) al = sum > 0xff ? (uint8_t)sum : 0;
            else al = sum > 0xff ? 250 : (uint8_t)sum;
            if (al >= 251) al = 250;
            pmd->tempo_d = al;
            pmd->tempo_d_push = al;
            calc_tb_tempo(pmd);
            return PMD_CMD_RETURN;
        }

        // t±: relative tempo_48.
        uint8_t ah = pmd->tempo_48_push;
        unsigned sum = (unsigned)ah + al;
        if ((param & 0x80) != 0) {
            al = sum > 0xff ? (uint8_t)sum : 0x18;
            if (al < 0x18) al = 0x18;
        } else {
            al = sum > 0xff ? 255 : (uint8_t)sum;
        }
        pmd->tempo_48 = al;
        pmd->tempo_48_push = al;
        calc_tempo_tb(pmd);
        return PMD_CMD_RETURN;
    }
}

// :3480
static void calc_tb_tempo(struct pmd *pmd) {
    uint8_t bl = (uint8_t)(0 - pmd->tempo_d);
    uint8_t al = 255;
    if (bl >= 18) {
        uint8_t quotient = (uint8_t)(0x112c / bl);
        uint8_t remainder = (uint8_t)(0x112c % bl);
        al = quotient;
        if ((remainder & 0x80) != 0) al++;
    }
    pmd->tempo_48 = al;
    pmd->tempo_48_push = al;
}

// :3502
static void calc_tempo_tb(struct pmd *pmd) {
    uint8_t bl = pmd->tempo_48;
    uint8_t al = 0;
    if (bl >= 18) {
        uint8_t quotient = (uint8_t)(0x112c / bl);
        uint8_t remainder = (uint8_t)(0x112c % bl);
        al = (uint8_t)(0 - quotient);
        if ((remainder & 0x80) != 0) al--;
    }
    pmd->tempo_d = al;
    pmd->tempo_d_push = al;
}

// :3524
static enum pmd_cmd_result comtie(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)di;
    (void)si;
    pmd->tieflag |= 1;
    return PMD_CMD_RETURN;
}

// :3531
static enum pmd_cmd_result comd(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    di->detune = (int16_t)lod16(si);
    return PMD_CMD_RETURN;
}

// :3538
static enum pmd_cmd_result comdd(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    di->detune = (int16_t)(di->detune + (int16_t)lod16(si));
    return PMD_CMD_RETURN;
}

// :3545
static enum pmd_cmd_result comstloop(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint16_t offset = lod16(si);
    uint8_t *base = di == &pmd->part_e ? pmd->efcdat : pmd->mmlbuf;
    base[offset + 1] = 0;
    return PMD_CMD_RETURN;
}

// :3561
static enum pmd_cmd_result comedloop(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint8_t al = lod8(si);
    if (al == 0) {
        (*si)++;
        di->loopcheck = 1;
        goto reloop;
    }
    uint8_t ah = al;
    (*si)[0]++;
    al = lod8(si);
    if (ah != al) goto reloop;
    *si += 2;
    return PMD_CMD_RETURN;

reloop:
    {
        uint16_t ax = lod16(si);
        ax = (uint16_t)(ax + 2);
        uint8_t *base = di == &pmd->part_e ? pmd->efcdat : pmd->mmlbuf;
        *si = base + ax;
    }
    return PMD_CMD_RETURN;
}

// :3590
static enum pmd_cmd_result comexloop(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint16_t offset = lod16(si);
    uint8_t *base = di == &pmd->part_e ? pmd->efcdat : pmd->mmlbuf;
    uint8_t *bx = base + offset;
    uint8_t dl = (uint8_t)(bx[0] - 1);
    bx++;
    if (dl == bx[0]) *si = bx + 3;
    return PMD_CMD_RETURN;
}

// :3613
static enum pmd_cmd_result comlopset(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    di->partloop = *si;
    return PMD_CMD_RETURN;
}

// :3620
static enum pmd_cmd_result comshift(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    di->shift = (int8_t)lod8(si);
    return PMD_CMD_RETURN;
}

// :3628
static enum pmd_cmd_result comshift2(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    di->shift = (int8_t)(di->shift + (int8_t)lod8(si));
    return PMD_CMD_RETURN;
}

// :3637
static enum pmd_cmd_result comshift_master(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    di->shift_def = (int8_t)lod8(si);
    return PMD_CMD_RETURN;
}

// :3646
static enum pmd_cmd_result comvolup(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    (void)si;
    uint8_t al = (uint8_t)(di->volume + 4);
    di->volume = al >= 128 ? 127 : al;
    return PMD_CMD_RETURN;
}

// :3657
static enum pmd_cmd_result comvolup2(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    uint8_t al = (uint8_t)(lod8(si) + di->volume);
    di->volume = al >= 128 ? 127 : al;
    return PMD_CMD_RETURN;
}

// :3663
static enum pmd_cmd_result comvolupp(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    (void)si;
    uint8_t al = (uint8_t)(di->volume + 1);
    di->volume = al >= 16 ? 15 : al;
    return PMD_CMD_RETURN;
}

// :3673
static enum pmd_cmd_result comvolupp2(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    uint8_t al = (uint8_t)(lod8(si) + di->volume);
    di->volume = al >= 16 ? 15 : al;
    return PMD_CMD_RETURN;
}

// :3682
static enum pmd_cmd_result comvoldown(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    (void)si;
    uint8_t al = di->volume;
    di->volume = al >= 4 ? (uint8_t)(al - 4) : 0;
    return PMD_CMD_RETURN;
}

// :3690
static enum pmd_cmd_result comvoldown2(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    uint8_t ah = lod8(si);
    di->volume = di->volume >= ah ? (uint8_t)(di->volume - ah) : 0;
    return PMD_CMD_RETURN;
}

// :3700
static enum pmd_cmd_result comvoldownp(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    (void)si;
    if (di->volume != 0) di->volume--;
    return PMD_CMD_RETURN;
}

// :3708
static enum pmd_cmd_result comvoldownp2(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    uint8_t ah = lod8(si);
    di->volume = di->volume >= ah ? (uint8_t)(di->volume - ah) : 0;
    return PMD_CMD_RETURN;
}

// :3720
static enum pmd_cmd_result _lfoset(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    lfo_change(di);
    enum pmd_cmd_result r = lfoset(pmd, di, si);
    lfo_change(di);
    return r;
}

// :3733
static enum pmd_cmd_result _mdepth_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    lfo_change(di);
    enum pmd_cmd_result r = mdepth_set(pmd, di, si);
    lfo_change(di);
    return r;
}

// :3737
static enum pmd_cmd_result _lfowave_set(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    lfo_change(di);
    enum pmd_cmd_result r = lfowave_set(pmd, di, si);
    lfo_change(di);
    return r;
}

// :3741
static enum pmd_cmd_result _lfo_extend(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    lfo_change(di);
    enum pmd_cmd_result r = lfo_extend(pmd, di, si);
    lfo_change(di);
    return r;
}

// :3745
static enum pmd_cmd_result _lfoset_delay(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    lfo_change(di);
    enum pmd_cmd_result r = lfoset_delay(pmd, di, si);
    lfo_change(di);
    return r;
}

// :3749
static enum pmd_cmd_result _lfoswitch(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    uint8_t al = (uint8_t)(lod8(si) & 7);
    al = (uint8_t)((al << 4) | (al >> 4));
    di->lfoswi = (uint8_t)((di->lfoswi & 0x8f) | al);
    lfo_change(di);
    lfoinit_main(di);
    lfo_change(di);
    return PMD_CMD_RETURN;
}

// :3762
static enum pmd_cmd_result _lfoswitch_f(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    enum pmd_cmd_result r = _lfoswitch(pmd, di, si);
    ch3_setting(pmd, di);
    return r;
}

// :3769
static void lfo_change(struct pmd_qq *di) {
    int16_t ax = di->lfodat;
    di->lfodat = di->_lfodat;
    di->_lfodat = ax;

    di->lfoswi = (uint8_t)((di->lfoswi >> 4) | (di->lfoswi << 4));
    di->extendmode = (uint8_t)((di->extendmode >> 4) |
                               (di->extendmode << 4));

    uint8_t t;
#define SWAP(a, b)                                                            \
    do {                                                                      \
        t = (a);                                                              \
        (a) = (b);                                                            \
        (b) = t;                                                              \
    } while (0)
    SWAP(di->delay, di->_delay);
    SWAP(di->speed, di->_speed);
    SWAP(di->step, di->_step);
    SWAP(di->time, di->_time);
    SWAP(di->delay2, di->_delay2);
    SWAP(di->speed2, di->_speed2);
    SWAP(di->step2, di->_step2);
    SWAP(di->time2, di->_time2);
    SWAP(di->mdepth, di->_mdepth);
    SWAP(di->mdspd, di->_mdspd);
    SWAP(di->mdspd2, di->_mdspd2);
    SWAP(di->lfo_wave, di->_lfo_wave);
    SWAP(di->mdc, di->_mdc);
    SWAP(di->mdc2, di->_mdc2);
#undef SWAP
}

// :3813
static enum pmd_cmd_result lfoset(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    uint8_t al = lod8(si);
    di->delay = al;
    di->delay2 = al;
    al = lod8(si);
    di->speed = al;
    di->speed2 = al;
    al = lod8(si);
    di->step = al;
    di->step2 = al;
    al = lod8(si);
    di->time = al;
    di->time2 = al;
    lfoinit_main(di);
    return PMD_CMD_RETURN;
}

// :3827
static enum pmd_cmd_result lfoset_delay(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    uint8_t al = lod8(si);
    di->delay = al;
    di->delay2 = al;
    lfoinit_main(di);
    return PMD_CMD_RETURN;
}

// :3836
static enum pmd_cmd_result lfoswitch(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    uint8_t al = lod8(si);
    if ((al & 0xf8) != 0) al = 1;
    al &= 7;
    di->lfoswi = (uint8_t)((di->lfoswi & 0xf8) | al);
    lfoinit_main(di);
    return PMD_CMD_RETURN;
}

// :3847
static enum pmd_cmd_result lfoswitch_f(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    enum pmd_cmd_result r = lfoswitch(pmd, di, si);
    ch3_setting(pmd, di);
    return r;
}

// :3854
static enum pmd_cmd_result psgenvset(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    uint8_t al = lod8(si);
    di->pat = al;
    di->patb = al;
    di->pv2 = lod8(si);
    al = lod8(si);
    di->pr1 = al;
    di->pr1b = al;
    al = lod8(si);
    di->pr2 = al;
    di->pr2b = al;
    if (di->envf == 0xff) {
        di->envf = 2;
        di->penv = (uint8_t)-15;
    }
    return PMD_CMD_RETURN;
}

// :3876
static enum pmd_cmd_result comy(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)di;
    uint16_t ax = lod16(si);
    uint8_t dh = (uint8_t)ax;       // AL
    uint8_t dl = (uint8_t)(ax >> 8); // AH
    opnset(pmd, (uint16_t)((dh << 8) | dl));
    return PMD_CMD_RETURN;
}

// :3885
static enum pmd_cmd_result psgnoise(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)di;
    pmd->psnoi = lod8(si);
    return PMD_CMD_RETURN;
}

// :3889
static enum pmd_cmd_result psgnoise_move(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)di;
    uint8_t al = (uint8_t)(lod8(si) + pmd->psnoi);
    if ((al & 0x80) != 0) al = 0;
    if (al >= 32) al = 31;
    pmd->psnoi = al;
    return PMD_CMD_RETURN;
}

// :3906
static enum pmd_cmd_result psgsel(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)pmd;
    di->psgpat = lod8(si);
    return PMD_CMD_RETURN;
}

// :3914
static enum pmd_cmd_result panset(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint8_t al = lod8(si);
    panset_main(pmd, di, al);
    return PMD_CMD_RETURN;
}

// :3917
static void panset_main(struct pmd *pmd, struct pmd_qq *di, uint8_t al) {
#if board2
    al = (uint8_t)(((al >> 2) | (al << 6)) & 0xc0); // ror al,2
    uint8_t ah = al;
    al = (uint8_t)((di->fmpan & 0x3f) | ah);
    di->fmpan = al;

    if (fm3_active(pmd, di)) {
        pmd->parts[part3].fmpan = al;
        pmd->parts[part3b].fmpan = al;
        pmd->parts[part3c].fmpan = al;
        pmd->parts[part3d].fmpan = al;
    }
    if (di->partmask != 0) return;

    uint8_t dl = al;
    uint8_t dh = (uint8_t)(pmd->partb + 0xb3);
    calc_panout(pmd, di, &dl);
    opnset(pmd, (uint16_t)((dh << 8) | dl));
#else
    (void)pmd;
    (void)di;
    (void)al;
#endif
}

// :3961
static void calc_panout(struct pmd *pmd, struct pmd_qq *di, uint8_t *dl) {
    (void)pmd;
    *dl = di->fmpan;
    if (di->hldelay_c != 0) *dl &= 0xc0;
}

// :3972
static enum pmd_cmd_result panset_ex(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    uint8_t al = lod8(si);
    (*si)++; // reverse-phase flag
#if board2
    if (al == 0) al = 3;
    else if ((al & 0x80) != 0) al = 1;
    else al = 2;
    panset_main(pmd, di, al);
#else
    (void)pmd;
    (void)di;
    (void)al;
#endif
    return PMD_CMD_RETURN;
}

// :3994
static enum pmd_cmd_result rhykey(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)di;
#if board2
    uint8_t al = lod8(si);
    al &= pmd->rhythmmask;
    if (al == 0) return PMD_CMD_RETURN;
    uint8_t dl = al;

    if (pmd->fadeout_volume != 0) volset2rf(pmd, pmd->rhyvol);

    if ((dl & 0x80) == 0) {
        uint8_t bits = dl;
        uint8_t dh = 0x18;
        for (int i = 0; i < 6; i++) {
            if (bits & 1) opnset44(pmd, (uint16_t)((dh << 8) | pmd->rdat[i]));
            bits >>= 1;
            dh++;
        }
    }

    opnset44(pmd, (uint16_t)(0x1000 | dl));
    if ((dl & 0x80) == 0) {
        rflag_inc(pmd, dl, &pmd->rshot_bd);
        pmd->rshot_dat |= dl;
    } else {
        rflag_inc(pmd, (uint8_t)~dl, &pmd->rdump_bd);
        pmd->rshot_dat &= (uint8_t)~dl;
    }
    if (**si == 0xeb) _rwait(pmd);
#else
    (*si)++;
    (void)pmd;
#endif
    return PMD_CMD_RETURN;
}

// :4045
static void rflag_inc(struct pmd *pmd, uint8_t dl, uint8_t *flags) {
    (void)pmd;
    uint8_t al = dl;
    for (int i = 0; i < 6; i++) {
        if (al & 1) flags[i]++;
        al >>= 1;
    }
}

// :4064
static enum pmd_cmd_result rhyvs(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)di;
#if board2
    uint8_t al = lod8(si);
    uint8_t dl = (uint8_t)(0x1f & al);
    rhyvs_write(pmd, al, dl);
#else
    (*si)++;
    (void)pmd;
#endif
    return PMD_CMD_RETURN;
}

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
static void rhyvs_write(struct pmd *pmd, uint8_t al, uint8_t dl) {
    uint8_t index = (uint8_t)((al >> 5) & 7);
    uint8_t dh = (uint8_t)(0x17 + index);
    uint8_t *rdat = rdat_adr(pmd, index);
    dl = (uint8_t)(dl | (*rdat & 0xc0));
    *rdat = dl;
    opnset44(pmd, (uint16_t)((dh << 8) | dl));
}

// :4097
static enum pmd_cmd_result rhyvs_sft(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)di;
#if board2
    uint8_t al = lod8(si);
    uint8_t index = al;
    uint8_t dh = (uint8_t)(0x17 + index);
    uint8_t *rdat = rdat_adr(pmd, index);
    uint8_t dl = (uint8_t)(*rdat & 0x1f);
    al = (uint8_t)(lod8(si) + dl);
    if (al >= 32) al = (al & 0x80) != 0 ? 0 : 31;
    dl = (uint8_t)(al & 0x1f);
    dl |= (uint8_t)(*rdat & 0xe0);
    *rdat = dl;
    opnset44(pmd, (uint16_t)((dh << 8) | dl));
#else
    *si += 2;
    (void)pmd;
#endif
    return PMD_CMD_RETURN;
}

// :4136
static enum pmd_cmd_result rpnset(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)di;
#if board2
    uint8_t al = lod8(si);
    uint8_t ah = (uint8_t)(al & 3);
    ah = (uint8_t)(((ah >> 2) | (ah << 6)) & 0xff); // ror ah,2
    rhyvs_write(pmd, al, ah);
#else
    (*si)++;
    (void)pmd;
#endif
    return PMD_CMD_RETURN;
}

// :4155
static enum pmd_cmd_result rmsvs(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)di;
#if board2
    uint8_t dl = lod8(si);
    uint8_t al = pmd->rhythm_voldown;
    if (al != 0) {
        uint16_t product = (uint16_t)(uint8_t)(-al) * dl;
        dl = (uint8_t)(product >> 8);
    }
    volset2r(pmd, dl);
#else
    (*si)++;
    (void)pmd;
#endif
    return PMD_CMD_RETURN;
}

// :4166
static void volset2r(struct pmd *pmd, uint8_t dl) {
    pmd->rhyvol = dl;
    volset2rf(pmd, dl);
}

// :4169
static void volset2rf(struct pmd *pmd, uint8_t dl) {
    uint8_t al = pmd->fadeout_volume;
    if (al != 0) {
        uint16_t product = (uint16_t)(uint8_t)(-al) * dl;
        dl = (uint8_t)(product >> 8);
    }
    opnset44(pmd, (uint16_t)((0x11 << 8) | dl));
}

// :4184
static PMDC_UNUSED enum pmd_cmd_result rmsvs_sft(struct pmd *pmd, struct pmd_qq *di, uint8_t **si) {
    (void)di;
#if board2
    uint8_t dl = lod8(si);
    uint8_t al = (uint8_t)(pmd->rhyvol + dl);
    if (al >= 64) al = (al & 0x80) != 0 ? 0 : 63;
    volset2r(pmd, al);
#else
    (*si)++;
    (void)pmd;
#endif
    return PMD_CMD_RETURN;
}

// :4208
static uint8_t oshift(struct pmd_qq *di, uint8_t al) {
    if ((al & 0x0f) == 0x0f) return al; // rest
    int8_t dl = (int8_t)(di->shift + di->shift_def);
    if (dl == 0) return al;

    uint8_t bl = al & 0x0f;
    uint8_t bh = (uint8_t)(al >> 4);

    if (dl < 0) {
        unsigned sum = (unsigned)bl + (uint8_t)dl;
        bool carry = sum > 0xff;
        bl = (uint8_t)sum;
        if (!carry) {
            do {
                bh--;
                unsigned s = (unsigned)bl + 12;
                bl = (uint8_t)s;
                if (s > 0xff) break;
            } while (1);
        }
    } else {
        unsigned sum = (unsigned)bl + (uint8_t)dl;
        bl = (uint8_t)sum;
        while (bl >= 12) {
            bh++;
            bl = (uint8_t)(bl - 12);
        }
    }
    al = (uint8_t)((bh << 4) | (bh >> 4));
    al |= bl;
    return al;
}

// :4266
static void fnumset(struct pmd *pmd, struct pmd_qq *di, uint8_t al) {
    (void)pmd;
    uint8_t ah = al & 0x0f;
    if (ah == 0x0f) goto fnrest;
    di->onkai = al;

    uint8_t ch = (uint8_t)(((al >> 1) | (al << 7)) & 0x38);
    uint8_t bl = al & 0x0f;
    uint16_t ax = fnum_data[bl];
    uint8_t ah_out = (uint8_t)((ax >> 8) | ch);
    di->fnum = (uint16_t)((ah_out << 8) | (uint8_t)ax);
    return;

fnrest:
    di->onkai = 0xff;
    if ((di->lfoswi & 0x11) != 0) return;
    di->fnum = 0;
}

// :4302
static void fnumsetp(struct pmd *pmd, struct pmd_qq *di, uint8_t al) {
    (void)pmd;
    uint8_t ah = al & 0x0f;
    if (ah == 0x0f) {
        di->onkai = 0xff;
        if ((di->lfoswi & 0x11) == 0) di->fnum = 0;
        return;
    }
    di->onkai = al;

    uint8_t cl = (uint8_t)(al >> 4);
    uint8_t bl = al & 0x0f;
    uint16_t ax = psg_tune_data[bl];
    if (cl != 0) {
        uint16_t shifted = (uint16_t)(ax >> cl);
        if (((ax >> (cl - 1)) & 1) != 0) shifted++;
        ax = shifted;
    }
    di->fnum = ax;
}

// :4335
static void otodasi(struct pmd *pmd, struct pmd_qq *di) {
    uint16_t ax = di->fnum;
    if (ax == 0) return;
    if (di->slotmask == 0) return;

    uint16_t cx = ax & 0x3800;
    ax &= 0x7ff;
    ax = (uint16_t)(ax + di->porta_num);
    ax = (uint16_t)(ax + di->detune);

    uint8_t dh = pmd->partb;
    if (fm3_active(pmd, di)) {
        if (pmd->ch3mode != 0x3f) {
            // :4398 ch3_special
            uint16_t block = cx;
            uint8_t bl = di->slotmask;
            uint8_t cl = di->lfoswi;
            uint8_t bh = di->volmask;
            if ((bh & 0x0f) == 0) bh = 0xf0;
            uint8_t ch = di->_volmask;
            if ((ch & 0x0f) == 0) ch = 0xf0;

            static const uint8_t hi_reg[4] = {0xa6, 0xac, 0xae, 0xad};
            static const uint8_t lo_reg[4] = {0xa2, 0xa8, 0xaa, 0xa9};
            uint16_t detunes[4] = {
                pmd->slot_detune4, pmd->slot_detune3,
                pmd->slot_detune2, pmd->slot_detune1,
            };
            for (int slot = 0; slot < 4; slot++) {
                if ((bl & 0x80) == 0) {
                    bl = (uint8_t)(bl << 1);
                    continue;
                }
                bl = (uint8_t)(bl << 1);
                uint16_t value = (uint16_t)(ax + detunes[slot]);
                bool use_lfo1 = (bh & 0x80) != 0 && (cl & 1) != 0;
                bool use_lfo2 = (ch & 0x80) != 0 && (cl & 0x10) != 0;
                bh = (uint8_t)(bh << 1);
                ch = (uint8_t)(ch << 1);
                if (use_lfo1) value = (uint16_t)(value + di->lfodat);
                if (use_lfo2) value = (uint16_t)(value + di->_lfodat);
                uint16_t out_block = block;
                fm_block_calc(&out_block, &value);
                value |= out_block;
                opnset(pmd, (uint16_t)((hi_reg[slot] << 8) | (value >> 8)));
                opnset(pmd, (uint16_t)((lo_reg[slot] << 8) | (value & 0xff)));
            }
            return;
        }
    }

    if ((di->lfoswi & 1) != 0) ax = (uint16_t)(ax + di->lfodat);
    if ((di->lfoswi & 0x10) != 0) ax = (uint16_t)(ax + di->_lfodat);
    fm_block_calc(&cx, &ax);

    ax |= cx;
    dh = (uint8_t)(dh + 0xa3);
    opnset(pmd, (uint16_t)((dh << 8) | (ax >> 8)));
    dh = (uint8_t)(dh - 4);
    opnset(pmd, (uint16_t)((dh << 8) | (ax & 0xff)));
}

// :4549
static void fm_block_calc(uint16_t *cx, uint16_t *ax) {
    for (;;) {
        if ((*ax & 0x8000) != 0) goto od1;
        if (*ax < 0x26a) goto od1;
        if (*ax < 0x26a * 2) return;
        *cx = (uint16_t)(*cx + 0x0800);
        if (*cx == 0x4000) {
            *cx = 0x3800;
            if (*ax < 0x800) return;
            *ax = 0x7ff;
            return;
        }
        *ax = (uint16_t)(*ax - 0x26a);
        continue;

od1:
        // The octave cannot drop below zero. The original subtracts and then
        // overwrites CX on the borrow path, so it happens after the check.
        if (*cx < 0x0800) {
            *cx = 0;
            if ((*ax & 0x8000) == 0 && *ax >= 8) return;
            *ax = 8;
            return;
        }
        *cx = (uint16_t)(*cx - 0x0800);
        *ax = (uint16_t)(*ax + 0x26a);
    }
}

// :4588
static void otodasip(struct pmd *pmd, struct pmd_qq *di) {
    uint16_t ax = di->fnum;
    if (ax == 0) return;

    ax = (uint16_t)(ax + di->porta_num);

    if ((di->extendmode & 1) == 0) {
        ax = (uint16_t)(ax - di->detune);
        if ((di->lfoswi & 1) != 0) ax = (uint16_t)(ax - di->lfodat);
        if ((di->lfoswi & 0x10) != 0) ax = (uint16_t)(ax - di->_lfodat);
        goto tonesetp;
    }

    // Extended detune.
    if (di->detune != 0) {
        int32_t product = (int16_t)ax * di->detune;
        int16_t dx = (int16_t)(((uint32_t)product << 4) >> 16);
        if (dx < 0) dx--;
        else dx++;
        ax = (uint16_t)(ax - (uint16_t)dx);
    }

    {
        int16_t dx = 0;
        if ((di->lfoswi & 0x11) != 0) {
            if ((di->lfoswi & 1) != 0) dx = di->lfodat;
            if ((di->lfoswi & 0x10) != 0) dx = (int16_t)(dx + di->_lfodat);
            if (dx != 0) {
                int32_t product = (int16_t)ax * dx;
                int16_t d = (int16_t)(((uint32_t)product << 4) >> 16);
                if (d < 0) d--;
                else d++;
                ax = (uint16_t)(ax - (uint16_t)d);
            }
        }
    }

tonesetp:
    if (ax >= 0x1000) ax = (int16_t)ax < 0 ? 0 : 0xfff;
    {
        uint8_t dh = (uint8_t)((pmd->partb - 1) * 2);
        opnset44(pmd, (uint16_t)((dh << 8) | (ax & 0xff)));
        dh++;
        opnset44(pmd, (uint16_t)((dh << 8) | (ax >> 8)));
    }
}

// :4726
static void volset(struct pmd *pmd, struct pmd_qq *di) {
    uint8_t bl = di->slotmask;
    if (bl == 0) return;

    uint8_t al = di->volpush;
    if (al != 0) al--;
    else al = di->volume;
    uint8_t cl = al;

    if (di != &pmd->part_e) {
        al = pmd->fm_voldown;
        if (al != 0) {
            uint16_t product = (uint16_t)(uint8_t)(-al) * cl;
            cl = (uint8_t)(product >> 8);
        }
        al = pmd->fadeout_volume;
        if (al >= 2) {
            al >>= 1;
            uint16_t product = (uint16_t)(uint8_t)(-al) * cl;
            cl = (uint8_t)(product >> 8);
        }
    }

    uint8_t vol[4] = {0x80, 0x80, 0x80, 0x80};
    uint8_t bh = 0;
    uint8_t ch = bl;

    cl = (uint8_t)~cl;
    bl &= di->carrier;
    bh |= bl;
    if ((bl & 0x80) != 0) vol[0] = cl;
    if ((bl & 0x40) != 0) vol[1] = cl;
    if ((bl & 0x20) != 0) vol[2] = cl;
    if ((bl & 0x10) != 0) vol[3] = cl;

    if (cl != 0xff) {
        if ((di->lfoswi & 2) != 0) {
            uint8_t mask = (uint8_t)(di->volmask & ch);
            bh |= mask;
            fmlfo_sub((uint8_t)di->lfodat, mask, vol);
        }
        if ((di->lfoswi & 0x20) != 0) {
            uint8_t mask = (uint8_t)(di->_volmask & ch);
            bh |= mask;
            fmlfo_sub((uint8_t)di->_lfodat, mask, vol);
        }
    }

    uint8_t dh = (uint8_t)(0x4c - 1 + pmd->partb);
    uint8_t *slots[4] = {&di->slot4, &di->slot3, &di->slot2, &di->slot1};
    for (int i = 0; i < 4; i++) {
        if ((bh & 0x80) != 0) {
            uint8_t dl = *slots[i];
            al = vol[i];
            unsigned sum = (unsigned)al + dl;
            al = sum > 0xff ? 255 : (uint8_t)sum;
            if (al < 0x80) al = 0;
            else al -= 0x80;
            opnset(pmd, (uint16_t)((dh << 8) | al));
        }
        bh = (uint8_t)(bh << 1);
        if (i == 0) dh = (uint8_t)(dh - 8);
        else if (i == 1) dh = (uint8_t)(dh + 4);
        else if (i == 2) dh = (uint8_t)(dh - 8);
    }
}

// :4864
static void fmlfo_sub(uint8_t al, uint8_t bl, uint8_t *vol) {
    for (int i = 0; i < 4; i++) {
        if ((bl & 0x80) == 0) {
            bl = (uint8_t)(bl << 1);
            continue;
        }
        bl = (uint8_t)(bl << 1);
        if ((al & 0x80) == 0) {
            uint8_t v = vol[i];
            uint8_t res = (uint8_t)(v - al);
            if (v < al) vol[i] = 0;
            else vol[i] = res;
        } else {
            uint8_t v = vol[i];
            uint8_t res = (uint8_t)(v - al);
            if (v >= al) vol[i] = 0xff;
            else vol[i] = res;
        }
    }
}

// :4890
static void volsetp(struct pmd *pmd, struct pmd_qq *di) {
    if (di->envf == 3) return;
    if (di->envf == 0xff && di->eenv_count == 0) return;

    uint8_t al = di->volpush;
    if (al != 0) al--;
    else al = di->volume;
    uint8_t dl = al;

    al = pmd->ssg_voldown;
    if (al != 0) {
        uint16_t product = (uint16_t)(uint8_t)(-al) * dl;
        dl = (uint8_t)(product >> 8);
    }

    al = pmd->fadeout_volume;
    if (al != 0) {
        uint16_t product = (uint16_t)(uint8_t)(-al) * dl;
        dl = (uint8_t)(product >> 8);
    }

    // :4921 psg_env_calc
    if (dl == 0) goto pv_out;

    if (di->envf == 0xff) {
        al = dl;
        dl = di->eenv_volume;
        if (dl == 0) {
            dl = 0;
            goto pv_out;
        }
        dl++;
        uint16_t product = (uint16_t)al * dl;
        dl = (uint8_t)product;
        bool carry = (dl & 1) != 0;
        dl >>= 1;
        carry = (dl & 1) != 0;
        dl >>= 1;
        carry = (dl & 1) != 0;
        dl >>= 1;
        carry = (dl & 1) != 0;
        dl >>= 1;
        if (carry) dl++;
    } else {
        int16_t sum = (int16_t)(int8_t)dl + (int8_t)di->penv;
        if (sum < 0) {
            dl = 0;
            goto pv_out;
        }
        dl = (uint8_t)sum;
        if (dl == 0) goto pv_out;
        if (dl >= 16) dl = 15;
    }

    // :4965 volume LFO
    if ((di->lfoswi & 0x22) != 0) {
        int16_t ax = 0;
        if ((di->lfoswi & 2) != 0) ax = di->lfodat;
        if ((di->lfoswi & 0x20) != 0) ax = (int16_t)(ax + di->_lfodat);
        int16_t sum = (int16_t)dl + ax;
        if (sum < 0) dl = 0;
        else if (sum >= 16) dl = 15;
        else dl = (uint8_t)sum;
    }

pv_out:
    {
        uint8_t dh = (uint8_t)(pmd->partb + 8 - 1);
        opnset44(pmd, (uint16_t)((dh << 8) | dl));
    }
}

// :4998
static void keyon(struct pmd *pmd, struct pmd_qq *di) {
    if (di->onkai == 0xff) return;
    uint8_t dl = (uint8_t)(pmd->partb - 1);
    uint8_t bl = dl;
    uint8_t *state;
#if board2
    if (pmd->fmsel != 0) state = &pmd->ura_key1 + bl;
    else state = &pmd->omote_key1 + bl;
#else
    state = &pmd->omote_key1 + bl;
#endif
    uint8_t al = (uint8_t)(*state | di->slotmask);
    if (di->sdelay_c != 0) al &= di->sdelay_m;
    *state = al;
    dl |= al;
#if board2
    if (pmd->fmsel != 0) dl |= 0x04;
#endif
    opnset44(pmd, (uint16_t)((0x28 << 8) | dl));
}

// :5039
static void keyonp(struct pmd *pmd, struct pmd_qq *di) {
    if (di->onkai == 0xff) return;
    uint8_t al, ah;
    psgmsk(pmd, &al, &ah);
    al |= ah;
    ah &= di->psgpat;
    ah = (uint8_t)~ah;
    al &= ah;
    opnset44(pmd, (uint16_t)((7 << 8) | al));

    uint8_t dl = pmd->psnoi;
    if (dl == pmd->psnoi_last) return;
    if ((pmd->psgefcnum & 0x80) == 0) return;
    opnset44(pmd, (uint16_t)((6 << 8) | dl));
    pmd->psnoi_last = dl;
}

// :5074
static void psgmsk(struct pmd *pmd, uint8_t *al, uint8_t *ah) {
    uint8_t cl = pmd->partb;
    // The original sets the carry and rotates it into AL once per part, so the
    // part's own channel bit is 1 << (partb - 1), not 1 << partb.
    *al = (uint8_t)(cl != 0 ? 1u << (cl - 1) : 0);
    *ah = *al;
    *al <<= 3;
    *ah |= *al;
    *al = get07(pmd);
}

// :5091
static void keyoff(struct pmd *pmd, struct pmd_qq *di) {
    if (di->onkai == 0xff) return;
    keyoff_main(pmd, di);
}

// :5095
// kof1: the register write, also used when silencing a masked part.
static void keyoff_main(struct pmd *pmd, struct pmd_qq *di) {
    uint8_t dl = (uint8_t)(pmd->partb - 1);
    uint8_t bl = dl;
    uint8_t *state;
#if board2
    if (pmd->fmsel != 0) state = &pmd->ura_key1 + bl;
    else state = &pmd->omote_key1 + bl;
#else
    state = &pmd->omote_key1 + bl;
#endif
    uint8_t cl = (uint8_t)(~di->slotmask & *state);
    *state = cl;
    dl |= cl;
#if board2
    if (pmd->fmsel != 0) dl |= 0x04;
#endif
    opnset44(pmd, (uint16_t)((0x28 << 8) | dl));
}

// :5125
static void keyoffp(struct pmd *pmd, struct pmd_qq *di) {
    (void)pmd;
    if (di->onkai == 0xff) return;
    if (di->envf == 0xff) di->eenv_count = 4;
    else di->envf = 2;
}

// :5144
static void neiroset(struct pmd *pmd, struct pmd_qq *di, uint8_t dl) {
    uint8_t *bx;
    toneadr_calc(pmd, di, dl, &bx);
    uint8_t *tl = bx + 4;
    if (!silence_fmpart(pmd, di)) {
        // :5160 neiroset_main
        uint8_t dh = (uint8_t)(0xb0 - 1 + pmd->partb);
        dl = bx[24];
        if (pmd->af_check != 0) dl = di->alg_fb;

        if (fm3_active(pmd, di)) {
            if (pmd->af_check == 0) {
                if ((di->slotmask & 0x10) == 0) {
                    uint8_t al = (uint8_t)(pmd->fm3_alg_fb & 0x38);
                    dl = (uint8_t)((dl & 7) | al);
                }
                pmd->fm3_alg_fb = dl;
            } else {
                dl = pmd->fm3_alg_fb;
            }
        }
        opnset(pmd, (uint16_t)((dh << 8) | dl));
        di->alg_fb = dl;
        dl &= 7;

        // :5200 check_carrier
        uint8_t al = carrier_table[dl];
        if ((di->volmask & 0x0f) == 0) di->volmask = al;
        if ((di->_volmask & 0x0f) == 0) di->_volmask = al;
        di->carrier = al;
        uint8_t ah = carrier_table[dl + 8];

        al = di->neiromask;
        ah &= al;

        dh = (uint8_t)(0x30 - 1 + pmd->partb);

        // The original rotates the masks, so a fully-set neiromask walks all
        // sixteen remaining parameters.
        for (int i = 0; i < 4; i++) {
            dl = *bx++;
            if (al & 0x80) opnset(pmd, (uint16_t)((dh << 8) | dl));
            al = (uint8_t)((al << 1) | (al >> 7));
            dh = (uint8_t)(dh + 4);
        }
        for (int i = 0; i < 4; i++) {
            dl = *bx++;
            if (ah & 0x80) opnset(pmd, (uint16_t)((dh << 8) | dl));
            ah = (uint8_t)((ah << 1) | (ah >> 7));
            dh = (uint8_t)(dh + 4);
        }
        for (int i = 0; i < 16; i++) {
            dl = *bx++;
            if (al & 0x80) opnset(pmd, (uint16_t)((dh << 8) | dl));
            al = (uint8_t)((al << 1) | (al >> 7));
            dh = (uint8_t)(dh + 4);
        }

    }
    neiroset_tl(di, tl);
}

// :5258
static void neiroset_tl(struct pmd_qq *di, const uint8_t *bx) {
    di->slot1 = bx[0];
    di->slot3 = bx[1];
    di->slot2 = bx[2];
    di->slot4 = bx[3];
}

// :5274
static void toneadr_calc(struct pmd *pmd, struct pmd_qq *di, uint8_t dl, uint8_t **bx) {
    if (pmd->prg_flg == 0 && di != &pmd->part_e) {
        *bx = pmd->tondat + (unsigned)dl * 32;
        return;
    }
    uint8_t *p = pmd->prgdat_adr;
    if (di == &pmd->part_e) p = pmd->prgdat_adr2;
    while (*p != dl) p += 26;
    *bx = p + 1;
}

// :5307
static bool silence_fmpart(struct pmd *pmd, struct pmd_qq *di) {
    uint8_t al = di->neiromask;
    if (al == 0) return true;

    uint8_t dh = (uint8_t)(pmd->partb + 0x3f);
    for (int i = 0; i < 4; i++) {
        if (al & 0x80) {
            opnset(pmd, (uint16_t)((dh << 8) | 127));
            opnset(pmd, (uint16_t)(((dh + 0x40) << 8) | 127));
        }
        al = (uint8_t)(al << 1);
        dh = (uint8_t)(dh + 4);
    }
    keyoff_main(pmd, di);
    return false;
}

// :5341
static bool lfo(struct pmd *pmd, struct pmd_qq *di) {
    if (di->delay != 0) {
        di->delay--;
        return false;
    }

    // :5348
    if ((di->extendmode & 2) != 0) {
        uint8_t ch = (uint8_t)(pmd->TimerAtime - pmd->lastTimerAtime);
        if (ch == 0) return false;
        int16_t old = di->lfodat;
        do {
            lfo_main(pmd, di);
            ch--;
        } while (ch != 0);
        return di->lfodat != old;
    }

    {
        int16_t old = di->lfodat;
        lfo_main(pmd, di);
        return di->lfodat != old;
    }
}

// :5375
static void lfo_main(struct pmd *pmd, struct pmd_qq *di) {
    (void)pmd;
    if (di->speed == 1) goto lfo2;
    if (di->speed == 0xff) return;
    di->speed--;
    return;

lfo2:
    di->speed = di->speed2;
    uint8_t bl = di->lfo_wave;
    if (bl == 0 || bl == 4) goto lfo_sankaku;
    if (bl == 2) goto lfo_kukei;
    if (bl == 6) goto lfo_oneshot;
    if (bl == 5) goto lfo_sankaku;
    if (bl == 1) goto lfo_nokogiri;
    goto lfo_random;

lfo_sankaku:
    {
        uint16_t ax;
        if (di->lfo_wave == 5) {
            int8_t al = (int8_t)di->step;
            uint8_t mag = (uint8_t)(al < 0 ? -al : al);
            ax = (uint16_t)((int16_t)al * (int16_t)mag);
        } else {
            ax = (uint16_t)(int16_t)(int8_t)di->step;
        }
        di->lfodat = (int16_t)(di->lfodat + (int16_t)ax);
        if (di->lfodat == 0) md_inc(di);
    }
    {
        uint8_t al = di->time;
        if (al == 0xff) goto lfo3;
        al--;
        if (al != 0) goto lfo3;
        al = di->time2;
        if (di->lfo_wave != 4) al = (uint8_t)(al + al);
        di->time = al;
        di->step = (uint8_t)(0 - di->step);
        return;
lfo3:
        di->time = al;
        return;
    }

lfo_nokogiri:
    di->lfodat = (int16_t)(di->lfodat +
                           (int16_t)(int8_t)di->step);
    {
        uint8_t al = di->time;
        if (al == 0xff) goto nk_lfo3;
        al--;
        if (al != 0) goto nk_lfo3;
        di->lfodat = (int16_t)(0 - di->lfodat);
        md_inc(di);
        al = di->time2;
        al = (uint8_t)(al + al);
nk_lfo3:
        di->time = al;
        return;
    }

lfo_oneshot:
    {
        uint8_t al = di->time;
        if (al == 0) return;
        if (al != 0xff) {
            al--;
            di->time = al;
        }
        di->lfodat = (int16_t)(di->lfodat +
                               (int16_t)(int8_t)di->step);
        return;
    }

lfo_kukei:
    {
        uint16_t ax = (uint16_t)((int16_t)(int8_t)di->step *
                                 (int16_t)(int8_t)di->time);
        di->lfodat = (int16_t)ax;
        md_inc(di);
        di->step = (uint8_t)(0 - di->step);
        return;
    }

lfo_random:
    {
        uint8_t al = di->step;
        if ((al & 0x80) != 0) al = (uint8_t)(0 - al);
        uint16_t x = (uint16_t)((unsigned)al * di->time);
        uint16_t r = rnd(pmd, (uint16_t)(x + x));
        di->lfodat = (int16_t)(r - x);
        return;
    }
}

// :5499
static void md_inc(struct pmd_qq *di) {
    di->mdspd--;
    if (di->mdspd != 0) return;
    di->mdspd = di->mdspd2;

    uint8_t al = di->mdc;
    if (al == 0) return;
    if ((al & 0x80) != 0) goto mdi21;
    al--;
    di->mdc = al;

mdi21:
    al = di->step;
    if ((al & 0x80) != 0) {
        al = (uint8_t)(0 - al);
        al = (uint8_t)(al + di->mdepth);
        if ((al & 0x80) != 0) {
            al = 0;
            if ((di->mdepth & 0x80) == 0) al = 0x81;
            di->step = al;
            return;
        }
        al = (uint8_t)(0 - al);
        di->step = al;
        return;
    }
    al = (uint8_t)(al + di->mdepth);
    if ((al & 0x80) != 0) {
        al = 0;
        if ((di->mdepth & 0x80) == 0) al = 0x7f;
        di->step = al;
        return;
    }
    di->step = al;
}

// :5548
static uint16_t rnd(struct pmd *pmd, uint16_t ax) {
    uint16_t cx = ax;
    uint32_t product = (uint32_t)259 * pmd->seed;
    ax = (uint16_t)((uint16_t)product + 3) & 0x7fff;
    pmd->seed = ax;
    uint32_t p2 = (uint32_t)ax * cx;
    return (uint16_t)(p2 / 32767);
}

// :5568
static uint8_t lfoinitp(struct pmd *pmd, struct pmd_qq *di, uint8_t al) {
    uint8_t ah = al & 0x0f;
    if (ah == 0x0c) {
        al = di->onkai_def;
        ah = al & 0x0f;
    }
    di->onkai_def = al;
    if (ah == 0x0f) goto lfo_exitp;

    di->porta_num = 0;
    if ((pmd->tieflag & 1) == 0) goto seinit;

lfo_exitp:
    soft_env(pmd, di);
    lfo_exit_common(pmd, di);
    return al;

seinit:
    if (di->envf == 0xff) goto extenv_init;
    di->envf = 0;
    di->penv = 0;
    ah = di->patb;
    di->pat = ah;
    if (ah == 0) {
        di->envf = 1;
        di->penv = di->pv2;
    }
    di->pr1 = di->pr1b;
    di->pr2 = di->pr2b;
    lfin1(pmd, di);
    return al;

extenv_init:
    di->eenv_arc = (uint8_t)(di->eenv_ar - 16);
    {
        uint8_t v = (uint8_t)(di->eenv_dr - 16);
        if ((v & 0x80) != 0) v = (uint8_t)(v + v);
        di->eenv_drc = v;
        v = (uint8_t)(di->eenv_sr - 16);
        if ((v & 0x80) != 0) v = (uint8_t)(v + v);
        di->eenv_src = v;
        di->eenv_rrc = (uint8_t)((di->eenv_rr + di->eenv_rr) - 16);
    }
    di->eenv_volume = di->eenv_al;
    di->eenv_count = 1;
    ext_ssgenv_main(di);
    lfin1(pmd, di);
    return al;
}

// :5645
static uint8_t lfoinit(struct pmd *pmd, struct pmd_qq *di, uint8_t al) {
    uint8_t ah = al & 0x0f;
    if (ah == 0x0c) {
        al = di->onkai_def;
        ah = al & 0x0f;
    }
    di->onkai_def = al;
    if (ah == 0x0f) goto lfo_exit;
    di->porta_num = 0;
    if ((pmd->tieflag & 1) == 0) {
        lfin1(pmd, di);
        return al;
    }

lfo_exit:
    lfo_exit_common(pmd, di);
    return al;
}

// :5661
// The shared tail of lfoinit and lfoinitp.
static void lfo_exit_common(struct pmd *pmd, struct pmd_qq *di) {
    if ((di->lfoswi & 3) != 0) lfo(pmd, di);
    if ((di->lfoswi & 0x30) != 0) {
        lfo_change(di);
        lfo(pmd, di);
        lfo_change(di);
    }
}

// :5684 lfin1, shared by lfoinit and lfoinitp.
static void lfin1(struct pmd *pmd, struct pmd_qq *di) {
#if board2
    di->hldelay_c = di->hldelay;
    if (di->hldelay != 0) {
        opnset(pmd, (uint16_t)(((pmd->partb + 0xb3) << 8) |
                               (di->fmpan & 0xc0)));
    }
#endif
    di->sdelay_c = di->sdelay;

    uint8_t cl = di->lfoswi;
    if ((cl & 3) != 0) {
        if ((cl & 4) == 0) lfoinit_main(di);
        lfo(pmd, di);
    }
    if ((cl & 0x30) != 0) {
        if ((cl & 0x40) == 0) {
            lfo_change(di);
            lfoinit_main(di);
            lfo_change(di);
        }
        lfo_change(di);
        lfo(pmd, di);
        lfo_change(di);
    }
}

// :5737
static void lfoinit_main(struct pmd_qq *di) {
    di->lfodat = 0;
    di->delay = di->delay2;
    di->speed = di->speed2;
    di->step = di->step2;
    di->time = di->time2;
    di->mdc = di->mdc2;

    if (di->lfo_wave == 2 || di->lfo_wave == 3) {
        di->speed = 1;
        return;
    }
    di->speed++;
}

// :5761
static bool soft_env(struct pmd *pmd, struct pmd_qq *di) {
    if ((di->extendmode & 4) != 0) {
        uint8_t ch = (uint8_t)(pmd->TimerAtime - pmd->lastTimerAtime);
        if (ch == 0) return false;
        uint8_t cl = 0;
        do {
            if (soft_env_main(di)) cl = 1;
            ch--;
        } while (ch != 0);
        return (cl & 1) != 0;
    }
    return soft_env_main(di);
}

// :5778
static bool soft_env_main(struct pmd_qq *di) {
    if (di->envf == 0xff) return ext_ssgenv_main(di);
    uint8_t old = di->penv;
    soft_env_sub(di, &old);
    return old != di->penv;
}

// :5789
static void soft_env_sub(struct pmd_qq *di, uint8_t *dl) {
    (void)dl;
    if (di->envf == 0) {
        // Attack
        di->pat--;
        if (di->pat != 0) return;
        di->envf = 1;
        di->penv = di->pv2;
        return;
    }
    if (di->envf == 2) goto se3;

    // Decay
    if (di->pr1 == 0) return;
    di->pr1--;
    if (di->pr1 != 0) return;
    di->pr1 = di->pr1b;
    di->penv--;

se4:
    if (di->penv >= (uint8_t)-15) return;
    if (di->penv < 15) return;
se5:
    di->penv = (uint8_t)-15;
    return;

se3:
    // Release
    if (di->pr2 == 0) goto se5;
    di->pr2--;
    if (di->pr2 != 0) return;
    di->pr2 = di->pr2b;
    di->penv--;
    goto se4;
}

// :5835
static bool ext_ssgenv_main(struct pmd_qq *di) {
    uint8_t ah = di->eenv_count;
    if (ah == 0) return false;
    uint8_t old = di->eenv_volume;
    uint8_t dl = di->eenv_volume;
    esm_sub(di, &ah, &dl);
    return old != di->eenv_volume;
}

// :5849
static void esm_sub(struct pmd_qq *di, uint8_t *ah, uint8_t *dl) {
    (void)dl;
    (*ah)--;
    if (*ah != 0) goto esm_dr_check;

    // Attack Rate
    {
        uint8_t al = (uint8_t)(di->eenv_arc - 1);
        if ((al & 0x80) != 0) goto arc_count_check;
        al++;
        di->eenv_volume = (uint8_t)(di->eenv_volume + al);
        if (di->eenv_volume < 15) {
            di->eenv_arc = (uint8_t)(di->eenv_ar - 16);
            return;
        }
        di->eenv_volume = 15;
        di->eenv_count++;
        if (di->eenv_sl != 15) return;
        di->eenv_count++;
        return;
arc_count_check:
        if (di->eenv_ar == 0) return;
        di->eenv_arc++;
        return;
    }

esm_dr_check:
    (*ah)--;
    if (*ah != 0) goto esm_sr_check;

    // Decay Rate
    {
        uint8_t al = (uint8_t)(di->eenv_drc - 1);
        if ((al & 0x80) != 0) goto drc_count_check;
        al++;
        uint8_t volume = (uint8_t)(di->eenv_volume - al);
        bool borrow = di->eenv_volume < al;
        di->eenv_volume = volume;
        if (borrow) goto dr_slset;
        if (di->eenv_volume < di->eenv_sl) goto dr_slset;
        uint8_t ah2 = (uint8_t)(di->eenv_dr - 16);
        if ((ah2 & 0x80) != 0) ah2 = (uint8_t)(ah2 + ah2);
        di->eenv_drc = ah2;
        return;
dr_slset:
        di->eenv_volume = di->eenv_sl;
        di->eenv_count++;
        return;
drc_count_check:
        if (di->eenv_dr == 0) return;
        di->eenv_drc++;
        return;
    }

esm_sr_check:
    (*ah)--;
    if (*ah != 0) goto esm_rr;

    // Sustain Rate
    {
        uint8_t al = (uint8_t)(di->eenv_src - 1);
        if ((al & 0x80) != 0) goto src_count_check;
        al++;
        uint8_t volume = (uint8_t)(di->eenv_volume - al);
        if (di->eenv_volume < al) {
            di->eenv_volume = 0;
        } else {
            di->eenv_volume = volume;
        }
        uint8_t ah2 = (uint8_t)(di->eenv_sr - 16);
        if ((ah2 & 0x80) != 0) ah2 = (uint8_t)(ah2 + ah2);
        di->eenv_src = ah2;
        return;
src_count_check:
        if (di->eenv_sr == 0) return;
        di->eenv_src++;
        return;
    }

esm_rr:
    // Release Rate
    {
        uint8_t al = (uint8_t)(di->eenv_rrc - 1);
        if ((al & 0x80) != 0) goto rrc_count_check;
        al++;
        uint8_t volume = (uint8_t)(di->eenv_volume - al);
        if (di->eenv_volume < al) di->eenv_volume = 0;
        else di->eenv_volume = volume;
        di->eenv_rrc = (uint8_t)((di->eenv_rr + di->eenv_rr) - 16);
        return;
rrc_count_check:
        if (di->eenv_rr == 0) return;
        di->eenv_rrc++;
        return;
    }
}

// :5967
static void fadeout(struct pmd *pmd) {
    if (pmd->pause_flag == 1) return;
    uint8_t al = pmd->fadeout_speed;
    if (al == 0) return;
    if ((al & 0x80) != 0) goto fade_in;

    unsigned sum = (unsigned)al + pmd->fadeout_volume;
    if (sum > 0xff) {
        pmd->fadeout_volume = 255;
        pmd->fadeout_speed = 0;
        if (pmd->fade_stop_flag == 1) pmd->music_flag |= 2;
        return;
    }
    pmd->fadeout_volume = (uint8_t)sum;
    return;

fade_in:
    sum = (unsigned)al + pmd->fadeout_volume;
    if (sum <= 0xff) {
        pmd->fadeout_volume = 0;
        pmd->fadeout_speed = 0;
#if board2
        volset2rf(pmd, pmd->rhyvol);
#endif
        return;
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
    uint8_t ah = 2;
#endif

#if board2
oploop:;
#endif
    const uint8_t *table;
    if (pmd->fm_effec_flag == 1
#if board2
        && ah == 1
#endif
    ) {
        table = fmoff_ef;
    } else {
        table = fmoff_nef;
    }
    for (;;) {
        uint8_t dh = *table++;
        if (dh == 0xff) break;
        opnset(pmd, (uint16_t)(((dh + 0x80) << 8) | 0xff));
    }

#if board2
    sel46(pmd);
    ah--;
    if (ah != 0) goto oploop;
#endif

    {
        uint16_t dx = 0x2800;
        uint8_t cx = 3;
#if !board2
        if (pmd->fm_effec_flag == 1) cx--;
#endif
        do {
            opnset44(pmd, dx);
            dx = (uint16_t)(dx + 1); // the channel number is the data byte
            cx--;
        } while (cx != 0);
    }
#if board2
    {
        uint16_t dx = 0x2804;
        uint8_t cx = 3;
        if (pmd->fm_effec_flag == 1) cx--;
        do {
            opnset44(pmd, dx);
            dx = (uint16_t)(dx + 1);
            cx--;
        } while (cx != 0);
    }
#endif

    if (pmd->effon != 0) goto psg_ef;
    if (pmd->ppsdrv_flag != 0 && pmd->pps != NULL &&
        pmd->pps->keyoff != NULL) {
        pmd->pps->keyoff(pmd->user_data);
    }
    opnset44(pmd, 0x07bf);
    goto s_pcm;

psg_ef:
    {
        uint8_t dl = (uint8_t)((get07(pmd) & 0x3f) | 0x9b);
        opnset44(pmd, (uint16_t)((0x07 << 8) | dl));
    }

s_pcm:;
#if board2
    if (pmd->pcmflag == 0) {
#if adpcm && !ademu
        if (pmd->pcm_gs_flag != 1) {
            opnset46(pmd, 0x0102);
            opnset46(pmd, 0x0001);
        }
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
            for (uint8_t ch = 0; ch < 8; ch++) {
                drv->keyoff(ch, pmd->user_data);
            }
        }
    }
#endif
#endif
}

// The original brackets every register write with pushf/cli/sti and restores
// the interrupt flag afterwards. The host owns that exclusion (see the
// interrupt/timer section of the plan), so these helpers perform the I/O only.
//
// :6173
static void opnset44(struct pmd *pmd, uint16_t dx) {
    rdychk(pmd, pmd->fm1_port1);
    outb(pmd, pmd->fm1_port1, (uint8_t)(dx >> 8));
    _waitP(pmd);
    outb(pmd, pmd->fm1_port2, (uint8_t)dx);
}

#if board2
// :6200
static PMDC_UNUSED void opnset46(struct pmd *pmd, uint16_t dx) {
    rdychk(pmd, pmd->fm2_port1);
    outb(pmd, pmd->fm2_port1, (uint8_t)(dx >> 8));
    _waitP(pmd);
    outb(pmd, pmd->fm2_port2, (uint8_t)dx);
}
#endif

// :6226
static void opnset(struct pmd *pmd, uint16_t dx) {
    rdychk(pmd, pmd->fm_port1);
    outb(pmd, pmd->fm_port1, (uint8_t)(dx >> 8));
    _waitP(pmd);
    outb(pmd, pmd->fm_port2, (uint8_t)dx);
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
    uint8_t al = pmd->board != 0 ? pmd->fadeout_volume : PMD_NO_BOARD;
    int60_exit(pmd);
    return al;
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
    uint8_t al = pmd->board != 0 ? pmd->fm_effec_num : PMD_NO_BOARD;
    int60_exit(pmd);
    return al;
}

// :6499
uint8_t pmd_get_pcmefc_num(struct pmd *pmd) {
    int60_enter(pmd, 0x12);
    uint8_t al = pmd->board != 0 ? pmd->pcm_effec_num : PMD_NO_BOARD;
    int60_exit(pmd);
    return al;
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
    uint8_t al = pmd->board != 0 ? pmd->ppsdrv_flag : PMD_NO_BOARD;
    int60_exit(pmd);
    return al;
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
// Both axes, in AL and AH. The original brackets each PSG access with cli/sti;
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
    if (pmd->fm_effec_flag == 1) goto pauoff_exit;
#endif
    pmd->partb = 3;
    neiro_reset(pmd, &pmd->parts[part3]);
    neiro_reset(pmd, &pmd->parts[part3b]);
    neiro_reset(pmd, &pmd->parts[part3c]);
    neiro_reset(pmd, &pmd->parts[part3d]);

#if board2
    sel46(pmd);
    pmd->partb = 1;
    neiro_reset(pmd, &pmd->parts[part4]);
    pmd->partb = 2;
    neiro_reset(pmd, &pmd->parts[part5]);
    if (pmd->fm_effec_flag == 1) goto pauoff_exit;
    pmd->partb = 3;
    neiro_reset(pmd, &pmd->parts[part6]);
#endif

pauoff_exit:
    pmd->pause_flag = 0;
    pmd->play_flag = 1;
    int60_exit(pmd);
}

// :6741
static uint8_t *get_memo(struct pmd *pmd, uint8_t al) {
    uint8_t *si = pmd->mmlbuf;
    if ((uint16_t)(si[0] | (si[1] << 8)) != 0x1a) {
        return NULL; // no voice data: the memo table address is unavailable
    }

    si = pmd->mmlbuf + (uint16_t)(si[0x18] | (si[0x19] << 8)) - 4;
    uint8_t bl = si[2];
    uint8_t bh = si[3];
    if (bl != 0x40 && (bh != 0xfe || bl < 0x41)) return NULL;

    // :6782 The memo table starts one entry further in MC 4.2 and again in
    // 4.8.
    if (bl >= 0x42) al++;
    if (bl >= 0x48) al++;
    si = pmd->mmlbuf + (uint16_t)(si[0] | (si[1] << 8));

    // :6792 One-based, and an underflowed count walks the whole table.
    al++;
    uint16_t dx;
    do {
        dx = (uint16_t)(si[0] | (si[1] << 8));
        if (dx == 0) return NULL;
        si += 2;
    } while (--al != 0);

    return pmd->mmlbuf + dx;
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
    uint8_t al = PMD_FF_MUSIC_NO_BOARD;
    if (!ok) al = PMD_FF_MUSIC_REJECTED;
    else if (pmd->board != 0) al = ff_music_main(pmd, bar);
    int60_exit(pmd);
    return al;
}

// :6820
// Fast-forwards to bar `dx`. The original masks the FM interrupt around this
// call; the host owns that exclusion.
static uint8_t ff_music_main(struct pmd *pmd, uint16_t dx) {
    uint8_t al;

    if (pmd->status2 == 0xff) return PMD_FF_MUSIC_STOPPED;

    pmd->skip_flag = 1;
    if (dx > pmd->syousetu) goto ffm_main;

    // :6835 Restart from the top before skipping forward.
    pmd->skip_flag = 2;
    if (pmd->effon == 1) effend(pmd);
    data_init2(pmd);
    play_init(pmd);
    opn_init(pmd);
    if (dx != 0) goto ffm_main;
    silence(pmd);
    goto ffm_exit0b;

ffm_main:;
    maskon_all(pmd);
#if board2
    opnset44(pmd, 0x10ff); // dump the rhythm source
#endif

    // :6856 The fadeout and rhythm mask are suspended for the skip.
    uint8_t fadeout_volume = pmd->fadeout_volume;
    uint8_t rhythmmask = pmd->rhythmmask;
    pmd->fadeout_volume = 0xff;
    pmd->rhythmmask = 0;

    uint16_t bp = dx;
    for (;;) {
        mmain(pmd);
        syousetu_count(pmd);
        if (pmd->status2 == 0xff) {
            maskoff_all(pmd);
            al = PMD_FF_MUSIC_PAST;
            goto ffm_exit;
        }
        if (bp <= pmd->syousetu) break;
    }

    pmd->fadeout_volume = fadeout_volume;
    pmd->rhythmmask = rhythmmask;
#if board2
    if (fadeout_volume == 0) volset2rf(pmd, pmd->rhyvol);
#endif
    maskoff_all(pmd);

ffm_exit0b:
    // :6890 One less than the fast-forward tempo.
    stb_ff(pmd, (uint8_t)(pmd->ff_tempo - 1));
    al = PMD_FF_MUSIC_OK;
ffm_exit:
    pmd->skip_flag = 0;
    return al;
}

// :6902
static void maskon_all(struct pmd *pmd) {
    const uint8_t(*entry)[3] = part_table;
    for (unsigned i = 0; i < max_part1; i++) {
        struct pmd_qq *di = &pmd->parts[i];
#if ppz
        // :6910 The PPZ8 build's work array continues with the PPZ8 parts, so
        // the Rhythm and Effect entries are skipped here.
        if ((*entry)[0] == 0xff) entry += 2;
#endif
        uint8_t al = (*entry)[1];
        uint8_t ah = (*entry)[2];
        entry++;

        di->partmask |= 0x80;
        if (di->partmask != 0x80) continue; // already masked elsewhere
        maskon_main(pmd, di, al, ah);
    }
}

// :6935
static void maskoff_all(struct pmd *pmd) {
    const uint8_t(*entry)[3] = part_table;
    for (unsigned i = 0; i < max_part1; i++) {
        struct pmd_qq *di = &pmd->parts[i];
#if ppz
        if ((*entry)[0] == 0xff) entry += 2;
#endif
        uint8_t al = (*entry)[1];
        uint8_t ah = (*entry)[2];
        entry++;

        di->partmask &= 0x7f;
        if (di->partmask != 0) continue; // still masked elsewhere
        maskoff_main(pmd, di, al, ah);
    }
}

// :6967
// Masks (AL < 80h) or unmasks (AL >= 80h) one part, keying off or restoring
// its sound.
static void part_mask(struct pmd *pmd, uint8_t al) {
    uint8_t ah = al & 0x7f;
#if ppz
    if (ah >= 16 + 8) return;
#else
    if (ah >= 16) return;
#endif
    if ((al & 0x80) != 0) {
        part_on(pmd, ah);
        return;
    }

    const uint8_t(*entry)[3] = &part_table[ah];
    int8_t dl = (int8_t)(*entry)[0];
    if (dl < 0) {
        rhythm_mask(pmd);
        return;
    }
    uint8_t partb = (*entry)[1];
    uint8_t ongen = (*entry)[2];

    struct pmd_qq *di = pmd->part_data_table[(uint8_t)dl];
    uint8_t previous = di->partmask;
    di->partmask |= 1;
    if (previous != 0) return; // already masked
    if (pmd->play_flag == 0) return; // the music is stopped

    maskon_main(pmd, di, partb, ongen);
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
static void maskon_main(struct pmd *pmd, struct pmd_qq *di, uint8_t al, uint8_t ah) {
    switch (ah) {
    case 0: // pm_fm1
        pmd->partb = al;
#if board2
        sel44(pmd);
#endif
        (void)silence_fmpart(pmd, di);
        return;
#if board2
    case 1: // pm_fm2
        pmd->partb = al;
        sel46(pmd);
        (void)silence_fmpart(pmd, di);
        return;
#endif
    case 2: { // pm_ssg
        pmd->partb = al;
        psgmsk(pmd, &al, &ah);
        opnset44(pmd, (uint16_t)((0x07 << 8) | (al | ah)));
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
        if (al == 7 && pmd->adpcm_emulate == 1) return;
#endif
        {
            const struct pmd_drv_ppz8 *drv = ppz8_ops(pmd);
            if (drv != NULL) drv->keyoff(al, pmd->user_data);
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
// Unmasks one part and restores its voice, given the part number in AH.
static void part_on(struct pmd *pmd, uint8_t ah) {
    const uint8_t(*entry)[3] = &part_table[ah];
    int8_t dl = (int8_t)(*entry)[0];
    if (dl < 0) {
        pmd->rhythmmask = 0xff;
        return;
    }
    uint8_t partb = (*entry)[1];
    uint8_t ongen = (*entry)[2];

    struct pmd_qq *di = pmd->part_data_table[(uint8_t)dl];
    if (di->partmask == 0) return; // not masked
    di->partmask &= (uint8_t)~1;
    if (di->partmask != 0) return; // still masked by an effect
    if (pmd->play_flag == 0) return; // the music is stopped

    maskoff_main(pmd, di, partb, ongen);
}

// :7135
static void maskoff_main(struct pmd *pmd, struct pmd_qq *di, uint8_t al, uint8_t ah) {
    switch (ah) {
    case 0: // po_fm1
        pmd->partb = al;
#if board2
        sel44(pmd);
#endif
        if (di->address != NULL) neiro_reset(pmd, di);
        return;
#if board2
    case 1: // po_fm2
        pmd->partb = al;
        sel46(pmd);
        if (di->address != NULL) neiro_reset(pmd, di);
        return;
#endif
    default:
        // Only the FM sources need their voice restored.
        return;
    }
}

// :7354
static void fm_effect_on(struct pmd *pmd, uint8_t al) {
    if (pmd->fm_effec_flag != 0) fm_effect_off(pmd);

    pmd->fm_effec_num = al;
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

    struct pmd_qq *di = &pmd->part_e;
    memset(di, 0, sizeof(*di));

    uint16_t offset = (uint16_t)(pmd->efcdat[2 * al] | (pmd->efcdat[2 * al + 1] << 8));
    di->address = pmd->efcdat + offset;
    di->leng = 1; // starts on the next count
    di->volume = 108; // FM volume default
    di->slotmask = 0xf0;
    di->neiromask = 0xff;
#if board2
    di->fmpan = 0xc0; // centre
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

INT_check:
#if pcm
    // :7540 Check the PCM86 FIFO interrupt bit.
    if ((inb(pmd, 0xa468) & 0x10) != 0) {
        fifo_main(pmd);
        goto INT_check;
    }
#endif
    // :7556 _ppz
    ppz_int_hook(pmd);
    // :7561-7566
    if ((rdychk(pmd, pmd->fm1_port1) & 3) != 0) {
        FM_Timer_main(pmd);
        goto INT_check;
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

// :7808
// The VA variant's port 8 is active low; translate the normalized bits to
// the original key positions (CTRL bit 7, SHIFT bit 6, CAPS bit 5, GRPH
// bit 4, KANA bit 3).
static PMDC_UNUSED uint8_t key_port8(uint8_t state) {
    uint8_t port = 0;
    if (state & PMD_KEY_CTRL) port |= 0x80;
    if (state & PMD_KEY_SHIFT) port |= 0x40;
    if (state & PMD_KEY_CAPS) port |= 0x20;
    if (state & PMD_KEY_GRPH) port |= 0x10;
    if (state & PMD_KEY_KANA) port |= 0x08;
    return port;
}

// :7831
// Counts bars.
static void syousetu_count(struct pmd *pmd) {
    uint8_t al = (uint8_t)(pmd->opncount + 1);
    if (al == pmd->syousetu_lng) {
        al = 0;
        pmd->syousetu++;
    }
    pmd->opncount = al;
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
static void stb_ff(struct pmd *pmd, uint8_t dl) {
    if (dl == pmd->TimerB_speed) return;
    pmd->TimerB_speed = dl;
    opnset44(pmd, (uint16_t)((0x26 << 8) | dl));
}

// :7864
// Rewinds one bar when the rewind key is held.
static void rew(struct pmd *pmd) {
    if (!check_grph(pmd, pmd->rew_sp_key)) return;
    uint16_t dx = pmd->syousetu;
    uint8_t al = (uint8_t)(pmd->syousetu_lng >> 2);
    if (pmd->opncount < al && dx != 0) dx--;
    ff_music_main(pmd, dx);
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
        uint16_t dx = (uint16_t)(0x088 + (i << 8));

        uint8_t ah = 0xff;
        bool present = false;
        for (unsigned n = 0; n < 256; n++) {
            ah &= inb(pmd, dx);
            if ((ah & 0x80) == 0) {
                present = true;
                break;
            }
        }
        if (!present) continue; // no sound chip on this port

        rdychk(pmd, dx);
        outb(pmd, dx, 0x0e);
        if ((inb(pmd, dx + 2) & 0xc0) != pmd->opn_0eh) continue;

        opnset_fmc(pmd, dx, 0x2900);
        opnset_fmc(pmd, dx, 0x2400);
        opnset_fmc(pmd, dx, 0x2500);
        opnset_fmc(pmd, dx, 0x2600);
        opnset_fmc(pmd, dx, 0x273f);
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
    uint8_t al = inb(pmd, pmd->fm1_port2) & 0xc0;
#if !va
    pmd->opn_0eh = al;
#endif
    // :8929 The original rotates the two interrupt-number bits into a 4-byte
    // table index and then multiplies by 4; the two steps cancel out.
    pmd->mes_int[mes_int_int_num] = vector_table[al >> 6].num;
    pmd->int_level = vector_table[al >> 6].level;

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
// original's intset/PIC/vector work is host-owned and omitted.
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
static void opnset_fmc(struct pmd *pmd, uint16_t dx, uint16_t ax) {
    // The original delays 256 `loop $` iterations before each write (:9179,
    // :9184); the host backend owns the chip timing.
    outb(pmd, dx, (uint8_t)(ax >> 8));
    outb(pmd, dx + 2, (uint8_t)ax);
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
    uint16_t dx = 0x088;
    if (pmd->port_sel != -1) {
        dx = (uint16_t)((dx & 0xff) | (pmd->port_sel << 8));
        tries = 1;
    }
    for (;;) {
        outb(pmd, dx, 0x0b);
        outb(pmd, dx + 2, 0xaa);
        if (inb(pmd, dx + 2) == 0xaa) {
            set_port(pmd, dx);
            return true;
        }
        if (--tries == 0) return false;
        dx = (uint16_t)(dx + 0x100);
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
    uint8_t al = inb(pmd, 0xa460);
    if (al == 0xff) return false;
    outb(pmd, 0x5f, al);
    outb(pmd, 0xa460, (uint8_t)(al & 0xfc)); // NEC OPNA off

    uint8_t tries = 2;
    uint16_t dx = 0x188;
    if (pmd->port_sel != -1) {
        dx = (uint16_t)((dx & 0xff) | (pmd->port_sel << 8));
        tries = 1;
    }
    for (;;) {
        outb(pmd, dx, 0xff);
        if (inb(pmd, dx + 2) == 1) { // YM2608?
            uint8_t bl = inb(pmd, dx + 4);
            if ((uint8_t)((inb(pmd, dx + 6) & bl) + 1) == 0) break;
        }
        if (--tries == 0) return false;
        dx = (uint16_t)(dx + 0x100);
    }

    // :9133
    al = inb(pmd, 0xa460);
    outb(pmd, 0x5f, al);
    outb(pmd, 0xa460, (uint8_t)((al & 0xfc) | 1)); // NEC OPNA on
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
    set_port(pmd, dx);
    return true;
}

#if !pcm
// :9406
// Probes for an SPB (YM2608) at port_sel or 088h-388h. Returns false when no
// board was found.
static PMDC_UNUSED bool check_spb(struct pmd *pmd) {
    pmd->ongen = 1;
    // The EPSON ROM reads are omitted as in check_86b (:9412). Unlike
    // check_86b, an EPSON machine only skips the OPNA off write here.
    uint8_t al = inb(pmd, 0xa460);
    if (al != 0xff) {
        outb(pmd, 0x5f, al);
        outb(pmd, 0xa460, (uint8_t)(al & 0xfc)); // NEC OPNA off
    }

    uint8_t tries = 4;
    uint16_t dx = 0x088;
    if (pmd->port_sel != -1) {
        dx = (uint16_t)((dx & 0xff) | (pmd->port_sel << 8));
        tries = 1;
    }
    for (;;) {
        outb(pmd, dx, 0xff);
        if (inb(pmd, dx + 2) == 1) { // YM2608?
            uint8_t bl = inb(pmd, dx + 4);
            if ((uint8_t)((inb(pmd, dx + 6) & bl) + 1) != 0) {
                set_port(pmd, dx);
                return true;
            }
        }
        if (--tries == 0) return false;
        dx = (uint16_t)(dx + 0x100);
    }
}
#endif

// :9468
// Sets fm1/fm2 from the base port a probe matched.
static void set_port(struct pmd *pmd, uint16_t dx) {
    pmd->fm1_port1 = dx;
    pmd->fm1_port2 = (uint16_t)(dx + 2);
    pmd->fm2_port1 = (uint16_t)(dx + 4);
    pmd->fm2_port2 = (uint16_t)(dx + 6);
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
// Prints AL as a decimal number without leading zeros.
static void put8(struct pmd *pmd, uint8_t al) {
    bool printed = false;
    p8_oneset(pmd, &al, 100, &printed);
    p8_oneset(pmd, &al, 10, &printed);
    print_char(pmd, (char)('0' + al));
}

// :9726
static void p8_oneset(struct pmd *pmd, uint8_t *al, uint8_t dl, bool *printed) {
    char dh = '0';
    while (*al >= dl) {
        *al = (uint8_t)(*al - dl);
        dh++;
    }
    if (*printed || dh != '0') {
        print_char(pmd, dh);
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
        unsigned char c = (unsigned char)lodsb(&cmd);
        if (c == '/' || c == '-') {
            c = (unsigned char)lodsb(&cmd) & 0xdf;
            const struct pmd_option *entry = table;
            for (;;) {
                if (entry->name == 0) return usage(pmd, from_env);
                if ((unsigned char)entry->name == c) break;
                entry++;
            }
            if (!entry->fn(pmd, &cmd, from_env)) return false;
            continue;
        }
        if (c < ' ') return true;
        if (c == ' ') continue;
        return usage(pmd, from_env);
    }
}

// :9928
// Reads one option character. The original's lodsb always advances SI; this
// cursor stops on the null terminator, which no handler reads past.
static char lodsb(const char **cmd) {
    if (**cmd == '\0') return '\0';
    return *(*cmd)++;
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
    unsigned char c = (unsigned char)lodsb(cmd) & 0xdf;
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
    unsigned char c = (unsigned char)**cmd;
    if (c == '-') {
        (*cmd)++;
        pmd->key_check = 1;
        return true;
    }
    c &= 0xdf;
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
            for (uint8_t ch = 0; ch < 8; ch++) {
                pmd->ppz8->keyoff(ch, pmd->user_data); // 0200h-0207h
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
    const char *p = *cmd;
    uint8_t n = 0;
    bool found = false;
    for (;;) {
        unsigned digit = (unsigned char)(*p - '0');
        if (digit >= 10) break;
        n = (uint8_t)(n * 10 + digit);
        p++;
        found = true;
    }
    if (found) *cmd = p;
    *out = n;
    return found;
}

// :10350
// Prints AX as a decimal number without leading zeros.
static void print_16(struct pmd *pmd, uint16_t ax) {
    bool printed = false;
    p16_oneset(pmd, &ax, 10000, &printed);
    p16_oneset(pmd, &ax, 1000, &printed);
    p16_oneset(pmd, &ax, 100, &printed);
    p16_oneset(pmd, &ax, 10, &printed);
    print_char(pmd, (char)('0' + ax));
}

// :10366
static void p16_oneset(struct pmd *pmd, uint16_t *ax, uint16_t bx, bool *printed) {
    char dl = '0';
    while (*ax >= bx) {
        *ax = (uint16_t)(*ax - bx);
        dl++;
    }
    if (*printed || dl != '0') {
        print_char(pmd, dl);
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
    unsigned char c = (unsigned char)lodsb(cmd) & 0xdf;
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
    if (pcmstore(pmd, check_data)) goto found_ymf288;

    // :29 The saved speed skips the faster settings.
    if (wait != 2 && wait != 1) {
        pmd->adpcm_wait = 0;
        pcmstore(pmd, (const uint8_t *)check_code);
        pcmread(pmd, check_data);
        if (pcmcompare(check_data)) goto arc_found_exit;
    }
    if (wait != 2) {
        pmd->adpcm_wait = 1;
        pcmstore(pmd, (const uint8_t *)check_code);
        pcmread(pmd, check_data);
        if (pcmcompare(check_data)) goto arc_found_exit;
    }
    pmd->adpcm_wait = 2;
    pcmstore(pmd, (const uint8_t *)check_code);
    pcmread(pmd, check_data);
    if (!pcmcompare(check_data)) goto arc_no_exit;

arc_found_exit: // :57 the ADPCM RAM is present
    pmd->pcm_gs_flag = 0;
    return;

found_ymf288: // :61
    // ADRAMCHK.ASM:62 overwrites the "2608" in mes_ongen1 with "F288".
    memcpy(pmd->mes_ongen1 + 2, "F288", 4);
arc_no_exit:
    pmd->pcm_gs_flag = 1;
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

    // The original's cli_sub/sti_sub (:252, :269) only mask the PIC for the
    // duration of the transfer; the host owns that exclusion.
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
        // :8690
        opnset44(pmd, 0x2900);
        opnset44(pmd, 0x2400);
        opnset44(pmd, 0x2500);
        opnset44(pmd, 0x2600);
        opnset44(pmd, 0x273f);
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

    // :8830 Releasing the DOS environment block is host-owned.

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
