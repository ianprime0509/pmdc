#ifndef PMDC_PMD_H
#define PMDC_PMD_H

#include <stdbool.h>
#include <stddef.h>
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

// A PPSDRV (INT 64h) sound effect request. AH=01h takes the effect number in
// AL with bit 7 set (EFCDRV.ASM:78).
struct pmd_pps_keyon {
    // AL & 7Fh: effect number.
    uint8_t num;
    // BH: low byte of the detune applied to the effect.
    uint8_t detune;
    // BL: volume (the driver passes the inverted 4-bit volume).
    uint8_t volume;
};

// The vtable for PPSDRV resident functionality (INT 64h).
//
// Like pmd_sys, every operation takes the pmd user_data as its last argument.
// PPSDRV is an external resident program; this project does not implement it.
// A NULL pmd->pps means PPSDRV is unavailable, which corresponds to the
// original's [ppsdrv_flag] == 0 after its resident check.
struct pmd_drv_pps {
    // INT 64h, AH=00h: key off/dump the current PPSDRV sound (EFCDRV.ASM:17,
    // :99, :191).
    void (*keyoff)(void *user_data);
    // INT 64h, AH=01h (EFCDRV.ASM:78).
    void (*keyon)(struct pmd_pps_keyon keyon, void *user_data);
    // INT 64h, AH=05h, AL=value >> 1, DL=value & 1 (PMD.ASM:2486).
    void (*pdr_switch)(uint8_t value, void *user_data);
};

// A PPZ8 voice table entry, returned for INT 7Fh AH=04h, AL=0Dh/0Eh
// (PPZDRV.ASM:395-401). The original computes an entry address by adding the
// 20h-byte PZI header and `voice * 12h` bytes; get_voice returns the entry
// itself, so the translation indexes an array of this type with ordinary
// pointer arithmetic instead of the original's byte counts. PMD never reads
// the two bytes that follow `rate`, so the C struct does not preserve the
// original 18-byte size.
struct pmd_ppz8_voice {
    // +00: sample data start offset.
    uint32_t start;
    // +04: sample data size.
    uint32_t size;
    // +08: loop start offset.
    uint32_t loop_start;
    // +0c: loop end offset.
    uint32_t loop_end;
    // +10: source sample rate.
    uint16_t rate;
};

