// For getopt_long and nanosleep in the POSIX wait loop.
#define _GNU_SOURCE

#include "pmd.h"
#include "pmd_stdio.h"
#include "pmd_ymfm.h"

#include "miniaudio.h"

#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

enum {
    playback_frames = 512,
    // 16-bit stereo.
    wav_bytes_per_frame = 4,
    wav_header_size = 44,
};

struct playback {
    struct pmd_ymfm *ymfm;
    int32_t samples[2 * playback_frames];
};

static volatile sig_atomic_t interrupted;

static void handle_signal(int signal_number) {
    (void)signal_number;
    interrupted = 1;
}

static void sleep_ms(unsigned milliseconds) {
#ifdef _WIN32
    Sleep(milliseconds);
#else
    struct timespec duration = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (long)(milliseconds % 1000) * 1000000L,
    };
    nanosleep(&duration, NULL);
#endif
}

static int16_t to_pcm16(int32_t sample) {
    if (sample > INT16_MAX) return INT16_MAX;
    if (sample < INT16_MIN) return INT16_MIN;
    return (int16_t)sample;
}

static void playback_callback(ma_device *device, void *output, const void *input,
                              ma_uint32 frame_count) {
    (void)input;
    struct playback *playback = device->pUserData;
    int16_t *out = output;

    while (frame_count != 0) {
        ma_uint32 frames =
            frame_count < (ma_uint32)playback_frames ? frame_count : (ma_uint32)playback_frames;
        pmd_ymfm_generate(playback->ymfm, frames, playback->samples);
        for (ma_uint32 i = 0; i < 2 * frames; i++) {
            out[i] = to_pcm16(playback->samples[i]);
        }
        out += 2 * frames;
        frame_count -= frames;
    }
}

