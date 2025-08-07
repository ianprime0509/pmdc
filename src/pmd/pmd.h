#ifndef PMDC_PMD_H
#define PMDC_PMD_H

#include <stdbool.h>
#include <stdint.h>

// The standard C noreturn attribute is not a property of function types and
// can't be used on function pointers, so we can't use it here.
#ifdef __GNUC__
#define PMDC_NORETURN __attribute__((noreturn))
#else
#define PMD
#endif

struct pmd;

struct pmd_board {
    // If true, the FM chip is a YM2608. If false, it is a YM2203.
    bool ym2608;
    // Low address port (SSG, rhythm, FM common data, FM channels 1-3).
    uint16_t fm1_port1;
    // Low data port.
    uint16_t fm1_port2;
    // High address port (ADPCM, FM channels 4-6).
    uint16_t fm2_port1;
    // High data port.
    uint16_t fm2_port2;
};

struct pmd_timing {
    // Number of loops to wait after a write to an FM address register before
    // writing the corresponding data.
    uint16_t data_write_delay_loops;
    // Nanoseconds taken by a single loop instruction.
    uint16_t loop_ns;
};

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
    // Replaces kankyo_seg (:8000).
    char *(*getenv)(const char *name, void *user_data);

    // x86 `out` instruction with byte operand.
    void (*outb)(uint16_t port, uint8_t data, void *user_data);
    // Waits for a given number of loops (`loop $` instructions).
    // See struct pmd_timing.
    void (*wait)(int loops, void *user_data);

    // Replaces port discovery logic in port_check (:9196).
    struct pmd_board (*get_board)(void *user_data);
    // Replaces timing logic in wait_check (:10513).
    struct pmd_timing (*get_timing)(void *user_data);
    // Replaces ADPCM availability check (ADRAMCHK.ASM:11).
    bool (*get_adpcm_supported)(void *user_data);
};

// :8155
struct pmd_qq {
    uint16_t address;
    uint16_t partloop;
    uint8_t leng;
    uint8_t qdat;
    uint16_t fnum;
    uint16_t detune;
    uint16_t lfodat;
    uint16_t porta_num;
    uint16_t porta_num2;
    uint16_t porta_num3;
    uint8_t volume;
    uint8_t shift;
    uint8_t delay;
    uint8_t speed;
    uint8_t step;
    uint8_t time;
    uint8_t delay2;
    uint8_t speed2;
    uint8_t step2;
    uint8_t time2;
    uint8_t lfoswi;
    uint8_t volpush;
    uint8_t mdepth;
    uint8_t mdspd;
    uint8_t mdspd2;
    uint8_t envf;
    uint8_t eenv_count;
    uint8_t eenv_ar;
    uint8_t eenv_dr;
    uint8_t eenv_sr;
    uint8_t eenv_rr;
    uint8_t eenv_sl;
    uint8_t eenv_al;
    uint8_t eenv_arc;
    uint8_t eenv_drc;
    uint8_t eenv_src;
    uint8_t eenv_rrc;
    uint8_t eenv_volume;
    uint8_t extendmode;
    uint8_t fmpan;
    uint8_t psgpat;
    uint8_t voicenum;
    uint8_t loopcheck;
    uint8_t carrier;
    uint8_t slot1;
    uint8_t slot3;
    uint8_t slot2;
    uint8_t slot4;
    uint8_t slotmask;
    uint8_t neiromask;
    uint8_t lfo_wave;
    uint8_t partmask;
    uint8_t keyoff_flag;
    uint8_t volmask;
    uint8_t qdata;
    uint8_t qdatb;
    uint8_t hldelay;
    uint8_t hldelay_c;
    uint16_t _lfodat;
    uint8_t _delay;
    uint8_t _speed;
    uint8_t _step;
    uint8_t _time;
    uint8_t _delay2;
    uint8_t _speed2;
    uint8_t _step2;
    uint8_t _time2;
    uint8_t _mdepth;
    uint8_t _mdspd;
    uint8_t _mdspd2;
    uint8_t _lfo_wave;
    uint8_t _volmask;
    uint8_t mdc;
    uint8_t mdc2;
    uint8_t _mdc;
    uint8_t _mdc2;
    uint8_t onkai;
    uint8_t sdelay;
    uint8_t sdelay_c;
    uint8_t sdelay_m;
    uint8_t alg_fb;
    uint8_t keyon_flag;
    uint8_t qdat2;
    uint16_t fnum2;
    uint8_t onkai_def;
    uint8_t shift_def;
    uint8_t qdat3;
};

struct pmd {
    // :7975
    uint16_t fm_port1;
    uint16_t fm_port2;
    uint16_t ds_push;
    uint16_t dx_push;
    uint8_t ah_push;
    uint8_t al_push;
    uint8_t partb;
    uint8_t tieflag;
    uint8_t volpush_flag;
    uint8_t rhydmy;
    uint8_t fmsel;
    uint8_t omote_keys[3];
    uint8_t ura_key1;
    uint8_t ura_key2;
    uint8_t ura_key3;
    uint8_t loop_work;
    uint8_t ppsdrv_flag;
    uint16_t prgdat_adr2;
    uint16_t pcmrepeat1;
    uint16_t pcmrepeat2;
    uint16_t pcmrelease;
    uint8_t lastTimerAtime;
    uint8_t music_flag;
    uint8_t slotdetune_flag;
    uint8_t slot3_flag;
    uint16_t eoi_adr;
    uint8_t eoi_data;
    uint16_t mask_adr;
    uint8_t mask_data;
    uint8_t mask_data2;
    uint16_t ss_push;
    uint16_t sp_push;
    uint8_t fm3_alg_fb;
    uint8_t af_check;
    uint8_t ongen;
    uint8_t lfo_switch;

