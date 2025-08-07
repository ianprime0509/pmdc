#include "pmd.h"

#include <string.h>

// :12
enum {
    mdata_def = 16,
    voice_def = 8,
    effect_def = 4,
    key_def = 1,
};

// :21
#ifndef va
#define va 0
#endif
// :24
#ifndef board2
#define board2 0
#endif
// :27
#ifndef adpcm
#define adpcm 0
#endif
// :30
#ifndef ademu
#define ademu 0
#endif
// :33
#ifndef pcm
#define pcm 0
#endif
// :36
#ifndef ppz
#define ppz 0
#endif
// :39
#ifndef sync
#define sync 0
#endif
// :42
#ifndef vsync
#define vsync 0
#endif

#include "strings_jp.h"

// Character classification helpers
//
// These functions work slightly differently from their ctype.h counterparts,
// since they must work correctly with Shift JIS and replicate quirks within the
// original code (such as DEL not being considered a control character).

static bool is_cntrl(char c) { return c >= 0 && c < ' '; }
static bool is_graph(char c) { return !is_cntrl(c) && c != ' '; }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_upper(char c) { return c >= 'A' && c <= 'Z'; }
static bool is_lower(char c) { return c >= 'a' && c <= 'z'; }
static char to_upper(char c) { return is_lower(c) ? c - 'a' + 'A' : c; }

static void outb(struct pmd *pmd, uint16_t port, uint8_t data) {
    pmd->sys->outb(port, data, pmd->user_data);
}

// :49
enum {
#if va + board2
    fmvd_init = 0,
#else
    fmvd_init = 16,
#endif
};

// :8263
enum {
#if board2
# if ppz
    max_part1 = 14 + 8,
    max_part2 = 11,
# else
    max_part1 = 14,
    max_part2 = 11,
# endif
#else
    max_part1 = 11,
    max_part2 = 11,
#endif
};

struct option {
    char c;
    int (*impl)(struct pmd *pmd);
};

// :10759
static const struct option resident_option[] = {
    // TODO
    {0},
};

// :146
PMDC_NORETURN static void msdos_exit(struct pmd *pmd) {
    pmd->sys->exit(0, pmd->user_data);
}

// :151
PMDC_NORETURN static void error_exit(struct pmd *pmd, int qq) {
    pmd->sys->exit(qq, pmd->user_data);
}

// :156
static void print_mes(struct pmd *pmd, const char *qq) {
    // The VA version of the driver uses a different (non-standard?) interrupt
    // for printing, which doesn't matter for our purposes.
    pmd->sys->print(qq, pmd->user_data);
}

// :218
static void _wait(struct pmd *pmd) {
    pmd->sys->wait(pmd->wait_clock, pmd->user_data);
}

// :242
static void rdychk(struct pmd *pmd) {
    // TODO
    (void)pmd;
}

// :626
static void data_init(struct pmd *pmd) {
    pmd->fadeout_volume = 0;
    pmd->fadeout_speed = 0;
    pmd->fadeout_flag = 0;

    // :632
    data_init2(pmd);
}

// :632
static void data_init2(struct pmd *pmd) {
    for (int i = 0; i < max_part1; i++) {
        uint8_t partmask = pmd->parts[i].partmask;
        uint8_t keyon_flag = pmd->parts[i].keyon_flag;
        memset(&pmd->parts[i], 0, sizeof(pmd->parts[i]));
        pmd->parts[i].partmask = partmask & 0x0f;
        pmd->parts[i].keyon_flag = keyon_flag;
        pmd->parts[i].onkai = -1;
        pmd->parts[i].onkai_def = -1;
    }

    // :654
    pmd->tieflag = 0;
    pmd->status = 0;
    pmd->status2 = 0;
    pmd->syousetu = 0;
    pmd->opncount = 0;
    pmd->TimerAtime = 0;
    pmd->lastTimerAtime = 0;
    // :663
    memset(pmd->omote_keys, 0, sizeof(pmd->omote_keys));
    // :667
    pmd->fm3_alg_fb = 0;
    pmd->af_check = 0;
    // :670
    pmd->pcmstart = 0;
    pmd->pcmstop = 0;
    pmd->pcmrepeat1 = 0;
    pmd->pcmrepeat2 = 0;
    pmd->pcmrelease = 0x8000;
    // :676
    pmd->kshot_dat = 0;
    pmd->rshot_dat = 0;
    pmd->last_shot_data = 0;
    // :680
    pmd->slotdetune_flag = 0;
    pmd->slot_detune1 = 0;
    pmd->slot_detune2 = 0;
    pmd->slot_detune3 = 0;
    pmd->slot_detune4 = 0;
    // :685
    pmd->slot3_flag = 0;
    pmd->ch3mode = 0x3f;
    // :688
    pmd->fmsel = 0;
    // :690
    pmd->syousetu_lng = 96;
    // :692
    pmd->fm_port1 = pmd->fm1_port1;
    pmd->fm_port2 = pmd->fm1_port2;
    // :697
    pmd->fm_voldown = pmd->_fm_voldown;
    pmd->ssg_voldown = pmd->_ssg_voldown;
    pmd->pcm_voldown = pmd->_pcm_voldown;
#if ppz
    pmd->ppz_voldown = pmd->_ppz_voldown;
#endif
    pmd->rhythm_voldown = pmd->_rhythm_voldown;
    // :710
    pmd->pcm86_vol = pmd->_pcm86_vol;
}