static void put_u16le(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static void put_u32le(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

// Builds a 16-bit stereo PCM header. The data chunk size is only known once
// rendering is done, so the header is written twice.
static void build_wav_header(uint8_t *header, uint32_t sample_rate,
                             uint32_t data_size) {
    memcpy(header, "RIFF", 4);
    put_u32le(header + 4, 36 + data_size);
    memcpy(header + 8, "WAVEfmt ", 8);
    put_u32le(header + 16, 16);
    put_u16le(header + 20, 1); // PCM
    put_u16le(header + 22, 2); // channels
    put_u32le(header + 24, sample_rate);
    put_u32le(header + 28, sample_rate * wav_bytes_per_frame);
    put_u16le(header + 32, wav_bytes_per_frame);
    put_u16le(header + 34, 16); // bits per sample
    memcpy(header + 36, "data", 4);
    put_u32le(header + 40, data_size);
}

// Renders one pass through the module to a WAV file. The driver bumps status2
// once every part reaches its global loop point (the `L` command) or the end
// of its data, which is where a looping module restarts and the pass ends.
static bool render_wav(const char *path, struct pmd *pmd, struct pmd_ymfm *ymfm) {
    uint32_t sample_rate = pmd_ymfm_sample_rate(ymfm);
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        perror(path);
        return false;
    }

    uint8_t header[wav_header_size];
    build_wav_header(header, sample_rate, 0);
    bool ok = fwrite(header, 1, sizeof(header), file) == sizeof(header);

    int32_t samples[2];
    int16_t pcm16[2 * playback_frames];
    uint32_t frames = 0;
    uint32_t data_size = 0;
    uint8_t initial_status = pmd_get_status(pmd).status2;

    while (ok && pmd_get_status(pmd).status2 == initial_status) {
        // One frame at a time ends the pass on the sample the loop point lands
        // on, with no tail of the repeat.
        pmd_ymfm_generate(ymfm, 1, samples);
        pcm16[2 * frames] = to_pcm16(samples[0]);
        pcm16[2 * frames + 1] = to_pcm16(samples[1]);
        if (++frames < playback_frames) continue;
        ok = fwrite(pcm16, sizeof(pcm16), 1, file) == 1;
        data_size += playback_frames * wav_bytes_per_frame;
        frames = 0;
    }
    if (ok && frames != 0) {
        size_t size = frames * wav_bytes_per_frame;
        ok = fwrite(pcm16, size, 1, file) == 1;
        data_size += size;
    }
    if (ok) {
        build_wav_header(header, sample_rate, data_size);
        ok = fseek(file, 0, SEEK_SET) == 0 &&
             fwrite(header, 1, sizeof(header), file) == sizeof(header);
    }
    if (fclose(file) != 0) {
        perror(path);
        ok = false;
    }
    return ok;
}

static void print_usage(const char *program) {
    fprintf(stderr,
            "usage: %s [--chip CHIP] [--board BOARD] [-o FILE] MODULE [VOICE]\n"
            "  MODULE  PMD module file\n"
            "  VOICE   instrument bank file\n"
            "  CHIP    ym2203, ym2608, or ymf288\n"
            "  BOARD   pc98, pc98-86b, or va\n"
            "  -c, --chip CHIP    select the FM chip\n"
            "  -b, --board BOARD  select the FM board\n"
            "  -o, --output FILE  write WAV audio to FILE instead of playing\n",
            program);
}

static bool parse_chip(const char *name, enum pmd_ymfm_chip *chip) {
    if (strcmp(name, "ym2203") == 0 || strcmp(name, "2203") == 0) {
        *chip = PMD_YMFM_YM2203;
    } else if (strcmp(name, "ym2608") == 0 || strcmp(name, "2608") == 0) {
        *chip = PMD_YMFM_YM2608;
    } else if (strcmp(name, "ymf288") == 0 || strcmp(name, "f288") == 0) {
        *chip = PMD_YMFM_YMF288;
    } else {
        return false;
    }
    return true;
}

static bool parse_board(const char *name, enum pmd_ymfm_board *board) {
    if (strcmp(name, "pc98") == 0 || strcmp(name, "pc-98") == 0) {
        *board = PMD_YMFM_BOARD_PC98;
    } else if (strcmp(name, "pc98-86b") == 0 || strcmp(name, "86b") == 0) {
        *board = PMD_YMFM_BOARD_PC98_86B;
    } else if (strcmp(name, "va") == 0) {
        *board = PMD_YMFM_BOARD_VA;
    } else {
        return false;
    }
    return true;
}

static uint16_t board_base_port(enum pmd_ymfm_board board) {
    switch (board) {
    case PMD_YMFM_BOARD_VA:
        return 0x44;
    case PMD_YMFM_BOARD_PC98_86B:
        return 0x188;
    case PMD_YMFM_BOARD_PC98:
        break;
    }
    return 0x88;
}

enum parse_result {
    PARSE_OK,
    PARSE_HELP,
    PARSE_ERROR,
};

struct options {
    const char *module;
    const char *voice;
    const char *output;
    enum pmd_ymfm_chip chip;
    enum pmd_ymfm_board board;
    bool chip_set;
    bool board_set;
};

static enum parse_result parse_args(int argc, char **argv, struct options *options) {
    memset(options, 0, sizeof(*options));

    static const struct option long_options[] = {
        {"chip", required_argument, NULL, 'c'},
        {"board", required_argument, NULL, 'b'},
        {"output", required_argument, NULL, 'o'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "c:b:o:h", long_options, NULL)) != -1) {
        switch (opt) {
        case 'c':
            if (!parse_chip(optarg, &options->chip)) {
                fprintf(stderr, "%s: invalid chip '%s'\n", argv[0], optarg);
                return PARSE_ERROR;
            }
            options->chip_set = true;
            break;
        case 'b':
            if (!parse_board(optarg, &options->board)) {
                fprintf(stderr, "%s: invalid board '%s'\n", argv[0], optarg);
                return PARSE_ERROR;
            }
            options->board_set = true;
            break;
        case 'o':
            options->output = optarg;
            break;
        case 'h':
            print_usage(argv[0]);
            return PARSE_HELP;
        default:
            return PARSE_ERROR;
        }
    }

    for (int i = optind; i < argc; i++) {
        if (options->module == NULL) {
            options->module = argv[i];
        } else if (options->voice == NULL) {
            options->voice = argv[i];
        } else {
            fprintf(stderr, "%s: unexpected argument '%s'\n", argv[0], argv[i]);
            return PARSE_ERROR;
        }
    }

    if (options->module == NULL) {
        print_usage(argv[0]);
        return PARSE_ERROR;
    }
    return PARSE_OK;
}