    // :8030
    uint16_t mmlbuf;
    uint16_t tondat;
    uint16_t efcdat;
    uint16_t fm1_port1;
    uint16_t fm1_port2;
    uint16_t fm2_port1;
    uint16_t fm2_port2;
    uint16_t fmint_ofs;
    uint16_t fmint_seg;
    uint16_t efcint_ofs;
    uint16_t efcint_seg;
    uint16_t prgdat_adr;
    uint16_t radtbl;
    uint16_t rhyadr;
    uint16_t rhythmmask;
    uint8_t board;
    uint8_t key_check;
    uint8_t fm_voldown;
    uint8_t ssg_voldown;
    uint8_t pcm_voldown;
    uint8_t rhythm_voldown;
    uint8_t prg_flg;
    uint8_t x68_flg;
    uint8_t status;
    uint8_t status2;
    uint8_t tempo_d;
    uint8_t fadeout_speed;
    uint8_t fadeout_volume;
    uint8_t tempo_d_push;
    uint8_t syousetu_lng;
    uint8_t opncount;
    uint8_t TimerAtime;
    uint8_t effflag;
    uint8_t psnoi;
    uint8_t psnoi_last;
    uint8_t fm_effec_num;
    uint8_t fm_effec_flag;
    uint8_t disint;
    uint8_t pcmflag;
    uint16_t pcmstart;
    uint16_t pcmstop;
    uint8_t pcm_effec_num;
    uint16_t _pcmstart;
    uint16_t _pcmstop;
    uint16_t _voice_delta_n;
    uint8_t _pcmpan;
    uint8_t _pcm_volume;
    uint8_t rshot_dat;
    uint8_t rdat[6];
    uint8_t rhyvol;
    uint16_t kshot_dat;
    uint16_t ssgefcdat;
    uint16_t ssgefclen;
    uint8_t play_flag;
    uint8_t pause_flag;
    uint8_t fade_stop_flag;
    uint8_t kp_rhythm_flag;
    uint8_t TimerBflag;
    uint8_t TimerAflag;
    uint8_t int60flag;
    uint8_t int60_result;
    uint8_t pcm_gs_flag;
    uint8_t esc_sp_key;
    uint8_t grph_sp_key;
    uint8_t rescut_cant;
    uint16_t slot_detune1;
    uint16_t slot_detune2;
    uint16_t slot_detune3;
    uint16_t slot_detune4;
    // Number of loop instructions needed to wait after an FM address port write
    // before writing the data.
    uint16_t wait_clock;
    // Nanoseconds taken by a single loop instruction.
    uint16_t wait1_clock;
    uint8_t ff_tempo;
    uint8_t pcm_access;
    uint8_t TimerB_speed;
    uint8_t fadeout_flag;
    uint8_t adpcm_wait;
    uint8_t revpan;
    uint8_t pcm86_vol;
    uint16_t syousetu;
    uint8_t int5_flag;
    uint8_t port22h;
    uint8_t tempo_48;
    uint8_t tempo_48_push;
    uint8_t rew_sp_key;
    uint8_t intfook_flag;
    uint8_t skip_flag;
    uint8_t _fm_voldown;
    uint8_t _ssg_voldown;
    uint8_t _pcm_voldown;
    uint8_t _rhythm_voldown;
    uint8_t _pcm86_vol;
    uint8_t mstart_flag;
    uint8_t mus_filename[13];
    uint8_t mmldat_lng;
    uint8_t voicedat_lng;
    uint8_t effecdat_lng;
    uint8_t rshot_bd;
    uint8_t rshot_sd;
    uint8_t rshot_sym;
    uint8_t rshot_hh;
    uint8_t rshot_tom;
    uint8_t rshot_rim;
    uint8_t rdump_bd;
    uint8_t rdump_sd;
    uint8_t rdump_sym;
    uint8_t rdump_hh;
    uint8_t rdump_tom;
    uint8_t rdump_rim;
    uint8_t ch3mode;
    uint8_t ch3mode_push;
    uint8_t ppz_voldown;
    uint8_t _ppz_voldown;
    uint16_t ppz_call_ofs;
    uint16_t ppc_cal_seg;
    uint8_t p86_freq;
    uint16_t p86_freqtable;
    uint8_t adpcm_emulate;

    // :8318
    struct pmd_qq parts[14 + 8 + 1];

    // :10851
    // epson_flag is used only during port_check (:9196) and not needed in this
    // version.
    uint8_t port_sel;
    uint8_t opn_0eh;

    // :10857
    uint8_t message_flag;
    uint16_t opt_sp_push;
    uint16_t resident_size;

    // EFCDRV.ASM:251
    uint16_t effadr;
    uint16_t eswthz;
    uint16_t eswtst;
    uint8_t effcnt;
    uint8_t eswnhz;
    uint8_t eswnst;
    uint8_t eswnct;
    uint8_t effon;
    uint8_t psgefcnum;
    uint8_t hosei_flag;
    uint8_t last_shot_data;

    char *si;

    void *user_data;
    const struct pmd_sys *sys;
};

#endif