// The vtable for PPZ8 resident functionality (INT 7Fh).
//
// Like pmd_sys, every operation takes the pmd user_data as its last argument.
// PPZ8 is an external resident program; this project does not implement it.
// A NULL pmd->ppz8 means PPZ8 is unavailable, which corresponds to the
// original's [ppz_call_seg] == 0.
struct pmd_drv_ppz8 {
    // INT 7Fh, AH=00h: initialize PPZ8 (PMD.ASM:482-484).
    void (*init)(void *user_data);
    // INT 7Fh, AH=06h: stop all channels (PMD.ASM:485-486).
    void (*stop_all)(void *user_data);
    // INT 7Fh, AH=12h: stop the PPZ8 FIFO interrupt (PMD.ASM:6146, :10177).
    void (*fifo_interrupt_stop)(void *user_data);
    // INT 7Fh, AH=01h, AL=channel, DX=voice number (PPZDRV.ASM:708-713,
    // PCMDRVE.ASM:640-643). Bit 15 of DX selects the PCM86/ADPCM bank.
    void (*keyon)(uint8_t channel, uint16_t voice, void *user_data);
    // INT 7Fh, AH=02h, AL=channel (PPZDRV.ASM:337, :691, PMD.ASM:7095).
    void (*keyoff)(uint8_t channel, void *user_data);
    // INT 7Fh, AH=07h, AL=channel, DX=volume (PPZDRV.ASM:686-688,
    // PCMDRVE.ASM:621-624).
    void (*set_volume)(uint8_t channel, uint8_t volume, void *user_data);
    // INT 7Fh, AH=0Bh, AL=channel, DX=tone value (PPZDRV.ASM:792-795,
    // PCMDRVE.ASM:699-705).
    void (*set_tone)(uint8_t channel, uint16_t tone, void *user_data);
    // INT 7Fh, AH=0Eh, AL=channel, CX:DX=loop start, SI:DI=loop end
    // (PPZDRV.ASM:371-373, :551-555, PCMDRVE.ASM:349-350).
    void (*set_loop)(uint8_t channel, uint32_t start, uint32_t end, void *user_data);
    // INT 7Fh, AH=13h, AL=channel, DX=pan (PPZDRV.ASM:483-487).
    void (*set_pan)(uint8_t channel, uint8_t pan, void *user_data);
    // INT 7Fh, AH=15h, AL=channel, DX=source sample rate (PPZDRV.ASM:558-561).
    void (*set_source_rate)(uint8_t channel, uint16_t rate, void *user_data);
    // INT 7Fh, AH=18h, AL=0 disables and AL=1 enables ADPCM emulation
    // (PMD.ASM:461, PPZDRV.ASM:534-536, PCMDRVE.ASM:489-491).
    void (*set_adpcm_emulation)(bool enable, void *user_data);
    // INT 7Fh, AH=19h: AL=0 permits and AL=1 forbids resident removal
    // (PMD.ASM:481-482, :9802, :10189).
    void (*set_removal_protection)(bool protect, void *user_data);
    // INT 7Fh, AH=04h, AL=10h: AL = the PPZ8 interrupt level
    // (PMD.ASM:9009-9010, :10205-10206).
    uint8_t (*get_irq_level)(void *user_data);
    // INT 7Fh, AH=04h, AL=0Dh/0Eh, DL=voice number (PPZDRV.ASM:395-401).
    // Returns the requested entry of the voice table, i.e. the ES:BX base
    // skipped past the 20h-byte PZI header and advanced by `voice` entries.
    // Bit 7 of the voice number selects the 128-255 bank. NULL if the bank or
    // voice is not loaded.
    const struct pmd_ppz8_voice *(*get_voice)(uint8_t voice, void *user_data);
    // The PPZ8 FM interrupt entry address, obtained through AH=04h, AL=09h
    // (PMD.ASM:9016-9021, :10212-10216), called directly while a PPZ8-owned
    // IRQ is handled (PMD.ASM:253-255). NULL models PPZ8 with no directly
    // callable entry point, in which case PMD skips the call (the original's
    // `_ppz` macro does nothing while ppz_call_seg is 1) and relies on PPZ8's
    // own interrupt handling.
    void (*fm_int)(void *user_data);
};

// The vtable for P86DRV resident functionality (INT 65h).
//
// Like pmd_sys, every operation takes the pmd user_data as its last argument.
// P86DRV is an external resident program; this project does not implement it.
// A NULL pmd->p86 means P86DRV is unavailable (PCMDRV86.ASM:919-928).
struct pmd_drv_p86 {
    // INT 65h, AH=FFh: AL = version, where 10h or greater supports data
    // offsets (PCMDRV86.ASM:930-935).
    uint8_t (*get_version)(void *user_data);
    // INT 65h, AH=FBh: push the current EMS mapping (PCMDRV86.ASM:949-950).
    void (*push_ems)(void *user_data);
    // INT 65h, AH=FCh: pop the current EMS mapping (PCMDRV86.ASM:958-959).
    void (*pop_ems)(void *user_data);
    // INT 65h, AH=FDh, DX:AX = data offset, returns DS:DX = data address
    // (PCMDRV86.ASM:1147-1148, :1167-1168). NULL if the offset is invalid.
    const uint8_t *(*get_data_offset)(uint32_t offset, void *user_data);
};