// :718
static void opn_init(struct pmd *pmd) {
    opnset44(pmd, 0x29, 0x83);
    // :722
    pmd->psnoi = 0;
    if (pmd->effon == 0) {
        // :726
        opnset44(pmd, 0x06, 0x00);
        pmd->psnoi_last = 0;
    }

    // :729
#if board2
    sel44(pmd);
    sr01(pmd);
    sel46(pmd);
    sr01(pmd);
    sel44(pmd);
#else
    sr01(pmd);
#endif

    // :755
#if !board2
    if (pmd->ongen == 0) return;
#endif
    // The remaining logic is specific to the YM2608.

    // :763
#if board2
    pd01(pmd);
    sel46(pmd);
    pd01(pmd);
    sel44(pmd);
#else
    pd01(pmd);
#endif

    // :792
    pmd->port22h = 0x00;
    opnset44(pmd, 0x22, 0x00);

    // :796
#if board2
    memset(pmd->rdat, 0xcf, 6);
    opnset44(pmd, 0x10, 0xff);
    // :810
    uint8_t rhyvol = 48;
    if (pmd->rhythm_voldown != 0) {
        // :815
        // TODO: audit this
        rhyvol = (rhyvol * 4) * (uint8_t)-pmd->rhythm_voldown / 4;
    }
    opnset44(pmd, 0x11, pmd->rhyvol = rhyvol);

    // :831
# if !ademu
    if (pmd->pcm_gs_flag != 1) {
        // :834
        opnset46(pmd, 0x0c, 0xff);
        opnset46(pmd, 0x0d, 0xff);
    }
# endif

    // :843
# if ppz + ademu
    // TODO: PPZ functionality
# endif
#else
    // :854
    // TODO: why is this here?
    rdychk(pmd);
    outb(pmd, pmd->fm2_port1, 0x10);
    _wait(pmd);
    outb(pmd, pmd->fm2_port2, 0x80);
    _wait(pmd);
    outb(pmd, pmd->fm2_port2, 0x18);
#endif
}

// :736
static void sr01(struct pmd *pmd) {
    // The loop logic has been rewritten here for comprehensibility.
    for (int i = 0; i < 15; i++) {
        if (i % 4 != 0) {
            opnset(pmd, 0x90 + i, 0x00);
        }
    }
}

// :768
static void pd01(struct pmd *pmd) {
    // The loop logic has been rewritten here for comprehensibility.
    for (int i = 0; i < 3; i++) {
        bool is_last_channel;
#if board2
        is_last_channel = pmd->fmsel == 1 && i == 2;
#else
        is_last_channel = i == 2;
#endif
        if (pmd->fm_effec_flag == 0 || !is_last_channel) {
            opnset(pmd, 0xb4 + i, 0xc0);
        }
    }
}

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

// :6173
static void opnset44(struct pmd *pmd, uint8_t addr, uint8_t data) {
    rdychk(pmd);
    outb(pmd, pmd->fm1_port1, addr);
    _wait(pmd);
    outb(pmd, pmd->fm1_port2, data);
}

// :6200
static void opnset46(struct pmd *pmd, uint8_t addr, uint8_t data) {
    rdychk(pmd);
    outb(pmd, pmd->fm2_port1, addr);
    _wait(pmd);
    outb(pmd, pmd->fm2_port2, data);
}

// :6226
static void opnset(struct pmd *pmd, uint8_t addr, uint8_t data) {
    rdychk(pmd);
    outb(pmd, pmd->fm_port1, addr);
    _wait(pmd);
    outb(pmd, pmd->fm_port2, data);
}