static bool load_file(const char *path, uint8_t *data, size_t size) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        perror(path);
        return false;
    }

    size_t read = fread(data, 1, size, file);
    bool ok = ferror(file) == 0;
    if (ok && read == size) {
        if (fgetc(file) != EOF) {
            fprintf(stderr, "%s: file does not fit in %zu bytes\n", path, size);
            ok = false;
        } else if (ferror(file) != 0) {
            perror(path);
            ok = false;
        }
    }
    if (fclose(file) != 0) {
        perror(path);
        ok = false;
    }
    return ok;
}

int main(int argc, char **argv) {
    struct options options;
    switch (parse_args(argc, argv, &options)) {
    case PARSE_OK:
        break;
    case PARSE_HELP:
        return EXIT_SUCCESS;
    case PARSE_ERROR:
        return EXIT_FAILURE;
    }

    static struct pmd pmd;
    pmd.sys = &pmd_sys_stdio;
    pmd.pps = NULL;
    pmd.ppz8 = NULL;
    pmd.p86 = NULL;

    struct pmd_ymfm_config config;
    pmd_ymfm_default_config(&config, &pmd);
    if (options.chip_set) config.chip = options.chip;
    if (options.board_set) {
        config.board = options.board;
        config.base_port = board_base_port(options.board);
    }

    struct pmd_ymfm *ymfm = pmd_ymfm_create(&config);
    if (ymfm == NULL) {
        fprintf(stderr, "%s: failed to create the FM chip\n", argv[0]);
        return EXIT_FAILURE;
    }
    pmd.hw = &pmd_hw_ymfm;
    pmd.user_data = ymfm;

    pmd_ymfm_load_adpcm_a_file(ymfm, "ym2608_adpcm_rom.bin");

    pmd_init(&pmd);
    pmd_resident_main(&pmd, "");

    int status = EXIT_FAILURE;
    if (pmd.board == 0) {
        fprintf(stderr, "%s: no supported FM board found\n", argv[0]);
        goto cleanup;
    }

    size_t music_size = (size_t)pmd.mmldat_lng * 1024;
    uint8_t *music = pmd_get_musdat_adr(&pmd);
    memset(music, 0, music_size);
    if (!load_file(options.module, music, music_size)) goto cleanup;

    if (options.voice != NULL) {
        size_t voice_size = (size_t)pmd.voicedat_lng * 1024;
        uint8_t *voice = pmd_get_tondat_adr(&pmd);
        memset(voice, 0, voice_size);
        if (!load_file(options.voice, voice, voice_size)) goto cleanup;
    }

    if (!pmd_mstart_f(&pmd)) {
        fprintf(stderr, "%s: failed to start playback\n", argv[0]);
        goto cleanup;
    }

    if (options.output != NULL) {
        if (!render_wav(options.output, &pmd, ymfm)) goto cleanup;
        status = EXIT_SUCCESS;
        goto cleanup;
    }

    struct playback playback = {.ymfm = ymfm};
    ma_device_config device_config = ma_device_config_init(ma_device_type_playback);
    device_config.playback.format = ma_format_s16;
    device_config.playback.channels = 2;
    device_config.sampleRate = pmd_ymfm_sample_rate(ymfm);
    device_config.dataCallback = playback_callback;
    device_config.pUserData = &playback;

    ma_device device;
    if (ma_device_init(NULL, &device_config, &device) != MA_SUCCESS) {
        fprintf(stderr, "%s: failed to initialize audio output\n", argv[0]);
        goto cleanup;
    }
    if (ma_device_start(&device) != MA_SUCCESS) {
        fprintf(stderr, "%s: failed to start audio output\n", argv[0]);
        ma_device_uninit(&device);
        goto cleanup;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    printf("Playing %s (press Ctrl+C to stop).\n", options.module);
    fflush(stdout);
    while (!interrupted) sleep_ms(100);

    ma_device_uninit(&device);
    status = EXIT_SUCCESS;

cleanup:
    pmd_resident_cut(&pmd);
    pmd_ymfm_destroy(ymfm);
    return status;
}