// :8155
// The per-part work area. `address` and `partloop` point into `mmlbuf` (or
// another buffer in the same `dataarea` image) and are NULL when the original
// address is null.
struct pmd_qq {
    // :8156 Playing data address.
    uint8_t *address;
    // :8157 Loop destination.
    uint8_t *partloop;
    // :8158 Remaining length.
    uint8_t leng;
    // :8159 Gate time (the computed q/Q value).
    uint8_t qdat;
    // :8160 Block/fnum.
    uint16_t fnum;
    // :8161 Detune.
    int16_t detune;
    // :8162 LFO data.
    int16_t lfodat;
    // :8163 Portamento step, remaining step, and remainder.
    int16_t porta_num;
    int16_t porta_num2;
    int16_t porta_num3;
    // :8166 Volume.
    uint8_t volume;
    // :8167 Key shift.
    int8_t shift;
    // :8168 LFO [DELAY], [SPEED], [STEP], [TIME].
    uint8_t delay;
    uint8_t speed;
    uint8_t step;
    uint8_t time;
    // :8172 LFO [DELAY_2], [SPEED_2], [STEP_2], [TIME_2].
    uint8_t delay2;
    uint8_t speed2;
    uint8_t step2;
    uint8_t time2;
    // :8176 LFOSW. B0/tone B1/vol B2/sync B3/porta B4/tone B5/vol B6/sync.
    uint8_t lfoswi;
    // :8178 Volume push area.
    uint8_t volpush;
    // :8179 M depth, M speed, M speed 2.
    uint8_t mdepth;
    uint8_t mdspd;
    uint8_t mdspd2;
    // :8182 PSG envelope start flag; -1 for the extended envelope.
    uint8_t envf;
    // :8183 Extended PSG envelope state.
    uint8_t eenv_count;
    union {
        struct {
            // :8184 Extended PSG envelope AR/DR/SR/RR/SL/AL.
            uint8_t eenv_ar;
            uint8_t eenv_dr;
            uint8_t eenv_sr;
            uint8_t eenv_rr;
            uint8_t eenv_sl;
            uint8_t eenv_al;
            // :8190 Envelope counters.
            uint8_t eenv_arc;
            uint8_t eenv_drc;
            uint8_t eenv_src;
            uint8_t eenv_rrc;
            // :8194 Envelope volume (0-15).
            uint8_t eenv_volume;
        };
        // :8248 `qqq` aliases for the old envelope fields, overlaid on the
        // extended envelope block above.
        struct {
            uint8_t pat;
            uint8_t pv2;
            uint8_t pr1;
            uint8_t pr2;
            uint8_t qqq_pad1;
            uint8_t qqq_pad2;
            uint8_t patb;
            uint8_t qqq_pad3;
            uint8_t pr1b;
            uint8_t pr2b;
            uint8_t penv;
        };
    };
    // :8195 Extended mode. B1/detune B2/LFO B3/normal or extended envelope.
    uint8_t extendmode;
    // :8196 FM panning + AMD + PMD.
    uint8_t fmpan;
    // :8197 PSG pattern (tone/noise/mix).
    uint8_t psgpat;
    // :8198 Voice number.
    uint8_t voicenum;
    // :8199 1 if the part looped, 3 if it ended.
    uint8_t loopcheck;
    // :8200 FM carrier, slot TLs, slot mask, voice mask.
    uint8_t carrier;
    uint8_t slot1;
    uint8_t slot3;
    uint8_t slot2;
    uint8_t slot4;
    uint8_t slotmask;
    uint8_t neiromask;
    // :8207 LFO waveform.
    uint8_t lfo_wave;
    // :8208 Part mask. b0/normal b1/effect b2/NEC PCM b3/none b4/PPZ or ADE
    // b5/s0 b6/m b7/temporary.
    uint8_t partmask;
    // :8210 Keyoff flag.
    uint8_t keyoff_flag;
    // :8211 Volume LFO mask.
    uint8_t volmask;
    // :8212 q, Q, hard LFO delay, and hard LFO delay counter.
    uint8_t qdata;
    uint8_t qdatb;
    uint8_t hldelay;
    uint8_t hldelay_c;
    // :8216 Secondary LFO data and parameters.
    int16_t _lfodat;
    uint8_t _delay;
    uint8_t _speed;
    uint8_t _step;
    uint8_t _time;
    uint8_t _delay2;
    uint8_t _speed2;
    uint8_t _step2;
    uint8_t _time2;
    // :8225 M depth, M speed, and M speed 2 (secondary).
    uint8_t _mdepth;
    uint8_t _mdspd;
    uint8_t _mdspd2;
    // :8228 Secondary LFO waveform and volume LFO mask.
    uint8_t _lfo_wave;
    uint8_t _volmask;
    // :8230 M depth counters.
    uint8_t mdc;
    uint8_t mdc2;
    uint8_t _mdc;
    uint8_t _mdc2;
    // :8234 Playing note (0xff for a rest).
    uint8_t onkai;
    // :8235 Slot delay, counter, and mask.
    uint8_t sdelay;
    uint8_t sdelay_c;
    uint8_t sdelay_m;
    // :8238 Voice alg/fb.
    uint8_t alg_fb;
    // :8239 Incremented when a new note or rest is processed.
    uint8_t keyon_flag;
    // :8240 Minimum q value.
    uint8_t qdat2;
    // :8241 PPZ8/PMD86 fnum high bits.
    uint16_t fnum2;
    // :8242 Playing note before the key shift (0x?f for a rest).
    uint8_t onkai_def;
    // :8243 Master key shift.
    int8_t shift_def;
    // :8244 q random value.
    uint8_t qdat3;
};