// :8417
static void comstart(struct pmd *pmd, char *cmdline) {
    // TODO: what is the purpose of the "virus check" logic?

    // :8429
    print_mes(pmd, mes_title);
    // :8433
    pmd->opt_sp_push = 0;

    // :8438
    // TODO: resident functionality not implemented

    // :8471
    pmd->mmldat_lng = mdata_def;
    pmd->voicedat_lng = voice_def;
    pmd->effecdat_lng = effect_def;
    pmd->key_check = key_def;
    pmd->fm_voldown = pmd->_fm_voldown = fmvd_init;
    pmd->ssg_voldown = pmd->_ssg_voldown = 0;
    pmd->pcm_voldown = pmd->_pcm_voldown = 0;
    pmd->ppz_voldown = pmd->_ppz_voldown = 0;
    pmd->rhythm_voldown = pmd->_rhythm_voldown = 0;
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
    pmd->pcm86_vol = pmd->_pcm86_vol = 0;
    pmd->fade_stop_flag = 1;
    pmd->ppsdrv_flag = -1;
#if va
    pmd->grph_sp_key = 0x80;
    pmd->rew_sp_key = 0x40;
    pmd->esc_sp_key = 0x80;
#else
    pmd->grph_sp_key = 0x10;
    pmd->rew_sp_key = 1;
    pmd->esc_sp_key = 0x10;
    pmd->port_sel = -1;
#endif
    pmd->ff_tempo = 250;
    pmd->music_flag = 0;
    pmd->message_flag = 1;

    // :8529
    fm_check(pmd);

    // :8538
    char *env_cmdline = search_env(pmd, "PMDOPT");
    if (env_cmdline) {
        pmd->si = env_cmdline;
        // In the original, opt_sp_push actually stores the stack pointer, as a
        // way to do longjmp from the usage message printing code to this point.
        // This version does not use longjmp, and opt_sp_push is only used as a
        // flag to indicate that the options are being parsed from the
        // environment rather than the command line.
        pmd->opt_sp_push = 1;
        set_option(pmd, resident_option);
    }

    // :8561
    if (*cmdline != 0) {
        // :8569
        pmd->si = cmdline;
        set_option(pmd, resident_option);
        // :8574
        if (pmd->mmldat_lng < 1) pmderr_2(pmd);
        if (pmd->mmldat_lng + pmd->voicedat_lng + pmd->effecdat_lng > 40) pmderr_3(pmd);
    }

    // :8587
    // TODO: this doesn't seem to be relevant without resident functionality

    // :8604
    // TODO: this doesn't seem to be relevant without resident functionality

    // :8642
    // TODO

    // :8663
    pmd->fmint_seg = 0;
    pmd->fmint_ofs = 0;
    pmd->efcint_seg = 0;
    pmd->efcint_ofs = 0;
    pmd->intfook_flag = 0;
    pmd->skip_flag = 0;
    pmd->effon = 0;
    pmd->fm_effec_flag = 0;
    pmd->pcmflag = 0;
    pmd->psgefcnum = -1;
    pmd->fm_effec_num = -1;
    pmd->pcm_effec_num = -1;

    // :8681
    if (pmd->board != 0) {
        // :8687
        int_init(pmd);

        // :8692
        // TODO: not sure what's going on here
        opnset44(pmd, 0x29, 0x00);
        opnset44(pmd, 0x24, 0x00);
        opnset44(pmd, 0x25, 0x00);
        opnset44(pmd, 0x26, 0x00);
        opnset44(pmd, 0x27, 0x3f);

        // :8756
        // TODO: system function to install interrupt handler
    }

    // :8776
    // TODO: probably don't need pmdvector installation logic here

    // :8793
    // TODO
}

// :8845
static void fm_check(struct pmd *pmd) {
    port_check(pmd);
    // TODO: port_check should be able to fail and return no board
    // :8854
    pmd->fm_port1 = pmd->fm1_port1;
    pmd->fm_port2 = pmd->fm1_port2;
    // :8858
    // TODO: store port_num to print it later?
    // :8866
    wait_check(pmd);
    // :8873
#if board2 * adpcm
    opnset44(pmd, 0x29, 0x83);
# if ademu
    pmd->pcm_gs_flag = 1;
# else
    adpcm_ram_check(pmd);
# endif
#else
    pmd->pcm_gs_flag = 1;
#endif
    pmd->board = 1;
}

// :8899
static void int_init(struct pmd *pmd) {
    // :8093
    // TODO: the interrupt check logic is probably not relevant for us, but consider at least printing the board name and info

    // :8973
    if (pmd->ppsdrv_flag == 0xff) {
        // :8983
        if (ppsdrv_check(pmd) == 0) {
            pmd->ppsdrv_flag = 1;
            if (pmd->kp_rhythm_flag == 0xff) pmd->kp_rhythm_flag = 0;
        } else {
            pmd->ppsdrv_flag = 0;
            if (pmd->kp_rhythm_flag == 0xff) pmd->kp_rhythm_flag = 1;
        }
    } else {
        // :8976
        if (pmd->kp_rhythm_flag == 0xff) {
            pmd->kp_rhythm_flag = pmd->ppsdrv_flag ^ 1;
        }
    }
    // :8998
    if (pmd->message_flag != 0 && pmd->ppsdrv_flag == 1) {
        print_mes(pmd, mes_ppsdrv);
    }

    // :9005
    if (ppz8_check(pmd) == 0) {
        // :9008
        // TODO: support PPZ8
    }

    // :9033
    // TODO: this logic is probably irrelevant for us
}

