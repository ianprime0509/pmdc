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

// :151
PMDC_NORETURN static void error_exit(struct pmd *pmd, int status);
// :173
static void print_mes(struct pmd *pmd, const char *mes);
// :218
static PMDC_UNUSED void _wait(struct pmd *pmd);
// :223
static PMDC_UNUSED void _waitP(struct pmd *pmd);
// :230
static PMDC_UNUSED void _rwait(struct pmd *pmd);
// :242
static void rdychk(struct pmd *pmd, uint16_t port);
#if board2
// :1029
static PMDC_UNUSED void sel46(struct pmd *pmd);
// :1039
static PMDC_UNUSED void sel44(struct pmd *pmd);
#endif
// :6173
static PMDC_UNUSED void opnset44(struct pmd *pmd, uint16_t dx);
#if board2
// :6200
static PMDC_UNUSED void opnset46(struct pmd *pmd, uint16_t dx);
#endif
// :6218
static PMDC_UNUSED void opnset(struct pmd *pmd, uint16_t dx);
// :6253
static PMDC_UNUSED uint8_t get07(struct pmd *pmd);
// :8385
static void resident_exit_end(struct pmd *pmd);
// :8555
static bool _set_option(struct pmd *pmd, const struct pmd_option *table,
                        const char *cmd);
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
// :9036
static bool ppsdrv_check(struct pmd *pmd);
#if ppz
// :9110
static bool ppz8_check(struct pmd *pmd);
#endif
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
static void p8_oneset(struct pmd *pmd, uint8_t *al, uint8_t dl,
                      bool *printed);
// :9753
static bool resident_cut_main(struct pmd *pmd, const char **cmd,
                              bool from_env);
// :9927
static bool set_option(struct pmd *pmd, const struct pmd_option *table,
                       const char *cmd, bool from_env);
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
static bool grph_special_set(struct pmd *pmd, const char **cmd,
                             bool from_env);
// :10081
static bool rew_special_set(struct pmd *pmd, const char **cmd,
                            bool from_env);
// :10095
static bool esc_special_set(struct pmd *pmd, const char **cmd,
                            bool from_env);
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
static bool message_clear_set(struct pmd *pmd, const char **cmd,
                              bool from_env);
// :10314
static bool get_comline_number(const char **cmd, uint8_t *out);
// :10350
static void print_16(struct pmd *pmd, uint16_t ax);
// :10366
static void p16_oneset(struct pmd *pmd, uint16_t *ax, uint16_t bx,
                       bool *printed);
// :10440
PMDC_NORETURN static bool pmderr_5(struct pmd *pmd, const char **cmd,
                                   bool from_env);
// :10444
PMDC_NORETURN static bool pmderr_6(struct pmd *pmd, const char **cmd,
                                   bool from_env);
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
// not implemented; they are no-delay points until the call sites land.
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
// Discards one status read, then waits for the BUSY bit to clear.
static void rdychk(struct pmd *pmd, uint16_t port) {
    (void)inb(pmd, port);
    while (inb(pmd, port) & 0x80) {
    }
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

// The original brackets every register write with pushf/cli/sti and restores
// the interrupt flag afterwards. The host owns that exclusion (see the
// interrupt/timer section of the plan), so these helpers perform the I/O only.
//
// :6173
static PMDC_UNUSED void opnset44(struct pmd *pmd, uint16_t dx) {
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

// :6218
static PMDC_UNUSED void opnset(struct pmd *pmd, uint16_t dx) {
    rdychk(pmd, pmd->fm_port1);
    outb(pmd, pmd->fm_port1, (uint8_t)(dx >> 8));
    _waitP(pmd);
    outb(pmd, pmd->fm_port2, (uint8_t)dx);
}

// :6253
// Reads PSG register 07h. The original requires its callers to run with
// interrupts disabled.
static PMDC_UNUSED uint8_t get07(struct pmd *pmd) {
    rdychk(pmd, pmd->fm1_port1);
    outb(pmd, pmd->fm1_port1, 7);
    _waitP(pmd);
    return inb(pmd, pmd->fm1_port2);
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
static bool _set_option(struct pmd *pmd, const struct pmd_option *table,
                        const char *cmd) {
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
// (:9020) are host-owned: pmd_hw.install_fm_timer_handler provides the timer
// source in the timer phase.
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

// :9036
// PPSDRV is available when the host supplies its vtable; the original checks
// INT 64h's resident signature instead.
static bool ppsdrv_check(struct pmd *pmd) { return pmd->pps != NULL; }

#if ppz
// :9110
// Same as ppsdrv_check for PPZ8 (INT 7Fh in the original).
static bool ppz8_check(struct pmd *pmd) { return pmd->ppz8 != NULL; }
#endif

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
    // :9152 stop_86pcm (PCMDRV86.ASM) stops PCM playback before the port is
    // used; it arrives with the PCM86 driver in phase 8.
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
// exits. The port's removal sequence lands in pmd_resident_cut (phase 6), and
// pmd_change_main reports PMD_CHANGE_REMOVED instead.
static bool resident_cut_main(struct pmd *pmd, const char **cmd,
                              bool from_env) {
    (void)cmd;
    (void)from_env;
    // TODO(phase 6): pmd_resident_cut is still a no-op, so the
    // PMD_CHANGE_REMOVED that pmd_change_main returns for this option is a
    // forward declaration rather than a report that the driver is down.
    // Nothing may rely on it until the shutdown sequence lands.
    pmd_resident_cut(pmd);
    return false;
}

// :9927
// Scans `cmd` for slash/dash options and dispatches them through `table`
// (:9939). Returns true at the end of the command line, false when parsing
// must stop: a bad PMDOPT option (`usage`) or /R.
static bool set_option(struct pmd *pmd, const struct pmd_option *table,
                       const char *cmd, bool from_env) {
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
static bool grph_special_set(struct pmd *pmd, const char **cmd,
                            bool from_env) {
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
static bool rew_special_set(struct pmd *pmd, const char **cmd,
                           bool from_env) {
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
static bool esc_special_set(struct pmd *pmd, const char **cmd,
                           bool from_env) {
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
static bool message_clear_set(struct pmd *pmd, const char **cmd,
                              bool from_env) {
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
static void p16_oneset(struct pmd *pmd, uint16_t *ax, uint16_t bx,
                       bool *printed) {
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
PMDC_NORETURN static bool pmderr_5(struct pmd *pmd, const char **cmd,
                                   bool from_env) {
    (void)cmd;
    (void)from_env;
    pmderr_main(pmd, pmderror_mes5);
}

// :10444
PMDC_NORETURN static bool pmderr_6(struct pmd *pmd, const char **cmd,
                                   bool from_env) {
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

// :8384
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
        // interrupt source, and the timer phase installs the handler through
        // pmd_hw.install_fm_timer_handler.
    }

    // :8773 The INT 60h vector save/install is omitted: the typed public
    // functions replace the register dispatcher.

    // :8790 opnint_start is playback-dependent (data_init, opn_init, mstop,
    // setint) and arrives with the timer and sequencer phases.

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
            // resident_cut_main (/R) stops the parse; pmd_resident_cut is the
            // phase 6 removal sequence. The only other early stop is a PMDOPT
            // usage error, which cannot happen while parsing the command line.
            return PMD_CHANGE_REMOVED;
        }
        print_mes(pmd, changemes_0);
    }

    put_status(pmd);
    return PMD_CHANGE_OK;
}

// :9753
void pmd_resident_cut(struct pmd *pmd) {
    (void)pmd;
    // Filled in when the shutdown sequence is translated (phase 6, with the
    // PSG-effect part completed in phase 8).
}