// :8263
// The largest variant's part count (max_part1 in the PPZ8 build). struct pmd
// has a fixed layout for every variant; variants with fewer parts leave their
// trailing work areas unused.
enum {
    PMD_MAX_PARTS = 14 + 8,
};

// A YM2608 ADPCM voice table entry (PCMDRV.ASM:473-479, :850): 16-bit start
// and stop offsets relative to the ADPCM data area.
struct pmd_pcm_adpcm_voice {
    uint16_t start;
    uint16_t stop;
};

// A PCM86 voice table entry (PCMDRV86.ASM:540-556): a 24-bit data offset and
// size within the PCM86 EMS data area.
struct pmd_pcm86_voice {
    uint32_t offset;
    uint32_t size;
};

// :8353
// The variant's PCM voice table. The two tables are mutually exclusive: a
// variant has at most one of them, and none at all without PCM. struct pmd has
// a fixed layout for every variant, so the table is a union and only the
// active member is meaningful; pmd_get_pcm_adr returns NULL for variants with
// no table.
union pmd_pcm_table {
    // The YM2608 ADPCM voice table (board2 and adpcm, without ademu).
    struct {
        // The first ADPCM start offset, 26h in the original data image.
        uint16_t pcmends;
        struct pmd_pcm_adpcm_voice pcmadrs[256];
        // :8359 The loaded ADPCM file name.
        char filename[128];
    } adpcm_data;
    // The PCM86 voice table (board2 and pcm).
    struct {
        // The EMS data area containing the loaded PCM data (the original's
        // pcmst_ofs/pcmst_seg).
        const uint8_t *base;
        struct pmd_pcm86_voice pcmadrs[256];
    } pcm86;
};

// :8038
struct pmd_int_handler {
    void (*fn)(void *);
    void *user_data;
};

// :6366
// The multi-register results of the INT 60h functions. The comments give the
// original register each field corresponds to.
struct pmd_status {
    uint8_t status;       // AH
    uint8_t status2;      // AL
};
struct pmd_drv_info {
    uint8_t board;        // AL; 255 when no board is present
    uint8_t version;      // AH and DH (`vers`)
    uint8_t version_char; // DL (`verc`)
};
struct pmd_psgefcnum {
    uint8_t psgefcnum;    // AH
    uint8_t effon;        // AL
};
struct pmd_joy {
    uint8_t joystick1;    // AL
    uint8_t joystick2;    // AH
};
struct pmd_size {
    uint8_t music;        // AL
    uint8_t voice;        // AH
    uint8_t effect;       // DL
};

// The value the INT 60h no-board jump table returns in AL (n_int60_jumptable's
// get_255). Functions whose AL result is meaningful only with a board return
// this value when pmd->board is 0.
enum {
    PMD_NO_BOARD = 0xff,
};

// :6452
// drv_chk
enum {
    PMD_DRV_BOARD_PMD = 0,
    PMD_DRV_BOARD_B2 = 1,
    PMD_DRV_BOARD_86 = 2,
    PMD_DRV_BOARD_PPZ = 4,
    PMD_DRV_BOARD_PPZE = 5,
    PMD_DRV_BOARD_ABSENT = PMD_NO_BOARD,
};

// ff_music
enum {
    PMD_FF_MUSIC_OK = 0,
    PMD_FF_MUSIC_PAST = 1,
    PMD_FF_MUSIC_STOPPED = 2,
    // When no board is present the original leaves AL untouched, so the typed
    // interface reports an explicit sentinel instead.
    PMD_FF_MUSIC_NO_BOARD = PMD_NO_BOARD,
};