// :9083
static int ppsdrv_check(struct pmd *pmd) {
    // TODO: support PPS
    (void)pmd;
    return 1;
}

// :9110
static int ppz8_check(struct pmd *pmd) {
    // TODO: support PPZ8
    (void)pmd;
    return 1;
}

// :9136
static void opnint_start(struct pmd *pmd) {
    if (pmd->board == 0) return;
    // :9139
    memset(pmd->parts, 0, max_part1 * sizeof(pmd->parts[0]));
    // :9145
    pmd->rhythmmask = 255;
    pmd->rhydmy = -1;
    // :9148
    data_init(pmd);
    opn_init(pmd);
    opnset44(pmd, 0x07, 0xbf);
    // TODO
}

// :9196
static void port_check(struct pmd *pmd) {
    // TODO: the supported board(s) may depend on the driver in use
    struct pmd_board board = pmd->sys->get_board(pmd->user_data);
    pmd->ongen = board.ym2608 ? 1 : 0;
    pmd->fm1_port1 = board.fm1_port1;
    pmd->fm1_port2 = board.fm1_port2;
    pmd->fm2_port1 = board.fm2_port1;
    pmd->fm2_port2 = board.fm2_port2;
}

// :9927
static void set_option(struct pmd *pmd, struct option *options) {
    for (;;) {
        char c = *pmd->si++;
        if (c == '/' || c == '-') {
            if (option_main(pmd, options) != 0) return;
        } else if (is_cntrl(c)) {
            return;
        } else if (c == ' ') {
            continue;
        } else {
            usage(pmd);
            return;
        }
    }
}

// :9939
static int option_main(struct pmd *pmd, struct option *options) {
    char c = to_upper(*pmd->si++);
    for (; options->c != 0; options++) {
        if (c == options->c) return options->impl(pmd);
    }
    usage(pmd);
    return 1;
}

// :10397
static char *search_env(struct pmd *pmd, const char *name) {
    return pmd->sys->getenv(name, pmd->user_data);
}

// :10424
PMDC_NORETURN static void pmderr_1(struct pmd *pmd) {
    pmderr_main(pmd, pmderror_mes1);
}

// :10428
PMDC_NORETURN static void pmderr_2(struct pmd *pmd) {
    pmderr_main(pmd, pmderror_mes2);
}

// :10432
PMDC_NORETURN static void pmderr_3(struct pmd *pmd) {
    pmderr_main(pmd, pmderror_mes3);
}

// :10436
PMDC_NORETURN static void pmderr_4(struct pmd *pmd) {
    pmderr_main(pmd, pmderror_mes4);
}

// :10440
PMDC_NORETURN static void pmderr_5(struct pmd *pmd) {
    pmderr_main(pmd, pmderror_mes5);
}

// :10444
PMDC_NORETURN static void pmderr_6(struct pmd *pmd) {
    pmderr_main(pmd, pmderror_mes6);
}

// :10448
PMDC_NORETURN static void pmderr_7(struct pmd *pmd) {
    pmderr_main(pmd, pmderror_mes7);
}

// :10452
PMDC_NORETURN static void pmderr_main(struct pmd *pmd, const char *mes) {
    pmd->sys->print(mes, pmd->user_data);
    error_exit(pmd, 1);
}

// :10467
static void usage(struct pmd *pmd) {
    if (pmd->opt_sp_push != 0) {
        // :10472
        print_mes(pmd, mes_warning);
        print_mes(pmd, mes_kankyo);
        pmd->opt_sp_push = 0;
    } else {
        // :10478
        print_mes(pmd, mes_usage);
        error_exit(pmd, 255);
    }
}

// :10513
static void wait_check(struct pmd *pmd) {
    struct pmd_timing timing = pmd->sys->get_timing(pmd->user_data);
    pmd->wait_clock = timing.data_write_delay_loops;
    pmd->wait1_clock = timing.loop_ns;
}

// ADRAMCHK.ASM:11
static void adpcm_ram_check(struct pmd *pmd) {
    pmd->pcm_gs_flag = !pmd->sys->get_adpcm_supported(pmd->user_data);
}