// The result of pmd_change_main.
enum pmd_change_result {
    // The status was applied and printed.
    PMD_CHANGE_OK,
    // /R removed the driver; pmd_resident_cut already ran.
    PMD_CHANGE_REMOVED,
};

// struct pmd contains the global state of the PMD process. The original
// assembly implementation simply used global variables, but this translation
// moves all global state into this struct to improve its flexibility of use.
//
// struct pmd is pinned: it holds pointers into itself (`mmlbuf` points into
// `dataarea`, and `part_data_table` points at `parts`/`part_e`), so it must
// not be moved or copied after `pmd_init`.
//
// The fields follow the original data area order (:7975 onwards) as closely as
// practical. The INT 60h register save area (ds_push/dx_push/ah_push/al_push),
// the PIC mask/EOI addresses (eoi_*/mask_*), the private interrupt stack
// (ss_push/sp_push), int60_result, and the PPZ8 call address (ppz_call_ofs/
// ppz_call_seg, replaced by the ppz8 vtable pointer) are omitted: the typed
// interface replaces the register dispatcher, reports rejections through typed
// returns instead of int60_result, and leaves interrupt masking to the host.
struct pmd {
    // :293
    // Interrupt level read from FM register 0Eh.
    uint8_t int_level;

    // :7975
    uint16_t fm_port1;
    uint16_t fm_port2;

    // :7981
    uint8_t partb;
    uint8_t tieflag;
    uint8_t volpush_flag;
    uint8_t rhydmy;
    uint8_t fmsel;
    uint8_t omote_key1;
    uint8_t omote_key2;
    uint8_t omote_key3;
    uint8_t ura_key1;
    uint8_t ura_key2;
    uint8_t ura_key3;
    uint8_t loop_work;
    uint8_t ppsdrv_flag;
    // :7994
    uint8_t *prgdat_adr2;
    uint16_t pcmrepeat1;
    uint16_t pcmrepeat2;
    uint16_t pcmrelease;
    uint8_t lastTimerAtime;
    uint8_t music_flag;
    uint8_t slotdetune_flag;
    uint8_t slot3_flag;
    uint8_t fm3_alg_fb;
    uint8_t af_check;
    // :8011
    // 0 = YM2203
    // 1 = YM2608
    uint8_t ongen;
    // :8012
    uint8_t lfo_switch;

    // :8030 open_work
    // :8031
    uint8_t *mmlbuf;
    uint8_t *tondat;
    uint8_t *efcdat;
    // :8034
    uint16_t fm1_port1;
    uint16_t fm1_port2;
    uint16_t fm2_port1;
    uint16_t fm2_port2;
    // :8038
    struct pmd_int_handler fmint;
    // :8040
    struct pmd_int_handler efcint;
    // :8042
    uint8_t *prgdat_adr;
    // :8043
    const uint8_t *radtbl;
    // :8044
    const uint8_t *rhyadr;
    // :8045
    uint8_t rhythmmask;
    // :8046
    uint8_t board;
    // :8047
    uint8_t key_check;
    // :8048
    uint8_t fm_voldown;
    uint8_t ssg_voldown;
    uint8_t pcm_voldown;
    uint8_t rhythm_voldown;
    // :8052
    uint8_t prg_flg;
    uint8_t x68_flg;
    uint8_t status;
    uint8_t status2;
    // :8056
    uint8_t tempo_d;
    uint8_t fadeout_speed;
    uint8_t fadeout_volume;
    uint8_t tempo_d_push;
    // :8060
    uint8_t syousetu_lng;
    uint8_t opncount;
    uint8_t TimerAtime;
    uint8_t effflag;
    uint8_t psnoi;
    uint8_t psnoi_last;
    // :8066
    uint8_t fm_effec_num;
    uint8_t fm_effec_flag;
    uint8_t disint;
    uint8_t pcmflag;
    // :8070
    uint16_t pcmstart;
    uint16_t pcmstop;
    uint8_t pcm_effec_num;
    uint16_t _pcmstart;
    uint16_t _pcmstop;
    uint16_t _voice_delta_n;
    uint8_t _pcmpan;
    uint8_t _pcm_volume;
    // :8078
    uint8_t rshot_dat;
    // :8079
    // Rhythm volume/pan data.
    uint8_t rdat[6];
    // :8080
    uint8_t rhyvol;
    // :8081
    uint16_t kshot_dat;
    // :8082
    const uint8_t *ssgefcdat;
    // :8083
    uint16_t ssgefclen;
    // :8084
    uint8_t play_flag;
    uint8_t pause_flag;
    uint8_t fade_stop_flag;
    uint8_t kp_rhythm_flag;
    // :8088
    uint8_t TimerBflag;
    uint8_t TimerAflag;
    uint8_t int60flag;
    // :8092
    uint8_t pcm_gs_flag;
    uint8_t esc_sp_key;
    uint8_t grph_sp_key;
    uint8_t rescut_cant;
    // :8096
    uint16_t slot_detune1;
    uint16_t slot_detune2;
    uint16_t slot_detune3;
    uint16_t slot_detune4;
    // :8100
    uint16_t wait_clock;
    uint16_t wait1_clock;
    uint8_t ff_tempo;
    uint8_t pcm_access;
    uint8_t TimerB_speed;
    uint8_t fadeout_flag;
    uint8_t adpcm_wait;
    uint8_t revpan;
    uint8_t pcm86_vol;
    // :8109
    uint16_t syousetu;
    uint8_t int5_flag;
    uint8_t port22h;
    uint8_t tempo_48;
    uint8_t tempo_48_push;
    uint8_t rew_sp_key;
    uint8_t intfook_flag;
    uint8_t skip_flag;
    // :8117
    uint8_t _fm_voldown;
    uint8_t _ssg_voldown;
    uint8_t _pcm_voldown;
    uint8_t _rhythm_voldown;
    uint8_t _pcm86_vol;
    uint8_t mstart_flag;
    // :8123
    char mus_filename[13];
    // :8124
    uint8_t mmldat_lng;
    uint8_t voicedat_lng;
    uint8_t effecdat_lng;
    // :8127
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
    // :8139
    uint8_t ch3mode;
    uint8_t ch3mode_push;
    uint8_t ppz_voldown;
    uint8_t _ppz_voldown;
    // :8145
    uint8_t p86_freq;
    // :8147
    const uint8_t *p86_freqtable;
    // :8151
    uint8_t adpcm_emulate;

    // :8318
    // The contiguous part work areas (part1 onwards).
    struct pmd_qq parts[PMD_MAX_PARTS];
    // :8350
    struct pmd_qq part_e;
    // :8283
    // The part work area pointers, with part_e last. Indices come from the part
    // numbers in `part_table`, so every variant only ever reaches the first
    // max_part1 + 1 entries; the fixed-size tail beyond them is unused and, as
    // in the original, left uninitialized.
    // The original stores open_work before this table; it is the base the
    // resident check measures `board` against, which the port does not need.
    struct pmd_qq *part_data_table[PMD_MAX_PARTS + 1];
    // :8353
    union pmd_pcm_table pcm_table;
    // :8377
    // The resident data area: dataarea[0] is the version/system byte and
    // mmlbuf is dataarea + 1. tondat and efcdat are computed from dataarea, not
    // from mmlbuf, so the version byte comes out of the module buffer and the
    // whole area spans the configured sizes summed (40 KiB at most).
    uint8_t dataarea[40 * 1024];

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

    // Non-resident area (:10852 onwards).
    // :10853
    // The selected FM port, -1 for auto.
    int8_t port_sel;
    // :10857
    uint8_t message_flag;

    // EFCDRV.ASM:252
    // PSG effect driver state.
    const uint8_t *effadr;
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

    // PCMDRV86.ASM:1387
    // PCM86 playback state.
    uint8_t play86_flag;
    uint8_t trans_flag;
    uint16_t start_ofs;
    uint16_t start_ofs2;
    uint16_t size1;
    uint16_t size2;
    uint16_t _start_ofs;
    uint16_t _start_ofs2;
    uint16_t _size1;
    uint16_t _size2;
    uint8_t addsize1;
    uint16_t addsize2;
    uint16_t addsizew;
    uint16_t repeat_ofs;
    uint16_t repeat_ofs2;
    uint16_t repeat_size1;
    uint16_t repeat_size2;
    uint16_t release_ofs;
    uint16_t release_ofs2;
    uint16_t release_size1;
    uint16_t release_size2;
    uint8_t repeat_flag;
    uint8_t release_flag1;
    uint8_t release_flag2;
    uint8_t pcm86_pan_flag;
    uint8_t pcm86_pan_dat;

    // TUNEDATA.INC:9
    // The PCM86 frequency table; p86_freqtable points here in PCM86 variants
    // so /HZ can rewrite it without touching shared storage. It is unused
    // elsewhere.
    uint8_t pcm_tune_data[288];

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

// Initializes the global state of the program. The caller must set sys, hw,
// pps, ppz8, p86, and user_data first; pmd_init preserves them. The struct pmd
// is pinned and must not be moved or copied after this call, because mmlbuf
// and part_data_table point into it.
void pmd_init(struct pmd *pmd);
// Initializes the program as resident: parses options and the command line,
// sets up the buffers, and, with a board present, the FM timer handler.
void pmd_resident_main(struct pmd *pmd, char *cmdline);
// Handles updated options for an already resident program.
enum pmd_change_result pmd_change_main(struct pmd *pmd, char *cmdline);
// Stops the driver: removes the FM timer handler and silences the board.
// Returns after the driver no longer touches the hardware; the host still owns
// and may discard the struct pmd.
void pmd_resident_cut(struct pmd *pmd);

// The INT 60h function table (:6366), one typed function per entry. Each
// comment gives the original input registers; the no-board behavior follows
// n_int60_jumptable (:7294), where entries that are not listed there use
// `nothing` and therefore do nothing at all without a board.
//
// Starts playback. AH=00h. Returns false when a timer is running and the
// request is deferred (AH=FFh). Without a board this does nothing and returns
// true.
bool pmd_mstart_f(struct pmd *pmd);
// Stops playback. AH=01h. Returns false when a timer is running (AH=FFh).
// Without a board this does nothing and returns true.
bool pmd_mstop_f(struct pmd *pmd);
// Starts a fade in or out by setting the fade speed. AH=02h, AL=speed. Without
// a board the speed is left unchanged.
void pmd_fout(struct pmd *pmd, uint8_t speed);
// Plays an SSG effect. AH=03h, AL=effect number. Without a board this does
// nothing.
void pmd_eff_on(struct pmd *pmd, uint8_t efcnum);
// Silences the SSG effect. AH=04h. Without a board this does nothing.
void pmd_effoff(struct pmd *pmd);
// Returns the current bar number. AH=05h, AX. Without a board the original
// only sets AL to FFh, so this returns PMD_NO_BOARD; that value is
// indistinguishable from bar 255, which only a board-present call returns.
uint16_t pmd_get_ss(struct pmd *pmd);
// Returns the module data area (the original DS:DX at AH=06h). This is the
// dataarea including the version byte at mmlbuf[-1]. Works without a board.
uint8_t *pmd_get_musdat_adr(struct pmd *pmd);
// Returns the voice data area. AH=07h. Works without a board.
uint8_t *pmd_get_tondat_adr(struct pmd *pmd);
// Returns the fadeout volume. AH=08h, AL. Without a board this returns
// PMD_NO_BOARD.
uint8_t pmd_get_fv(struct pmd *pmd);
// Returns the board type and version. AH=09h, AL/AH/DX. Without a board the
// reported board is PMD_DRV_BOARD_ABSENT, with the same version as usual.
struct pmd_drv_info pmd_drv_chk(struct pmd *pmd);
// Returns the status bytes. AH=0Ah, AH/AL. Without a board both bytes are
// PMD_NO_BOARD.
struct pmd_status pmd_get_status(struct pmd *pmd);
// Returns the FM effect data area. AH=0Bh. Works without a board.
uint8_t *pmd_get_efcdat_adr(struct pmd *pmd);
// Plays an FM effect. AH=0Ch, AL=effect number. Without a board this does
// nothing.
void pmd_fm_effect_on(struct pmd *pmd, uint8_t efcnum);
// Silences the FM effect. AH=0Dh. Without a board this does nothing.
void pmd_fm_effect_off(struct pmd *pmd);
// Returns the PCM voice table, or NULL in a variant without one. AH=0Eh. Works
// without a board.
union pmd_pcm_table *pmd_get_pcm_adr(struct pmd *pmd);
// Plays a PCM effect. AH=0Fh, AL=effect number, DX=delta N, CH=pan, CL=volume.
// Without a board this does nothing.
void pmd_pcm_effect(struct pmd *pmd, uint8_t efcnum, uint16_t deltan, uint8_t pan, uint8_t vol);
// Returns the fixed-size part work area pointer table; the entries after the
// variant's last part are NULL. AH=10h. Works without a board.
struct pmd_qq **pmd_get_workadr(struct pmd *pmd);
// Returns the playing FM effect number, or PMD_NO_BOARD with no board. AH=11h.
uint8_t pmd_get_fmefc_num(struct pmd *pmd);
// Returns the playing PCM effect number, or PMD_NO_BOARD with no board. AH=12h.
uint8_t pmd_get_pcmefc_num(struct pmd *pmd);
// Sets the FM interrupt callback; a null fn clears it. AH=13h, DS:DX. Without a
// board this does nothing, so the callback is not stored.
void pmd_set_fm_int(struct pmd *pmd, struct pmd_int_handler handler);
// Sets the effect interrupt callback; a null fn clears it. AH=14h, DS:DX.
// Without a board this does nothing, so the callback is not stored.
void pmd_set_efc_int(struct pmd *pmd, struct pmd_int_handler handler);
// Returns the playing SSG effect number and priority. AH=15h, AH/AL. Without a
// board both fields are PMD_NO_BOARD.
struct pmd_psgefcnum pmd_get_psgefcnum(struct pmd *pmd);
// Reads the joystick through PSG registers 0Eh/0Fh. AH=16h, AL/AH. Without a
// board both fields are PMD_NO_BOARD.
struct pmd_joy pmd_get_joy(struct pmd *pmd);
// Returns the PPSDRV flag. AH=17h, AL. Without a board this returns
// PMD_NO_BOARD.
uint8_t pmd_get_ppsdrv_flag(struct pmd *pmd);
// Sets the PPSDRV flag. AH=18h, AL. Without a board this does nothing.
void pmd_set_ppsdrv_flag(struct pmd *pmd, uint8_t flag);
// Sets the fadeout volume. AH=19h, AL. Without a board this does nothing, so
// the volume is left unchanged.
void pmd_set_fv(struct pmd *pmd, uint8_t vol);
// Pauses playback. AH=1Ah. Without a board this does nothing.
void pmd_pause_on(struct pmd *pmd);
// Resumes playback. AH=1Bh. Without a board this does nothing.
void pmd_pause_off(struct pmd *pmd);
// Fast-forwards to the given bar. AH=1Ch, DX=bar. Returns 0 on success, 1 when
// the music ended before the requested bar, 2 when the music is stopped, or
// PMD_FF_MUSIC_NO_BOARD with no board.
uint8_t pmd_ff_music(struct pmd *pmd, uint16_t bar);
// Returns the address of the given memo string. AH=1Dh, AL=memo number. NULL
// when the module has no memo or the number is out of range. Works without a
// board.
uint8_t *pmd_get_memo(struct pmd *pmd, uint8_t n);
// Masks or unmasks a part. AH=1Eh, AL=part number; +80h unmasks. Without a
// board this does nothing.
void pmd_part_mask(struct pmd *pmd, uint8_t part);
// Returns the FM interrupt callback. AH=1Fh. Works without a board.
struct pmd_int_handler pmd_get_fm_int(struct pmd *pmd);
// Returns the effect interrupt callback. AH=20h. Works without a board.
struct pmd_int_handler pmd_get_efc_int(struct pmd *pmd);
// Returns the playing module file name. AH=21h. Works without a board.
char *pmd_get_mus_name(struct pmd *pmd);
// Returns the configured buffer sizes in KiB. AH=22h, AL/AH/DL. Works without
// a board.
struct pmd_size pmd_get_size(struct pmd *pmd);
// The AH=FFh MIDI sync entry point (:7340). Only sync builds define it.
void pmd_opnint_sub(struct pmd *pmd);

#endif
