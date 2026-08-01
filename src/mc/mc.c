#include "mc.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// :13
#ifndef hyouka
#define hyouka 0
#endif
#ifndef efc
#define efc 0
#endif

// :38
#define olddat 0
#define split 0
#define tempo_old_flag 0

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
static bool is_graph(char c) { return !is_cntrl(c) && c != ' '; }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_upper(char c) { return c >= 'A' && c <= 'Z'; }
static bool is_lower(char c) { return c >= 'a' && c <= 'z'; }
static char to_upper(char c) { return is_lower(c) ? c - 'a' + 'A' : c; }

// Data read/write helpers

static uint16_t read16(uint8_t *ptr) {
    return ptr[0] + (ptr[1] << 8);
}
static void write16(uint8_t *ptr, uint16_t val) {
    ptr[0] = val;
    ptr[1] = val >> 8;
}
static void write32(uint8_t *ptr, uint32_t val) {
    ptr[0] = val;
    ptr[1] = val >> 8;
    ptr[2] = val >> 16;
    ptr[3] = val >> 24;
}

static uint16_t lodsw(struct mc *mc) {
    uint16_t val = read16(mc->sib);
    mc->sib += 2;
    return val;
}
static void stosw(struct mc *mc, uint16_t val) {
    write16(mc->di, val);
    mc->di += 2;
}
static void stosd(struct mc *mc, uint32_t val) {
    write32(mc->di, val);
    mc->di += 4;
}

// Data offset helpers
//
// Assembly doesn't have the same rigid struct layout that C does. In the
// original assembly code, there are a few places where a specific location
// _within_ a larger field/structure has been given a label, sometimes even
// conditionally. To mimic this in C, the following functions return pointers
// to these "internal labels" within the global data.

static char *ou00(struct mc *mc) { return &mc->comtbl[11].cmd; }
static char *od00(struct mc *mc) { return &mc->comtbl[12].cmd; }

static uint8_t *m_start(struct mc *mc) { return mc->m_buf; }
#if efc || olddat
static uint8_t *m_buf(struct mc *mc) { return mc->m_buf; }
#else
static uint8_t *m_buf(struct mc *mc) { return mc->m_buf + 1; }
#endif
static uint8_t *mbuf_end(struct mc *mc) { return mc->m_buf + sizeof(mc->m_buf) - 1; }

static char *part_chr(struct mc *mc) { return mc->part_mes + 5; }

// :8009
static const char *err_table[36] = {
    err00,
    err01,
    err02,
    err03,
    err04,
    err05,
    err06,
    err07,
    err08,
    err09,
    err10,
    err11,
    err12,
    err13,
    err14,
    err15,
    err16,
    err17,
    err18,
    err19,
    err20,
    err21,
    err22,
    err23,
    err24,
    err25,
    err26,
    err27,
    err28,
    err29,
    err30,
    err31,
    err32,
    err33,
    err34,
    err35,
};

// :8062
static const uint16_t fnumdat_seg[] = {
    0x026a, 0x026b, 0x026c, 0x026e, 0x026f, 0x0270, 0x0271, 0x0272,
    0x0273, 0x0274, 0x0276, 0x0277, 0x0278, 0x0279, 0x027a, 0x027b,
    0x027c, 0x027e, 0x027f, 0x0280, 0x0281, 0x0282, 0x0283, 0x0284,
    0x0286, 0x0287, 0x0288, 0x0289, 0x028a, 0x028b, 0x028d, 0x028e,

    0x028f, 0x0290, 0x0291, 0x0293, 0x0294, 0x0295, 0x0296, 0x0297,
    0x0299, 0x029a, 0x029b, 0x029c, 0x029d, 0x029f, 0x02a0, 0x02a1,
    0x02a2, 0x02a3, 0x02a5, 0x02a6, 0x02a7, 0x02a8, 0x02aa, 0x02ab,
    0x02ac, 0x02ad, 0x02ae, 0x02b0, 0x02b1, 0x02b2, 0x02b3, 0x02b5,

    0x02b6, 0x02b7, 0x02b8, 0x02ba, 0x02bb, 0x02bc, 0x02bd, 0x02bf,
    0x02c0, 0x02c1, 0x02c3, 0x02c4, 0x02c5, 0x02c6, 0x02c8, 0x02c9,
    0x02ca, 0x02cc, 0x02cd, 0x02ce, 0x02cf, 0x02d1, 0x02d2, 0x02d3,
    0x02d5, 0x02d6, 0x02d7, 0x02d9, 0x02da, 0x02db, 0x02dd, 0x02de,

    0x02df, 0x02e1, 0x02e2, 0x02e3, 0x02e5, 0x02e6, 0x02e7, 0x02e9,
    0x02ea, 0x02eb, 0x02ed, 0x02ee, 0x02ef, 0x02f1, 0x02f2, 0x02f3,
    0x02f5, 0x02f6, 0x02f8, 0x02f9, 0x02fa, 0x02fc, 0x02fd, 0x02fe,
    0x0300, 0x0301, 0x0303, 0x0304, 0x0305, 0x0307, 0x0308, 0x030a,

    0x030b, 0x030c, 0x030e, 0x030f, 0x0311, 0x0312, 0x0313, 0x0315,
    0x0316, 0x0318, 0x0319, 0x031b, 0x031c, 0x031d, 0x031f, 0x0320,
    0x0322, 0x0323, 0x0325, 0x0326, 0x0328, 0x0329, 0x032a, 0x032c,
    0x032d, 0x032f, 0x0330, 0x0332, 0x0333, 0x0335, 0x0336, 0x0338,

    0x0339, 0x033b, 0x033c, 0x033e, 0x033f, 0x0341, 0x0342, 0x0344,
    0x0345, 0x0347, 0x0348, 0x034a, 0x034b, 0x034d, 0x034f, 0x0350,
    0x0352, 0x0353, 0x0355, 0x0356, 0x0358, 0x0359, 0x035b, 0x035c,
    0x035e, 0x035f, 0x0361, 0x0363, 0x0364, 0x0366, 0x0367, 0x0369,

    0x036a, 0x036c, 0x036d, 0x036f, 0x0371, 0x0372, 0x0374, 0x0375,
    0x0377, 0x0379, 0x037a, 0x037c, 0x037d, 0x037f, 0x0381, 0x0382,
    0x0384, 0x0386, 0x0387, 0x0389, 0x038a, 0x038c, 0x038e, 0x038f,
    0x0391, 0x0393, 0x0394, 0x0396, 0x0398, 0x0399, 0x039b, 0x039d,

    0x039e, 0x03a0, 0x03a2, 0x03a3, 0x03a5, 0x03a7, 0x03a8, 0x03aa,
    0x03ac, 0x03ad, 0x03af, 0x03b1, 0x03b3, 0x03b4, 0x03b6, 0x03b8,
    0x03b9, 0x03bb, 0x03bd, 0x03bf, 0x03c0, 0x03c2, 0x03c4, 0x03c6,
    0x03c7, 0x03c9, 0x03cb, 0x03cd, 0x03ce, 0x03d0, 0x03d2, 0x03d4,

    0x03d5, 0x03d7, 0x03d9, 0x03db, 0x03dc, 0x03de, 0x03e0, 0x03e2,
    0x03e4, 0x03e5, 0x03e7, 0x03e9, 0x03eb, 0x03ed, 0x03ef, 0x03f0,
    0x03f2, 0x03f4, 0x03f6, 0x03f8, 0x03f9, 0x03fb, 0x03fd, 0x03ff,
    0x0401, 0x0403, 0x0405, 0x0406, 0x0408, 0x040a, 0x040c, 0x040e,

    0x0410, 0x0412, 0x0414, 0x0415, 0x0417, 0x0419, 0x041b, 0x041d,
    0x041f, 0x0421, 0x0423, 0x0425, 0x0427, 0x0428, 0x042a, 0x042c,
    0x042e, 0x0430, 0x0432, 0x0434, 0x0436, 0x0438, 0x043a, 0x043c,
    0x043e, 0x0440, 0x0442, 0x0444, 0x0446, 0x0448, 0x044a, 0x044c,

    0x044e, 0x0450, 0x0452, 0x0454, 0x0456, 0x0458, 0x045a, 0x045c,
    0x045e, 0x0460, 0x0462, 0x0464, 0x0466, 0x0468, 0x046a, 0x046c,
    0x046e, 0x0470, 0x0472, 0x0474, 0x0476, 0x0478, 0x047a, 0x047c,
    0x047e, 0x0480, 0x0483, 0x0485, 0x0487, 0x0489, 0x048b, 0x048d,

    0x048f, 0x0491, 0x0493, 0x0495, 0x0498, 0x049a, 0x049c, 0x049e,
    0x04a0, 0x04a2, 0x04a4, 0x04a6, 0x04a9, 0x04ab, 0x04ad, 0x04af,
    0x04b1, 0x04b3, 0x04b6, 0x04b8, 0x04ba, 0x04bc, 0x04be, 0x04c1,
    0x04c3, 0x04c5, 0x04c7, 0x04c9, 0x04cc, 0x04ce, 0x04d0, 0x04d2,
};

// :8274
static const uint8_t fmvol[] = {
    127 - 0x2a,
    127 - 0x28,
    127 - 0x25,
    127 - 0x22,
    127 - 0x20,
    127 - 0x1d,
    127 - 0x1a,
    127 - 0x18,
    127 - 0x15,
    127 - 0x12,
    127 - 0x10,
    127 - 0x0d,
    127 - 0x0a,
    127 - 0x08,
    127 - 0x05,
    127 - 0x02,
    127 - 0x00,
};

// :8292
#if !efc

// :8294
enum {
    pcm_vol_ext = 0,
};

// :8297
static const uint8_t psgenvdat[][4] = {
    {0,  0,  0, 0},
    {2, -1,  0, 1},
    {2, -2,  0, 1},
    {2, -2,  0, 8},
    {2, -1, 24, 1},
    {2, -2, 24, 1},
    {2, -2,  4, 1},
    {2,  1,  0, 1},
    {1,  2,  0, 1},
    {1,  2, 24, 1},
};
enum {
    psgenvdat_max = 9,
};

// :8309
enum {
    max_part = 11,
};
// :8310
enum ongen {
    // FM channel.
    fm = 0,
    // Unused.
    fm2 = 1,
    // SSG channel.
    psg = 2,
    // PCM channel (part J) or rhythm (part K).
    pcm = 3,
    // PPZ/PCM extend channel (§2.25).
    pcm_ex = 4,
};

#else

// :8318
enum {
    max_part = 126,
};

#endif

// :8322
enum {
    pcmpart = 10,
    rhythm2 = 11,
    rhythm = 18,
};

// :8394
static const uint8_t oplprg_table[] = {
    8,  1,0,
    8,  7,1,

    4, 15,4,
    4, 15,0,
    6, 15,0,
    6, 15,4,
    2, 63,0,
    2,  3,6,
    0, 15,0,
    0,  1,4,
    0,  1,5,
    0,  1,6,
    0,  1,7,

    5, 15,4,
    5, 15,0,
    7, 15,0,
    7, 15,4,
    3, 63,0,
    3,  3,6,
    1, 15,0,
    1,  1,4,
    1,  1,5,
    1,  1,6,
    1,  1,7,
};

// LC.INC:481
static const uint32_t num_data[] = {
    1000000000,
    100000000,
    10000000,
    1000000,
    100000,
    10000,
    1000,
    100,
    10,
};

// :50
PMDC_NORETURN static void msdos_exit(struct mc *mc);
// :55
PMDC_NORETURN static void error_exit(struct mc *mc, int status);
// :60
static void print_mes(struct mc *mc, const char *mes);
// :66
static void print_chr(struct mc *mc, char c);
// :72
static void print_line(struct mc *mc, const char *line);
// :533
static void p1cloop(struct mc *mc);
// :610
static void cmloop(struct mc *mc);
// :659
static void cmloop2(struct mc *mc);
// :881
static void check_lopcnt(struct mc *mc);
#if !efc
// :946
static void fm3_check(struct mc *mc);
// :961
static void fm3c_main(struct mc *mc, char part);
// :977
static void pcm_check(struct mc *mc);
// :991
static void pcmc_main(struct mc *mc, char part);
// :1007
static void rt(struct mc *mc);
#endif
// :1118
static void cm_init(struct mc *mc);
// :1219
static void nd_s_opl_loop(struct mc *mc);
// :1251
static void nd_s_loop(struct mc *mc);
// :1296
static void memow_loop0(struct mc *mc, char **adr);
#if !hyouka
// :1417
static void write_ff(struct mc *mc);
// :1448
static void write_disk(struct mc *mc);
#endif
// :1562
static void line_skip(struct mc *mc);
// :1803
static void read_fffile(struct mc *mc);
// :1909
static int get_option(struct mc *mc, bool is_mml, bool is_env);
// :2074
static void macro_set(struct mc *mc);
// :2118
PMDC_NORETURN static void ps_error(struct mc *mc);
// :2132
static void pcmfile_set(struct mc *mc, char c2, char *macro_start);
// :2149
static void ppcfile_set(struct mc *mc);
// :2156
static void pcmvolume_set(struct mc *mc);
// :2172
static void pcmextend_set(struct mc *mc, char *macro_start);
// :2224
static void ppsfile_set(struct mc *mc, char *macro_start);
// :2239
static void title_set(struct mc *mc, char c2, char *macro_start);
// :2255
static void composer_set(struct mc *mc);
// :2262
static void arranger_set(struct mc *mc, char c2);
// :2272
static void adpcm_set(struct mc *mc);
// :2292
static void memo_set(struct mc *mc);
// :2304
static void transpose_set(struct mc *mc);
// :2312
static void detune_select(struct mc *mc);
// :2328
static void LFOExtend_set(struct mc *mc, char c2);
// :2346
static void loopdef_set(struct mc *mc);
// :2355
static void EnvExtend_set(struct mc *mc);
// :2371
static void VolDown_set(struct mc *mc);
// :2458
static void JumpFlag_set(struct mc *mc);
// :2468
static void tempo_set(struct mc *mc);
// :2483
static void tempo_set2(struct mc *mc);
// :2493
static void zenlen_set(struct mc *mc);
// :2508
static void FM3Extend_set(struct mc *mc, char c2);
// :2544
static void file_name_set(struct mc *mc);
// :2569
static void fffile_set(struct mc *mc);
// :2580
static void dt2flag_set(struct mc *mc, char c2);
// :2604
static void octrev_set(struct mc *mc, char c2);
// :2624
static void option_set(struct mc *mc);
// :2631
static void bend_set(struct mc *mc);
// :2641
static void include_set(struct mc *mc);
// :2804
static int partcheck(char c);
// :2821
static char *set_strings(struct mc *mc, char *out);
// :2838
static char *set_strings2(struct mc *mc, char *out);
// :2865
static int move_next_param(struct mc *mc);
// :2894
static void hsset(struct mc *mc);
// :2986
static void new_neiro_set(struct mc *mc);
// :3070
static void nns_pname_set(struct mc *mc, uint8_t *dst);
// :3090
static void opl_nns(struct mc *mc);
// :3154
static void slot_trans(uint8_t *dst, uint8_t *src);
// :3168
static void slot_get(struct mc *mc, uint8_t *slot);
// :3231
static uint8_t get_param(struct mc *mc);
// :3308
static bool one_line_compile(struct mc *mc);
// :3331
static void olc0(struct mc *mc);
// :3333
static void olc02(struct mc *mc);
// :3336
static bool olc03(struct mc *mc);
// :3391
static bool olc_skip2(struct mc *mc);
// :3419
static bool skip_mml(struct mc *mc);
// :3451
static bool part_not_found(struct mc *mc);
// :3462
static bool part_found(struct mc *mc);
// :3662
static void adp_set(struct mc *mc, char cmd);
// :3673
static void tl_set(struct mc *mc, char cmd);
// :3704
static void partmask_set(struct mc *mc, char cmd);
// :3718
static void slotmask_set(struct mc *mc, char cmd);
// :3746
static void slotdetune_set(struct mc *mc);
// :3770
static void slotkeyondelay_set(struct mc *mc);
// :3794
static void ssg_efct_set(struct mc *mc, char cmd);
// :3806
static void fm_efct_set(struct mc *mc, char cmd);
// :3818
static void fade_set(struct mc *mc, char cmd);
// :3830
static void fb_set(struct mc *mc);
// :3858
static void porta_start(struct mc *mc, char cmd);
// :3880
static void porta_end(struct mc *mc, char cmd);
// :3961
static void bunsan_end(struct mc *mc);
// :4052
static void bunsan_main(struct mc *mc);
// :4109
static void bunsan_last(struct mc *mc);
// :4134
static void bunsan_exit(struct mc *mc);
// :4149
static void bunsan_set1loop(struct mc *mc);
// :4179
PMDC_NORETURN static void bunsan_error(struct mc *mc);
// :4210
static void status_write(struct mc *mc, char cmd);
// :4228
static void giji_echo_set(struct mc *mc, char cmd);
// :4273
static void sousyoku_onp_set(struct mc *mc, char cmd);
// :4322
static void ssgeg_set(struct mc *mc);
// :4387
static void sss_set1slot(struct mc *mc, uint8_t ssgeg_reg, uint8_t num);
// :4398
static void syousetu_lng_set(struct mc *mc, char cmd);
// :4409
static void hardlfo_set(struct mc *mc, char cmd);
// :4460
static void hardlfo_onoff(struct mc *mc, char cmd);
// :4504
static void hdelay_set(struct mc *mc);
// :4506
static void hdelay_set2(struct mc *mc, char cmd);
// :4512
static void opm_hf_set(struct mc *mc);
// :4516
static void opm_wf_set(struct mc *mc);
// :4520
static void opm_pmd_set(struct mc *mc);
// :4524
static void opm_amd_set(struct mc *mc);
// :4528
static void opm_all_set(struct mc *mc);
// :4545
static void hf_set(struct mc *mc);
// :4553
static void wf_set(struct mc *mc);
// :4563
static void pmd_set(struct mc *mc);
// :4572
static void amd_set(struct mc *mc);
// :4585
static void octrev(struct mc *mc, char cmd);
// :4593
static void lngmul(struct mc *mc, char cmd);
// :4624
static void lm_over(struct mc *mc, uint8_t *len_new);
// :4650
static void lngrew(struct mc *mc, char cmd);
// :4681
static void lngrew_2(struct mc *mc, char cmd);
// :4688
static void lng_dec(struct mc *mc, char cmd);
// :4711
static void lng_skip_ret(struct mc *mc);
// :4722
static void otoc(struct mc *mc, char cmd);
// :4725
static void otod(struct mc *mc, char cmd);
// :4728
static void otoe(struct mc *mc, char cmd);
// :4731
static void otof(struct mc *mc, char cmd);
// :4734
static void otog(struct mc *mc, char cmd);
// :4737
static void otoa(struct mc *mc, char cmd);
// :4740
static void otob(struct mc *mc, char cmd);
// :4743
static void otox(struct mc *mc, char cmd);
// :4745
static void otor(struct mc *mc, char cmd);
// :4748
static void otoset(struct mc *mc, char cmd, uint8_t note, int8_t shift);
// :4842
static void ots002(struct mc *mc, uint8_t onkai);
// :4871
static uint16_t fmpt(struct mc *mc, uint8_t onkai);
// :4935
static void porta_pitchset(struct mc *mc, uint16_t detune);
// :4961
static void otoset_x(struct mc *mc, uint8_t note);
// :4968
static void bp8(struct mc *mc, uint8_t onkai);
// :4991
static void bp9(struct mc *mc);
// :5037
static void ge_set(struct mc *mc);
// :5096
static void ge_set_vol(struct mc *mc);
// :5169
static uint8_t ongen_sel_vol(struct mc *mc, uint8_t vol);
// :5200
static void ss_set(struct mc *mc);
// :5309
static uint8_t one_down(struct mc *mc, uint8_t onkai);
// :5329
static uint8_t one_up(struct mc *mc, uint8_t onkai);
// :5348
static void press(struct mc *mc, uint8_t len);
// :5386
static void restprs(struct mc *mc, uint8_t len);
// :5394
static void prs3(struct mc *mc, uint8_t len);
// :5409
static int lngset(struct mc *mc, uint16_t *ret);
// :5420
static int lngset2(struct mc *mc, uint16_t *ret);
// :5473
static int hexget8(struct mc *mc, uint8_t *ret);
// :5495
static int hexcal8(char c, uint8_t *ret);
// :5520
static int futen(struct mc *mc, uint8_t *ret);
// :5567
static void futen_skip(struct mc *mc);
// :5580
static int numget(struct mc *mc, uint8_t *ret);
// :5594
static void octset(struct mc *mc, char cmd);
// :5604
static void octs0(struct mc *mc, char cmd, uint8_t octave);
// :5620
static void octup(struct mc *mc, char cmd);
// :5624
static void octdown(struct mc *mc, char cmd);
// :5635
static void lengthset(struct mc *mc, char cmd);
// :5662
static void zenlenset(struct mc *mc, char cmd);
// :5678
static uint8_t lngcal(struct mc *mc);
// :5698
static void parset(struct mc *mc, uint8_t dh, uint8_t dl);
// :5707
static void tempoa(struct mc *mc, char cmd);
// :5740
static void tempob(struct mc *mc, char cmd);
// :5753
static void tset(struct mc *mc, uint8_t timerb);
// :5761
static void tempo_ss(struct mc *mc, uint8_t subcmd);
// :5784
static uint8_t timerb_get(uint8_t tempo);
// :5802
static uint8_t get_clock(struct mc *mc, char cmd);
// :5824
static void qset(struct mc *mc, char cmd);
// :5856
static void qsetb(struct mc *mc);
// :5866
static void qset2(struct mc *mc, char cmd);
// :5899
static void vseta(struct mc *mc, char cmd);
// :5932
static void vset(struct mc *mc, uint8_t vol);
// :5956
static void vsetb(struct mc *mc, char cmd);
// :5973
static void vsetm(struct mc *mc, uint8_t vol);
// :5988
static void vsetm1(struct mc *mc, uint8_t vol);
// :6007
static void vss(struct mc *mc);
// :6025
static void vss2(struct mc *mc);
// :6030
static void vss2m(struct mc *mc);
// :6039
static void neirochg(struct mc *mc, char cmd);
#if !efc
// :6088
static void rhyprg(struct mc *mc, uint16_t prg);
// :6095
static void psgprg(struct mc *mc, uint8_t prg);
#endif
// :6114
static void repeat_check(struct mc *mc, uint8_t prg);
// :6153
static void set_prg(struct mc *mc, uint8_t prg);
// :6171
static void tieset(struct mc *mc, char cmd);
// :6175
static void tieset_2(struct mc *mc, char cmd);
// :6229
static void tie_norm(struct mc *mc);
// :6243
static void tie_skip(struct mc *mc);
// :6251
static void sular(struct mc *mc);
// :6279
static void detset(struct mc *mc, char cmd);
// :6303
static void detset_exit(struct mc *mc, uint16_t detune);
// :6316
static void detset_2(struct mc *mc);
// :6329
static void mstdet_set(struct mc *mc);
// :6338
static void extdet_set(struct mc *mc);
// :6349
static void vd_fm(struct mc *mc);
// :6351
static void vd_ssg(struct mc *mc);
// :6353
static void vd_pcm(struct mc *mc);
// :6355
static void vd_rhythm(struct mc *mc);
// :6358
static void vd_ppz(struct mc *mc);
// :6360
static void vd_main(struct mc *mc, uint8_t subcmd);
// :6380
static uint16_t getnum(struct mc *mc);
// :6414
static void stloop(struct mc *mc, char cmd);
// :6447
static void edloop(struct mc *mc, char cmd);
// :6454
static void edl00(struct mc *mc, uint16_t loops);
// :6459
static void edl00b(struct mc *mc, uint8_t loops);
// :6531
static void extloop(struct mc *mc, char cmd);
// :6558
static void lopset(struct mc *mc, char cmd);
// :6576
static void oshift(struct mc *mc, char cmd);
// :6598
static void def_onkai_set(struct mc *mc);
// :6592
static void master_trans_set(struct mc *mc);
// :6636
static void volup(struct mc *mc, char cmd);
// :6691
static void voldown(struct mc *mc, char cmd);
// :6746
static void lfoset(struct mc *mc, char cmd);
// :6820
static void extlfo_set(struct mc *mc);
// :6841
static void depthset(struct mc *mc);
// :6905
static void portaset(struct mc *mc);
// :6950
static void waveset(struct mc *mc);
// :6969
static void lfomask_set(struct mc *mc);
// :6988
static void lfoswitch(struct mc *mc, char cmd);
// :7047
static void psgenvset(struct mc *mc, char cmd);
// :7098
static void extenv_set(struct mc *mc);
// :7110
static void ycommand(struct mc *mc, char cmd);
// :7130
static void psgnoise(struct mc *mc, char cmd);
// :7155
static void psgpat(struct mc *mc, char cmd);
// :7188
static void bendset(struct mc *mc, char cmd);
// :7205
static void pitchset(struct mc *mc, char cmd);
// :7213
static void panset(struct mc *mc, char cmd);
// :7246
static void rhycom(struct mc *mc, char cmd);
// :7284
static enum mc_rcom_ret mstvol(struct mc *mc);
// :7309
static enum mc_rcom_ret rthvol(struct mc *mc);
// :7360
static uint8_t rhysel(struct mc *mc);
// :7347
static enum mc_rcom_ret panlef(struct mc *mc);
// :7350
static enum mc_rcom_ret panmid(struct mc *mc);
// :7353
static enum mc_rcom_ret panrig(struct mc *mc);
// :7355
static enum mc_rcom_ret rpanset(struct mc *mc, uint8_t pan);
// :7388
static enum mc_rcom_ret bdset(struct mc *mc);
// :7391
static enum mc_rcom_ret snrset(struct mc *mc);
// :7394
static enum mc_rcom_ret cymset(struct mc *mc);
// :7397
static enum mc_rcom_ret hihset(struct mc *mc);
// :7400
static enum mc_rcom_ret tamset(struct mc *mc);
// :7403
static enum mc_rcom_ret rimset(struct mc *mc);
// :7405
static enum mc_rcom_ret rs00(struct mc *mc, uint8_t n);
// :7446
static void hscom(struct mc *mc, char cmd);
// :7523
static int search_hs3(struct mc *mc, struct mc_hs3 **found, struct mc_hs3 **prefix_found);
// :7592
PMDC_NORETURN static void error(struct mc *mc, char cmd, uint8_t n);
// :7748
static void put_part(struct mc *mc);
// :7787
static void calc_line(struct mc *mc);
// :7864
static char *search_env(struct mc *mc, const char *name);
// :7892
static void print_8(struct mc *mc, uint8_t n);
// :7903
static void p8_oneset(struct mc *mc, uint8_t *n, uint8_t div, bool *seen_digits);
// :7930
static void print_16(struct mc *mc, uint16_t n);
// :7946
static void p16_oneset(struct mc *mc, uint16_t *n, uint16_t div, bool *seen_digits);
// :7973
static void usage(struct mc *mc);
// :7985
static void space_cut(struct mc *mc);
// LC.INC:5
static void _print_mes(struct mc *mc, const char *mes);
// LC.INC:13
static void lc(struct mc *mc, uint8_t lc_flag);
// LC.INC:48
static void part_loop2(struct mc *mc);
// LC.INC:173
static void kcom_loop(struct mc *mc, uint8_t *r_table);
// LC.INC:213
static void rcom_loop(struct mc *mc);
// LC.INC:247
static void command_exec(struct mc *mc, uint8_t cmd);
// LC.INC:256
static void jump16(struct mc *mc);
static void jump6 (struct mc *mc);
static void jump5 (struct mc *mc);
static void jump4 (struct mc *mc);
static void jump3 (struct mc *mc);
static void jump2 (struct mc *mc);
static void jump1 (struct mc *mc);
static void jump0 (struct mc *mc);
// LC.INC:268
static void _tempo(struct mc *mc);
// LC.INC:279
static void porta(struct mc *mc);
// LC.INC:291
static void loop_set(struct mc *mc);
// LC.INC:301
static void loop_start(struct mc *mc);
// LC.INC:311
static void loop_end(struct mc *mc);
// LC.INC:336
static void loop_exit(struct mc *mc);
// LC.INC:354
static void special_0c0h(struct mc *mc);
// LC.INC:371
static void print_length(struct mc *mc);
// LC.INC:432
static void print_32(struct mc *mc, uint32_t n);
// LC.INC:454
static void sub_pr(struct mc *mc, uint32_t *n, uint32_t div, bool *seen_digits);
// DISKPMD.INC:61
static int diskwrite(struct mc *mc, const char *filename, void *data, uint16_t n);
// DISKPMD.INC:112
static void *opnhnd(struct mc *mc, const char *filename, char oh_filename[static 128]);
// DISKPMD.INC:250
static bool sjis_check(char c);
// DISKPMD.INC:270
static int redhnd(struct mc *mc, void *file, void *dest, uint16_t n, uint16_t *read);
// DISKPMD.INC:280
static int clohnd(struct mc *mc, void *file);
// DISKPMD.INC:291
static void *makhnd(struct mc *mc, const char *filename);
// DISKPMD.INC:303
static int wrihnd(struct mc *mc, void *file, void *data, uint16_t n);

// :3493
static const struct mc_com default_comtbl[] = {
    {'c', otoc},
    {'d', otod},
    {'e', otoe},
    {'f', otof},
    {'g', otog},
    {'a', otoa},
    {'b', otob},
    {'r', otor},
    {'x', otox},
    {'l', lengthset},
    {'o', octset},
    {'>', octup},
    {'<', octdown},
    {'C', zenlenset},
    {'t', tempoa},
    {'T', tempob},
    {'q', qset},
    {'Q', qset2},
    {'v', vseta},
    {'V', vsetb},
    {'R', neirochg},
    {'@', neirochg},
    {'&', tieset},
    {'D', detset},
    {'[', stloop},
    {']', edloop},
    {':', extloop},
    {'L', lopset},
    {'_', oshift},
    {')', volup},
    {'(', voldown},
    {'M', lfoset},
    {'*', lfoswitch},
    {'E', psgenvset},
    {'y', ycommand},
    {'w', psgnoise},
    {'P', psgpat},
    {'!', hscom},
    {'B', bendset},
    {'I', pitchset},
    {'p', panset},
    {'\\', rhycom},
    {'X', octrev},
    {'^', lngmul},
    {'=', lngrew},
    {'H', hardlfo_set},
    {'#', hardlfo_onoff},
    {'Z', syousetu_lng_set},
    {'S', sousyoku_onp_set},
    {'W', giji_echo_set},
    {'~', status_write},
    {'{', porta_start},
    {'}', porta_end},
    {'n', ssg_efct_set},
    {'N', fm_efct_set},
    {'F', fade_set},
    {'s', slotmask_set},
    {'m', partmask_set},
    {'O', tl_set},
    {'A', adp_set},
    {'0', lngrew_2},
    {'1', lngrew_2},
    {'2', lngrew_2},
    {'3', lngrew_2},
    {'4', lngrew_2},
    {'5', lngrew_2},
    {'6', lngrew_2},
    {'7', lngrew_2},
    {'8', lngrew_2},
    {'9', lngrew_2},
    {'%', lngrew_2},
    {'$', lngrew_2},
    {'.', lngrew_2},
    {'-', lng_dec},
    {'+', tieset_2},
    {0},
};

// :7261
static const struct mc_rcom default_rcomtbl[] = {
    {'V', mstvol},
    {'v', rthvol},
    {'l', panlef},
    {'m', panmid},
    {'r', panrig},
    {'b', bdset},
    {'s', snrset},
    {'c', cymset},
    {'h', hihset},
    {'t', tamset},
    {'i', rimset},
    {0},
};

void mc_init(struct mc *mc) {
#define lengthof(arr) (sizeof((arr)) / sizeof((arr)[0]))

    // :3493
    static_assert(lengthof(mc->comtbl) == lengthof(default_comtbl), "comtbl length does not match");
    memcpy(mc->comtbl, default_comtbl, sizeof(default_comtbl));
    // :7261
    static_assert(lengthof(mc->rcomtbl) == lengthof(default_rcomtbl), "rcomtbl length does not match");
    memcpy(mc->rcomtbl, default_rcomtbl, sizeof(default_rcomtbl));

    // :8217
    mc->tempo = 0;
    mc->timerb = 0;
    mc->octave = 4;
    mc->leng = 0;
    mc->zenlen = 96;
    mc->deflng = 24;
    mc->deflng_k = 24;
    mc->calflg = 0;
    mc->hsflag = 0;
    mc->lopcnt = 0;
    mc->volss = 0;
    mc->volss2 = 0;
    mc->octss = 0;
    mc->nowvol = 0;
    mc->line = 0;
    mc->linehead = NULL;
    mc->length_check1 = 0;
    mc->length_check2 = 0;
    mc->allloop_flag = 0;
    mc->qcommand = 0;
    mc->acc_adr = NULL;
    mc->jump_flag = 0;

    // :8242
    mc->def_a = 0;
    mc->def_b = 0;
    mc->def_c = 0;
    mc->def_d = 0;
    mc->def_e = 0;
    mc->def_f = 0;
    mc->def_g = 0;

    // :8250
    mc->master_detune = 0;
    mc->detune = 0;
    mc->alldet = 0;
    mc->bend = 0;
    mc->pitch = 0;

    // :8256
    mc->bend1 = 0;
    mc->bend2 = 0;
    mc->bend3 = 0;

    // :8260
    mc->transpose = 0;

    // :8262
    mc->fm_voldown = 0;
    mc->ssg_voldown = 0;
    mc->pcm_voldown = 0;
    mc->rhythm_voldown = 0;
    mc->ppz_voldown = 0;

    // :8268
    mc->fm_voldown_flag = 0;
    mc->ssg_voldown_flag = 0;
    mc->pcm_voldown_flag = 0;
    mc->rhythm_voldown_flag = 0;
    mc->ppz_voldown_flag = 0;

    // :8294
    mc->pcm_vol_ext = 0;

    // :8326
    mc->part = 0;
    mc->ongen = 0;
    mc->pass = 0;

    // :8330
    mc->maxprg = 0;
    mc->kpart_maxprg = 0;
    mc->lastprg = 0;

    // :8334
    mc->prsok = 0;

    // :8341
    mc->prg_flg = 0;
    mc->ff_flg = 0;
    mc->x68_flg = 0;
    mc->towns_flg = 0;
    mc->dt2_flg = 0;
    mc->opl_flg = 0;
    mc->play_flg = 0;
    mc->save_flg = 0;
    mc->pmd_flg = 0;
    mc->ext_detune = 0;
    mc->ext_lfo = 0;
    mc->ext_env = 0;
    mc->memo_flg = 0;
    mc->pcm_flg = 0;
    mc->lc_flag = 0;
    mc->loop_def = 0;

    // :8358
    mc->adpcm_flag = 0xff;

    // :8362
    mc->ss_speed = 0;
    mc->ss_depth = 0;
    mc->ss_length = 0;
    mc->ss_tie = 0;

    // :8367
    mc->ge_delay = 0;
    mc->ge_depth = 0;
    mc->ge_depth2 = 0;
    mc->ge_tie = 0;
    mc->ge_flag1 = 0;
    mc->ge_flag2 = 0;
    mc->ge_dep_flag = 0;

    // :8375
    mc->skip_flag = 0;
    mc->tie_flag = 0;
    mc->porta_flag = 0;

    // :8379
    memset(mc->fm3_partchr, 0, sizeof(mc->fm3_partchr));
    mc->fm3_ofsadr = NULL;
    memset(mc->pcm_partchr, 0, sizeof(mc->pcm_partchr));
    mc->pcm_ofsadr = NULL;

    // The rest of the global variables in MC.ASM are left uninitialized.

    // LC.INC:583
    static_assert(lengthof(mc->part_mes) == lengthof(default_part_mes), "default_part_mes length does not match");
    memcpy(mc->part_mes, default_part_mes, sizeof(mc->part_mes));

    // LC.INC:590
    mc->print_flag = 0;
    mc->all_length = 0;
    mc->loop_length = 0;
    mc->max_all = 0;
    mc->max_loop = 0;
    memset(mc->fm3_adr, 0, sizeof(mc->fm3_adr));
    memset(mc->pcm_adr, 0, sizeof(mc->pcm_adr));
    mc->loop_flag = 0;

#undef lengthof
}

// :50
PMDC_NORETURN static void msdos_exit(struct mc *mc) {
    mc->sys->exit(0, mc->user_data);
}

// :55
PMDC_NORETURN static void error_exit(struct mc *mc, int status) {
    mc->sys->exit(status, mc->user_data);
}

// :60
static void print_mes(struct mc *mc, const char *mes) {
    mc->sys->print(mes, mc->user_data);
}

// :66
static void print_chr(struct mc *mc, char c) {
    mc->sys->putc(c, mc->user_data);
}

// :72
static void print_line(struct mc *mc, const char *line) {
    // Since our print accepts a null-terminated string rather than a
    // $-terminated string as in DOS, we don't need to replicate the original
    // logic of this macro (which was to print the string character by
    // character until the NUL terminator).
    mc->sys->print(line, mc->user_data);
}

// :132
void mc_main(struct mc *mc, char *cmdline) {
    // :144
    print_mes(mc, titmes);

    // :151
    // Note: in DOS, the command line is passed in the "Program Segment Prefix"
    // (PSP): https://en.wikipedia.org/wiki/Program_Segment_Prefix
    // Offset 80h: length of command line
    // Offset 81h-FFH: command line
    mc->si = cmdline;
    if (*mc->si == 0) usage(mc);
    space_cut(mc);

    // :162
    mc->part = 0;
    mc->ff_flg = 0;
    mc->x68_flg = 0;
    mc->dt2_flg = 0;
    mc->opl_flg = 0;
    mc->save_flg = 1;
    mc->memo_flg = 1;
    mc->pcm_flg = 1;
#if hyouka
    mc->play_flg = 1;
    mc->prg_flg = 2;
#else
    mc->play_flg = 0;
    mc->prg_flg = 0;
#endif

    // :181
    char *env_cmdline = search_env(mc, "MCOPT");
    if (env_cmdline) {
        char *old_si = mc->si;
        mc->si = env_cmdline;
        if (get_option(mc, false, true) != 0) {
            print_mes(mc, warning_mes);
            print_mes(mc, mcopt_err_mes);
        }
        mc->si = old_si;
    }
    get_option(mc, false, false);

    // :211
    char *mml_filename = mc->mml_filename;
    bool mml_filename_ext = false;
    for (;; mc->si++) {
        if (sjis_check(*mc->si)) {
            *mml_filename++ = *mc->si++;
            *mml_filename++ = *mc->si;
            continue;
        }
        // The comparison with 0 is not present in the original. It is present
        // here because we use a null terminator for the command line instead of
        // a CR.
        if (*mc->si == ' ' || *mc->si == '\r' || *mc->si == 0) {
            if (!mml_filename_ext) {
#if efc
                *mml_filename++ = '.';
                *mml_filename++ = 'E';
#else
                *mml_filename++ = '.';
                *mml_filename++ = 'M';
#endif
                *mml_filename++ = 'M';
                *mml_filename++ = 'L';
            }
            break;
        }
        if (*mc->si == '\\') mml_filename_ext = false;
        if (*mc->si == '.') mml_filename_ext = true;
        *mml_filename++ = *mc->si;
    }
    *mml_filename = 0;

    // :256
    char oh_filename[128];
    *oh_filename = 0;
    void *input_file = opnhnd(mc, mc->mml_filename, oh_filename);
    if (!input_file) mc->si = NULL, error(mc, 0, 3);
    uint16_t input_len;
    int read_status = redhnd(mc, input_file, mc->mml_buf, sizeof(mc->mml_buf), &input_len);
    int close_status = clohnd(mc, input_file);
    if (read_status != 0 || close_status != 0) mc->si = NULL, error(mc, 0, 3);

    // :288
    // This logic works a bit differently due to the differences between real
    // DOS EOF handling and the mc_sys functions in this version.
    char *mml_end = mc->mml_buf + input_len;
    *mml_end = 0;
    if (mml_end[-2] != '\r' || mml_end[-1] != '\n') {
        *mml_end++ = '\r';
        *mml_end++ = '\n';
        *mml_end++ = 0;
    }
    mc->mml_endadr = mml_end;

    // :314
    if (mml_end >= mc->mml_buf + sizeof(mc->mml_buf)) mc->si = NULL, error(mc, 0, 18);

    // :319
    if (oh_filename[0] != 0) strcpy(mc->mml_filename, oh_filename);

    // :341
#if !hyouka
    mml_filename = mc->mml_filename;
    char *m_filename = mc->m_filename;
    while ((*m_filename++ = *mml_filename++) != '.') ;

    mc->file_ext_adr = m_filename;
#if efc
    *m_filename++ = 'E';
    *m_filename++ = 'F';
    *m_filename++ = 'C';
#else
    *m_filename++ = 'M';
#endif
    *m_filename++ = 0;
#endif

    // :377
    // TODO: PMD resident functionality not implemented

    // :418
    mc->pmd_flg = 0;
    memset(mc->voice_buf, 0, sizeof(mc->voice_buf));

    // :433
    space_cut(mc);
    if (*mc->si != 0) {
        read_fffile(mc);
    }

    // :443
    memset(mc->prg_num, 0, sizeof(mc->prg_num));

    // :457
#if efc
    if ((mc->prg_flg & 1) == 0) mc->si = NULL, error(mc, 0, 28);
#endif

    // :467
    memset(mc->hsbuf2, 0, sizeof(mc->hsbuf2));
    memset(mc->hsbuf3, 0, sizeof(mc->hsbuf3));

    // :482
    mc->ppzfile_adr = NULL;
    mc->ppsfile_adr = NULL;
    mc->pcmfile_adr = NULL;
    mc->title_adr = NULL;
    mc->composer_adr = NULL;
    mc->arranger_adr = NULL;
    memset(mc->memo_adr, 0, sizeof(mc->memo_adr));

    // :489
    char *user_env = search_env(mc, "USER");
    if (user_env) {
        mc->composer_adr = user_env;
        mc->arranger_adr = user_env;
    }
    char *composer_env = search_env(mc, "COMPOSER");
    if (composer_env) {
        mc->composer_adr = composer_env;
    }
    char *arranger_env = search_env(mc, "ARRANGER");
    if (arranger_env) {
        mc->arranger_adr = arranger_env;
    }

    // :517
    mc->si = mc->mml_buf;
    mc->part = 0;
    mc->pass = 0;
    *mbuf_end(mc) = 0x7f;

    // :527
    mc->skip_flag = 0;

    // :533
    p1cloop(mc);

    // :579
    mc->lopcnt = 0;

    // :585
#if !efc && !olddat
    *m_start(mc) = 2 * mc->opl_flg + mc->x68_flg;
#endif

    // :592
    mc->di = m_buf(mc) + 2 * (max_part + 1);
    if ((mc->prg_flg & 1) != 0) {
        mc->di += 2;
    }
    mc->hsflag = 0;
    mc->prsok = 0;
    mc->part = 1;

    // :605
    mc->pass = 1;

    // :610
    cmloop(mc);

    // :930
#if !efc
    // See the note on cmloop for why :933 is not translated here.
    // :934
    mc->kpart_maxprg = mc->towns_flg != 1 ? mc->maxprg : 0;
    mc->deflng_k = mc->deflng;

    // :946
    fm3_check(mc);
    // :977
    pcm_check(mc);

    // :1007
    rt(mc);

    // :1157
    mc->part = 0;
    if (mc->towns_flg != 1) {
        lc(mc, mc->lc_flag);
        stosd(mc, mc->max_all);
        stosd(mc, mc->max_loop);
    }

    // :1175
    uint8_t *bp = mc->di;
    mc->di += 2;
    *mc->di++ = mc->towns_flg != 1 ? vers : 0x46;
    *mc->di++ = -2;
#endif

    // :1193
#if !hyouka
    if ((mc->prg_flg & 1) != 0) {
        write16(m_buf(mc) + 2 * (max_part + 1), mc->di - m_buf(mc));
        // :1205
        mc->sib = mc->voice_buf;
#if split
        mc->sib++;
#endif
        // :1213
        if (mc->opl_flg == 1) {
            nd_s_opl_loop(mc);
        } else {
            nd_s_loop(mc);
        }
        // :1278
        *mc->di++ = 0x00;
        *mc->di++ = 0xff;
    }
#endif

    // :1285
#if !efc
    if (mc->towns_flg != 1) {
        memow_loop0(mc, &mc->ppzfile_adr);
    }
    memow_loop0(mc, &mc->ppsfile_adr);
    memow_loop0(mc, &mc->pcmfile_adr);

    // :1315
    mc->si = mc->title_adr;
    mc->title_adr = mc->dic;
    if (mc->si) {
        // :1325
        mc->dic = set_strings(mc, mc->dic);
    } else {
        // :1322
        *mc->di++ = 0;
    }

    // :1331
    mc->si = mc->composer_adr;
    mc->composer_adr = mc->dic;
    if (mc->si) {
        // :1341
        mc->dic = set_strings(mc, mc->dic);
    } else {
        // :1338
        *mc->di++ = 0;
    }

    // :1354
    mc->si = mc->arranger_adr;
    mc->arranger_adr = mc->dic;
    if (mc->si) {
        // :1364
        mc->dic = set_strings(mc, mc->dic);
    } else {
        // :1361
        *mc->di++ = 0;
    }

    // :1377
    for (char **memo = mc->memo_adr; *memo; memo++) {
        mc->si = *memo;
        *memo = mc->dic;
        mc->dic = set_strings(mc, mc->dic);
    }

    // :1390
    write16(bp, mc->di - m_buf(mc));

    // :1393
    // The original code here relies heavily on the fact that pointers are
    // 16 bits: it actually adjusts the various _adr variables to be relative
    // to m_buf as part of the translation loops above, and then this part just
    // stores those values as-is. We can't do that here, since pointers are not
    // just 16-bit values that we can set to anything we want. Instead, we
    // compute the offsets here when storing them.
    if (mc->towns_flg != 1) stosw(mc, (uint8_t*)mc->ppzfile_adr - m_buf(mc));
    stosw(mc, (uint8_t*)mc->ppsfile_adr - m_buf(mc));
    stosw(mc, (uint8_t*)mc->pcmfile_adr - m_buf(mc));
    stosw(mc, (uint8_t*)mc->title_adr - m_buf(mc));
    stosw(mc, (uint8_t*)mc->composer_adr - m_buf(mc));
    stosw(mc, (uint8_t*)mc->arranger_adr - m_buf(mc));
    for (char **memo = mc->memo_adr; *memo; memo++) {
        stosw(mc, (uint8_t*)*memo - m_buf(mc));
    }
    // The original code always ends up storing a "null" memo as part of the
    // loop (barring memo overflow, which leads to unpredictable behavior
    // regardless), but we can't reproduce that same logic exactly for the
    // reason described above on pointer usage. Instead, we just store the extra
    // "null" entry explicitly here.
    stosw(mc, 0);
#endif

    // :1408
    if (*mbuf_end(mc) != 0x7f) mc->si = NULL, error(mc, 0, 19);

    // :1413
#if !hyouka
    // :1417
    write_ff(mc);
    // :1448
    write_disk(mc);
#else
    // :1470
    // TODO: PMD resident functionality not implemented
#endif

    // :1494
    print_mes(mc, finmes);

    // :1496
    // TODO: PMD resident functionality not implemented

    // :1560
    msdos_exit(mc);
}

// :533
static void p1cloop(struct mc *mc) {
    for (;; line_skip(mc)) {
        mc->linehead = mc->si;
    p1c_next: ;
        char c = *mc->si++;
        if (sjis_check(c)) {
            mc->si++;
            goto p1c_next;
        }
        if (c == 0) break;
        if (!is_graph(c) || c == ';') continue;
        if (c == '`') {
            mc->skip_flag ^= 2;
            goto p1c_next;
        }
        if ((mc->skip_flag & 2) != 0) continue;

        // :555
        if (c == '!') {
            hsset(mc);
        } else if (c == '#') {
            macro_set(mc);
        } else if (c == '@') {
            new_neiro_set(mc);
        } else {
            goto p1c_next;
        }
    }
}

// :610
// The structure of cmloop and cmloop2 has been slightly reworked in this
// translation for clarity.
//
// In the original, cmloop is used to loop over all standard parts (:928),
// including the logic of cmloop2. After that, some additional initialization
// logic (:930) is executed, which is not part of the cmloop logic. However,
// right after the initialization, there are checks to process FM3 and PPZ
// extend channels (:946, :977), which actually include jumps back to cmloop2
// to process those parts (if present). The original logic takes care not to
// repeat the initialization logic at :930 or reprocess extended parts (by
// setting the part letter to 0 before jumping to cmloop2).
//
// This translation avoids this by making cmloop2 its own function: it is
// called in each iteration of cmloop for the standard parts as expected, and
// called for each extended part separately. This avoids the additional
// measures taken to avoid reprocessing in the original, and makes it possible
// to write the logic in readable C without becoming a goto soup.
static void cmloop(struct mc *mc) {
    for (;;) {
#if !efc
        // :613
        if (mc->opl_flg != 0 || mc->x68_flg != 0) {
            // :620
            mc->ongen = mc->part == 10 ? pcm : fm;
        } else {
            // :631
            uint8_t ongen = 0;
            for (int i = mc->part; i >= 4; i -= 3) ongen++;
            mc->ongen = ongen;
        }
#endif
        // :645
        write16(m_buf(mc) + 2 * (mc->part - 1), mc->di - m_buf(mc));

        // :659
        cmloop2(mc);

        // :922
        mc->part++;
#if efc
        if (mc->part >= max_part + 2) break;
#else
        if (mc->part >= max_part + 1) break;
#endif
    }
}

// :659
static void cmloop2(struct mc *mc) {
    mc->si = mc->mml_buf;
    cm_init(mc);

#if !efc
    // :664
    if (mc->part == 1 && mc->fm3_partchr[0] != 0) {
        *mc->di++ = 0xc6;
        mc->fm3_ofsadr = mc->di;
        for (int i = 0; i < 3; i++) stosw(mc, 0);
    }
    // :678
    if (mc->part == pcmpart && mc->pcm_partchr[0] != 0) {
        *mc->di++ = 0xb4;
        mc->pcm_ofsadr = mc->di;
        for (int i = 0; i < 8; i++) stosw(mc, 0);
    }
    // :691
    if (mc->part == 7) {
        // :693
        if (mc->zenlen != 96) {
            *mc->di++ = 0xdf;
            *mc->di++ = mc->zenlen;
        }
#if !tempo_old_flag
        // :700
        if (mc->tempo != 0) {
            *mc->di++ = 0xfc;
            *mc->di++ = 0xff;
            *mc->di++ = mc->tempo;
        }
#endif
        // :708
        if (mc->timerb != 0) {
            *mc->di++ = 0xfc;
            *mc->di++ = mc->timerb;
        }
        // :715
        if (mc->towns_flg != 1) {
            // :717
            if (mc->fm_voldown != 0) {
                *mc->di++ = 0xc0;
                *mc->di++ = 0xfe + mc->fm_voldown_flag;
                *mc->di++ = mc->fm_voldown;
            }
            // :726
            if (mc->ssg_voldown != 0) {
                *mc->di++ = 0xc0;
                *mc->di++ = 0xfc + mc->ssg_voldown_flag;
                *mc->di++ = mc->ssg_voldown;
            }
            // :735
            if (mc->pcm_voldown != 0) {
                *mc->di++ = 0xc0;
                *mc->di++ = 0xfa + mc->pcm_voldown_flag;
                *mc->di++ = mc->pcm_voldown;
            }
            // :744
            if (mc->ppz_voldown != 0) {
                *mc->di++ = 0xc0;
                *mc->di++ = 0xf5 + mc->ppz_voldown_flag;
                *mc->di++ = mc->ppz_voldown;
            }
            // :753
            if (mc->rhythm_voldown != 0) {
                *mc->di++ = 0xc0;
                *mc->di++ = 0xf8 + mc->rhythm_voldown_flag;
                *mc->di++ = mc->rhythm_voldown;
            }
        }
    }
    // :762
    if (mc->opl_flg == 0 && mc->x68_flg == 0) {
        // :766
        if (mc->ext_detune != 0 && mc->ongen == psg) {
            *mc->di++ = 0xcc;
            *mc->di++ = 0x01;
        }
        // :773
        if (mc->ext_env != 0 && mc->ongen >= psg && mc->part != rhythm2) {
            *mc->di++ = 0xc9;
            *mc->di++ = 0x01;
        }
    }
    // :782
    if (mc->ext_lfo != 0 && mc->part != rhythm2) {
        *mc->di++ = 0xca;
        *mc->di++ = 0x01;
        if (mc->towns_flg != 1) {
            *mc->di++ = 0xbb;
            *mc->di++ = 0x01;
        }
    }
    // :793
    if (mc->towns_flg != 1 && mc->adpcm_flag != 0xff && mc->part == pcmpart) {
        *mc->di++ = 0xc0;
        *mc->di++ = 0xf7;
        *mc->di++ = mc->adpcm_flag;
    }
    // :804
    if (mc->transpose != 0 && mc->part != rhythm2) {
        *mc->di++ = 0xb2;
        *mc->di++ = mc->transpose;
    }
#endif

    // :817
    for (;;) {
        char c = *mc->si++;
        if (sjis_check(c)) {
            mc->si++;
            continue;
        } else if (c == 0) {
            break;
        } else if (!is_graph(c) || c == ';') {
            // :871
            line_skip(mc);
            continue;
        } else if (c == '`') {
            mc->skip_flag ^= 2;
            continue;
        } else if ((mc->skip_flag & 2) != 0) {
            // :837
        } else if (c == '!') {
            hsset(mc);
            // :2968, :2971 -> :871
            line_skip(mc);
            continue;
        } else if (c == '"') {
            // :842
            mc->skip_flag ^= 1;
            *mc->di++ = 0xc0;
            *mc->di++ = mc->skip_flag & 1;
            continue;
        } else if (c == '\'') {
            // :852
            mc->skip_flag &= 0xfe;
            *mc->di++ = 0xc0;
            *mc->di++ = 0x00;
            continue;
        }
        uint8_t compile_part;
#if efc
        // :858
        mc->si--;
        uint16_t n;
        if (lngset(mc, &n) != 0) {
            line_skip(mc);
            continue;
        }
        compile_part = n;
#else
        // :864
        compile_part = c - 'A' + 1;
#endif
        if (compile_part == mc->part) {
            if (!one_line_compile(mc)) break;
        }
    }

    // :875
    mc->si = NULL;

    // :881
    check_lopcnt(mc);

    // :902
    if (mc->porta_flag != 0) error(mc, '{', 9);
    // :906
    if (mc->allloop_flag != 0 && mc->length_check1 == 0) error(mc, 'L', 10);

    // :916
    *mc->di++ = 0x80;
}

// :881
static void check_lopcnt(struct mc *mc) {
    for (;;) {
        if (mc->lopcnt == 0) break;
        print_mes(mc, warning_mes);
        put_part(mc);
        print_mes(mc, loop_err_mes);
        *mc->di++ = 0xf8;
        // BUG: jumps to the wrong place and bh is not clearly defined
        edl00(mc, 1);
    }
}

#if !efc

// :946
static void fm3_check(struct mc *mc) {
    if (!mc->fm3_ofsadr) return;
    // See the note on cmloop for why the part characters do not need to be set
    // to 0 here.
    if (mc->fm3_partchr[0] != 0) fm3c_main(mc, mc->fm3_partchr[0]);
    if (mc->fm3_partchr[1] != 0) fm3c_main(mc, mc->fm3_partchr[1]);
    if (mc->fm3_partchr[2] != 0) fm3c_main(mc, mc->fm3_partchr[2]);
}

// :961
static void fm3c_main(struct mc *mc, char part) {
    write16(mc->fm3_ofsadr, mc->di - m_buf(mc));
    mc->fm3_ofsadr += 2;
    mc->part = part - 'A' + 1;
    mc->ongen = fm;
    cmloop2(mc);
}

// :977
static void pcm_check(struct mc *mc) {
    if (!mc->pcm_ofsadr) return;
    for (int i = 0; i < 8; i++) {
    // See the note on cmloop for why the part characters do not need to be set
    // to 0 here.
        if (mc->pcm_partchr[i] != 0) pcmc_main(mc, mc->pcm_partchr[i]);
    }
}

// :991
static void pcmc_main(struct mc *mc, char part) {
    write16(mc->pcm_ofsadr, mc->di - m_buf(mc));
    mc->pcm_ofsadr += 2;
    mc->part = part - 'A' + 1;
    mc->ongen = pcm_ex;
    cmloop2(mc);
}

// :1007
static void rt(struct mc *mc) {
    // :1012
    uint8_t *data = m_buf(mc) + 2 * max_part;
    mc->part = rhythm;
    mc->ongen = pcm;
    // :1017
    write16(data, mc->di - m_buf(mc));

    // :1024
    data = mc->di + 2 * mc->kpart_maxprg;
    // :1031
    mc->pass = 2;
    // :1033
    mc->si = mc->mml_buf;
    cm_init(mc);
    mc->deflng = mc->deflng_k;

    // :1042
    if (mc->kpart_maxprg != 0) stosw(mc, data - m_buf(mc));

    // :1054
    for (;;) {
        char c = *mc->si++;
        if (sjis_check(c)) {
            mc->si++;
        } else if (!is_graph(c) || c == ';') {
            // :1064, :1067 -> :1103
            line_skip(mc);
            // :1051
            if (*mc->si == 0) break;
        } else if (c == '`') {
            // :1071
            mc->skip_flag ^= 2;
        } else if ((mc->skip_flag & 2) == 0 && c == '!') {
            hsset(mc);
            // :2969 -> :1103
            line_skip(mc);
            // :1051
            if (*mc->si == 0) break;
        } else if (mc->kpart_maxprg == 0) {
            // :1082 -> :1103
            line_skip(mc);
            // :1051
            if (*mc->si == 0) break;
        } else if (c == 'R') {
            // :1087
            uint8_t *old_di = mc->di;
            mc->di = data;
            // :1089
            mc->hsflag = 1;
            mc->prsok = 0;
            one_line_compile(mc);
            *mc->di++ = 0xff;
            // :1098
            data = mc->di;
            mc->di = old_di;
            mc->kpart_maxprg--;
            // :1042
            if (mc->kpart_maxprg != 0) stosw(mc, data - m_buf(mc));
            // :1051
            if (*mc->si == 0) break;
        }
    }

    // :1106
    mc->di = data;
    if (mc->kpart_maxprg != 0) mc->si = NULL, error(mc, 0, 29);
}

#endif

// :1118
static void cm_init(struct mc *mc) {
    mc->maxprg = 0;
    mc->volss = 0;
    mc->volss2 = 0;
    mc->octss = 0;
    mc->skip_flag = 0;
    mc->tie_flag = 0;
    mc->porta_flag = 0;
    mc->ss_speed = 0;
    mc->ss_depth = 0;
    mc->ss_length = 0;
    mc->ge_delay = 0;
    mc->ge_depth = 0;
    mc->ge_depth2 = 0;
    mc->pitch = 0;
    mc->master_detune = 0;
    mc->detune = 0;
    mc->def_a = 0;
    mc->def_b = 0;
    mc->def_c = 0;
    mc->def_d = 0;
    mc->def_e = 0;
    mc->def_f = 0;
    mc->def_g = 0;
    mc->prsok = 0;
    mc->alldet = 0x8000;
    // :1146
    mc->octave = 3;
    mc->deflng = mc->zenlen >> 2;
}

// :1219
static void nd_s_opl_loop(struct mc *mc) {
    for (int i = 0; i < 256; i++) {
        if (mc->prg_num[i] != 0) {
            // :1223
            *mc->di++ = i;
            memcpy(mc->di, mc->sib, 9);
            mc->di += 9;
        }
        mc->sib += 16;
    }
}

// :1251
static void nd_s_loop(struct mc *mc) {
    for (int i = 0; i < 256; i++) {
        if (mc->prg_num[i] != 0) {
            // :1255
            *mc->di++ = i;
            memcpy(mc->di, mc->sib, 25);
            mc->di += 25;
        }
        mc->sib += 32;
    }
}

// :1296
static void memow_loop0(struct mc *mc, char **adr) {
    mc->si = *adr;
    *adr = mc->dic;
    if (mc->si) {
        // :1307
        mc->dic = set_strings2(mc, mc->dic);
    } else {
        // :1304
        *mc->di++ = 0;
    }
}

#if !hyouka

// :1417
static void write_ff(struct mc *mc) {
    if ((mc->prg_flg & 2) == 0) return;
    if (mc->ff_flg == 0) {
        print_mes(mc, warning_mes);
        print_mes(mc, not_ff_mes);
        return;
    }
    uint16_t n = mc->opl_flg != 1 ? 8 * 1024 : 4 * 1024;
    if (diskwrite(mc, mc->v_filename, mc->voice_buf, n) != 0) {
        mc->si = NULL, error(mc, 0, 5);
    }
}

// :1448
static void write_disk(struct mc *mc) {
    if (mc->save_flg == 0) return;
    uint16_t n = mc->di - m_start(mc);
    if (diskwrite(mc, mc->m_filename, m_start(mc), n)) {
        mc->si = NULL, error(mc, 0, 4);
    }
}

#endif

// :1562
static void line_skip(struct mc *mc) {
    while (*mc->si++ != '\n') ;
}

// :1803
static void read_fffile(struct mc *mc) {
    mc->ff_flg = 1;

    // :1813
    // Note: same overall logic as :211.
    char *v_filename = mc->v_filename;
    bool v_filename_ext = false;
    for (;; mc->si++) {
        if (sjis_check(*mc->si)) {
            *v_filename++ = *mc->si++;
            *v_filename++ = *mc->si;
            continue;
        }
        // The comparison with 0 is not present in the original. It is present
        // here because we use a null terminator for the command line instead of
        // a CR.
        if (*mc->si == ' ' || *mc->si == '\r' || *mc->si == 0) {
            if (!v_filename_ext) {
                *v_filename++ = '.';
                *v_filename++ = 'F';
                *v_filename++ = 'F';
                if (mc->opl_flg == 1) *v_filename++ = 'L';
            }
            break;
        }
        if (*mc->si == '\\') v_filename_ext = false;
        if (*mc->si == '.') v_filename_ext = true;
        *v_filename++ = *mc->si;
    }
    *v_filename = 0;

    // :1862
    char oh_filename[128];
    void *input_file = opnhnd(mc, mc->v_filename, oh_filename);
    if (!input_file) {
        // :1897
        print_mes(mc, warning_mes);
        print_mes(mc, ff_readerr_mes);
        return;
    }
    uint16_t input_len;
    int read_status = redhnd(mc, input_file, mc->voice_buf, sizeof(mc->voice_buf), &input_len);
    int close_status = clohnd(mc, input_file);
    if (read_status != 0 || close_status != 0) {
        // :1897
        print_mes(mc, warning_mes);
        print_mes(mc, ff_readerr_mes);
        return;
    }
#if !hyouka
    mc->prg_flg |= 1;
#endif
}

// :1909
// is_mml: ds == mml_seg
// is_env: ds == [kankyo_seg]
static int get_option(struct mc *mc, bool is_mml, bool is_env) {
    for (;;) {
        char c = *mc->si++;
        if (c == ' ') continue;
        if (is_cntrl(c)) break;
        if (c == '/' || c == '-') {
            // :1932
            switch (to_upper(*mc->si++)) {
            case 'V':
                // :1971
                if (to_upper(*mc->si) == 'W') {
                    mc->si++;
#if !hyouka
                    mc->prg_flg |= 2;
#endif
                } else {
#if !hyouka
                    mc->prg_flg |= 1;
#endif
                }
                break;
            case 'P':
                // :1990
#if !hyouka
                mc->play_flg = 1;
#endif
                break;
            case 'S':
                // :1999
#if !hyouka
                mc->save_flg = 0;
                mc->play_flg = 1;
#endif
                break;
            case 'M':
                // :2009
                mc->towns_flg = 0;
                mc->x68_flg = 1;
                mc->opl_flg = 0;
                mc->dt2_flg = 1;
                break;
            case 'T':
                // :2029
                mc->towns_flg = 1;
                mc->x68_flg = 0;
                mc->opl_flg = 0;
                mc->dt2_flg = 0;
                break;
            case 'N':
                // :2019
                mc->towns_flg = 0;
                mc->x68_flg = 0;
                mc->opl_flg = 0;
                mc->dt2_flg = 0;
                break;
            case 'L':
                // :2039
                mc->opl_flg = 1;
                break;
            case 'O':
                // :2046
                mc->memo_flg = 0;
                break;
            case 'A':
                // :2053
                mc->pcm_flg = 0;
                break;
            case 'C':
                // :2060
                mc->lc_flag = 1;
                break;
            default:
                // :1955
                if (is_mml) {
                    error(mc, 0, 0);
                } else if (is_env) {
                    return 1;
                } else {
                    mc->si = NULL, error(mc, 0, 0);
                }
                break;
            }
        } else {
            // :1926
            if (is_mml) {
                error(mc, 0, 0);
            } else if (is_env) {
                return 1;
            } else {
                break;
            }
        }
    }
    // :2064
    mc->si--;
    return 0;
}

// :2074
static void macro_set(struct mc *mc) {
#if !efc
    char *macro_start = mc->si;
#endif
    char c1 = to_upper(*mc->si++);
    char c2 = to_upper(*mc->si++);
    if (move_next_param(mc) != 0) error(mc, '#', 6);
    // :2086
    switch (c1) {
#if !efc
    case 'P':
        pcmfile_set(mc, c2, macro_start);
        break;
    case 'T':
        title_set(mc, c2, macro_start);
        break;
    case 'C':
        composer_set(mc);
        break;
    case 'A':
        arranger_set(mc, c2);
        break;
    case 'M':
        memo_set(mc);
        break;
    case 'Z':
        zenlen_set(mc);
        break;
    case 'L':
        LFOExtend_set(mc, c2);
        break;
    case 'E':
        EnvExtend_set(mc);
        break;
    case 'V':
        VolDown_set(mc);
        break;
    case 'J':
        JumpFlag_set(mc);
        break;
#endif
    case 'F':
        FM3Extend_set(mc, c2);
        break;
    case 'D':
        dt2flag_set(mc, c2);
        break;
    case 'O':
        octrev_set(mc, c2);
        break;
    case 'B':
        bend_set(mc);
        break;
    case 'I':
        include_set(mc);
        break;
    default: error(mc, '#', 7);
    }
    // :2123
    *mc->linehead = ';';
}

// :2217
static void ppzfile_set(struct mc *mc) {
    mc->ppzfile_adr = mc->si;
}

// :2118
PMDC_NORETURN static void ps_error(struct mc *mc) {
    error(mc, '#', 7);
}

// :2132
// c2: ah
// macro_start: bx
static void pcmfile_set(struct mc *mc, char c2, char *macro_start) {
    switch (c2) {
    case 'P':
        ppsfile_set(mc, macro_start);
        return;
    case 'C':
        if (to_upper(macro_start[2]) != 'M') ps_error(mc);
        switch (to_upper(macro_start[3])) {
        case 'V':
            pcmvolume_set(mc);
            return;
        case 'E':
            pcmextend_set(mc, macro_start);
            return;
        case 'F':
            ppcfile_set(mc);
            return;
        default:
            ps_error(mc);
            return;
        }
    default:
        ps_error(mc);
        return;
    }
}

// :2149
static void ppcfile_set(struct mc *mc) {
    mc->pcmfile_adr = mc->si;
}

// :2156
static void pcmvolume_set(struct mc *mc) {
    switch (to_upper(*mc->si++)) {
    case 'N':
        mc->pcm_vol_ext = 0;
        return;
    case 'E':
        mc->pcm_vol_ext = 1;
        return;
    default:
        ps_error(mc);
        return;
    }
}

// :2172
// macro_start: bx
static void pcmextend_set(struct mc *mc, char *macro_start) {
#if !efc
    if (to_upper(macro_start[3]) == 'F') {
        ppzfile_set(mc);
        return;
    }
    if (is_cntrl(*mc->si)) error(mc, '#', 6);
    char c = *mc->si++;
    if (partcheck(c) != 0) ps_error(mc);
    char *partchr = mc->pcm_partchr;
    *partchr++ = c;

    // :2195
    for (int i = 0; i < 7; i++) {
        if (partcheck(*mc->si) != 0) return;
        *partchr++ = *mc->si++;
    }
#else
    ps_error(mc);
#endif
}

// :2224
// macro_start: bx
static void ppsfile_set(struct mc *mc, char *macro_start) {
    switch (to_upper(macro_start[2])) {
    case 'C':
        ppcfile_set(mc);
        return;
    case 'Z':
        pcmextend_set(mc, macro_start);
        return;
    case 'S':
        mc->ppsfile_adr = mc->si;
        return;
    default:
        ps_error(mc);
        return;
    }
}

// :2239
// c2: ah
// macro_start: bx
static void title_set(struct mc *mc, char c2, char *macro_start) {
    switch (c2) {
    case 'E':
        tempo_set(mc);
        return;
    case 'R':
        transpose_set(mc);
        return;
    default:
        if (to_upper(macro_start[2]) == 'M') {
            tempo_set2(mc);
            return;
        }
        mc->title_adr = mc->si;
        return;
    }
}

// :2255
static void composer_set(struct mc *mc) {
    mc->composer_adr = mc->si;
}

// :2262
// c2: ah
static void arranger_set(struct mc *mc, char c2) {
    if (c2 == 'D') {
        adpcm_set(mc);
        return;
    }
    mc->arranger_adr = mc->si;
}

// :2272
static void adpcm_set(struct mc *mc) {
    if (to_upper(*mc->si++) != 'O') ps_error(mc);
    switch (to_upper(*mc->si++)) {
    case 'N':
        mc->adpcm_flag = 1;
        return;
    case 'F':
        // BUG: sets the wrong value
        mc->adpcm_flag = 1;
        return;
    default:
        ps_error(mc);
        return;
    }
}

// :2292
static void memo_set(struct mc *mc) {
    char **memo;
    for (memo = mc->memo_adr; *memo; memo++) ;
    *memo = mc->si;
}

// :2304
static void transpose_set(struct mc *mc) {
    mc->transpose = getnum(mc);
}

// :2312
static void detune_select(struct mc *mc) {
    switch (to_upper(*mc->si++)) {
    case 'N':
        mc->ext_detune = 0;
        return;
    case 'E':
        mc->ext_detune = 1;
        return;
    default:
        ps_error(mc);
        return;
    }
}

// :2328
// c2: ah
static void LFOExtend_set(struct mc *mc, char c2) {
    if (c2 == 'O') {
        loopdef_set(mc);
        return;
    }

    switch (to_upper(*mc->si++)) {
    case 'N':
        mc->ext_lfo = 0;
        return;
    case 'E':
        mc->ext_lfo = 1;
        return;
    default:
        ps_error(mc);
        return;
    }
}

// :2346
static void loopdef_set(struct mc *mc) {
    uint16_t n;
    if (lngset(mc, &n) != 0) ps_error(mc);
    mc->loop_def = n;
}

// :2355
static void EnvExtend_set(struct mc *mc) {
    switch (to_upper(*mc->si++)) {
    case 'N':
        mc->ext_env = 0;
        return;
    case 'E':
        mc->ext_env = 1;
        return;
    default:
        ps_error(mc);
        return;
    }
}

// :2371
static void VolDown_set(struct mc *mc) {
    for (;; mc->si++) {
        uint8_t fspr = 0;
        for (char c = *mc->si; ; c = *++mc->si) {
            if (c <= '9') break;
            switch (to_upper(c)) {
            case 'F':
                fspr |= 1;
                break;
            case 'S':
                fspr |= 2;
                break;
            case 'P':
                fspr |= 4;
                break;
            case 'R':
                fspr |= 8;
                break;
            case 'Z':
                fspr |= 16;
                break;
            default:
                mc->si++;
                error(mc, '#', 1);
                break;
            }
        }
        bool absolute = *mc->si != '+' && *mc->si != '-';
        if (fspr == 0) error(mc, '#', 1);
        uint16_t n = getnum(mc);
        if ((fspr & 1) != 0) {
            mc->fm_voldown = n;
            mc->fm_voldown_flag = absolute;
        }
        if ((fspr & 2) != 0) {
            mc->ssg_voldown = n;
            mc->ssg_voldown_flag = absolute;
        }
        if ((fspr & 4) != 0) {
            mc->pcm_voldown = n;
            mc->pcm_voldown_flag = absolute;
        }
        if ((fspr & 8) != 0) {
            mc->rhythm_voldown = n;
            mc->rhythm_voldown_flag = absolute;
        }
        if ((fspr & 16) != 0) {
            mc->ppz_voldown = n;
            mc->ppz_voldown_flag = absolute;
        }
        if (*mc->si != ',') break;
    }
}

// :2458
static void JumpFlag_set(struct mc *mc) {
    uint16_t n;
    if (lngset(mc, &n) != 0) error(mc, '#', 6);
    mc->jump_flag = n;
}

// :2468
static void tempo_set(struct mc *mc) {
    uint16_t n;
    if (lngset(mc, &n) != 0) error(mc, '#', 6);
#if tempo_old_flag
    mc->timerb = timerb_get(n);
#else
    mc->tempo = n;
#endif
}

// :2483
static void tempo_set2(struct mc *mc) {
    uint16_t n;
    if (lngset(mc, &n) != 0) error(mc, '#', 6);
    mc->timerb = n;
}

// :2493
static void zenlen_set(struct mc *mc) {
    uint16_t n;
    if (lngset(mc, &n) != 0) error(mc, '#', 6);
    mc->zenlen = n;
    mc->deflng = n >> 2;
}

// :2508
// c2: ah
static void FM3Extend_set(struct mc *mc, char c2) {
    switch (c2) {
    case 'I':
        file_name_set(mc);
        return;
    case 'F':
        fffile_set(mc);
        return;
    }

#if !efc
    if (is_cntrl(*mc->si)) error(mc, '#', 6);
    if (partcheck(*mc->si) != 0) mc->si++, ps_error(mc);
    mc->fm3_partchr[0] = *mc->si++;
    if (partcheck(*mc->si) != 0) return;
    mc->fm3_partchr[1] = *mc->si++;
    if (partcheck(*mc->si) != 0) return;
    mc->fm3_partchr[2] = *mc->si++;
#else
    ps_error(mc);
#endif
}

// :2544
static void file_name_set(struct mc *mc) {
#if !hyouka
    char *filename;
    if (*mc->si == '.') {
        filename = mc->file_ext_adr;
        mc->si++;
    } else {
        filename = mc->m_filename;
    }
    for (; *mc->si != ';' && is_graph(*mc->si); ) {
        *filename++ = *mc->si++;
    }
    *filename = 0;
#endif
}

// :2569
static void fffile_set(struct mc *mc) {
    read_fffile(mc);
}

// :2580
// c2: ah
static void dt2flag_set(struct mc *mc, char c2) {
#if !efc
    if (c2 == 'E') {
        detune_select(mc);
        return;
    }
#else
    (void)c2;
#endif

    if (to_upper(*mc->si++) != 'O') ps_error(mc);
    switch (to_upper(*mc->si++)) {
    case 'N':
        mc->dt2_flg = 1;
        return;
    case 'F':
        mc->dt2_flg = 0;
        return;
    default:
        ps_error(mc);
        return;
    }
}

// :2604
// c2: ah
static void octrev_set(struct mc *mc, char c2) {
    if (c2 == 'P') {
        option_set(mc);
        return;
    }

    switch (to_upper(*mc->si++)) {
    case 'R':
        *ou00(mc) = '<';
        *od00(mc) = '>';
        return;
    case 'N':
        *ou00(mc) = '>';
        *od00(mc) = '<';
        return;
    default:
        ps_error(mc);
        return;
    }
}

// :2624
static void option_set(struct mc *mc) {
    get_option(mc, true, false);
}

// :2631
static void bend_set(struct mc *mc) {
    uint16_t n;
    if (lngset(mc, &n) != 0) error(mc, '#', 6);
    mc->bend = n;
}

// :2641
static void include_set(struct mc *mc) {
    set_strings(mc, mc->mml_filename2);
    char *cr_ptr = mc->si;
    mc->si += 2;

    // :2662
    size_t rest_len = mc->mml_endadr - mc->si;
    char *rest_ptr = mc->mml_buf + sizeof(mc->mml_buf) - rest_len;
    memmove(rest_ptr, mc->si, rest_len);

    // :2677
    char *inc_mml = mc->si;
    *inc_mml++ = 1;

    // :2688
    char oh_filename[128];
    oh_filename[0] = 0;
    void *file = opnhnd(mc, mc->mml_filename2, oh_filename);
    if (!file) error(mc, '#', 3);
    char *inc_filename = oh_filename[0] != 0 ? oh_filename : mc->mml_filename2;
    while ((*inc_mml++ = *inc_filename++) != 0) ;
    *inc_mml++ = '\n';

    // :2719
    uint16_t inc_len;
    uint16_t inc_read_len = rest_ptr - inc_mml + 1;
    int read_status = redhnd(mc, file, inc_mml, inc_read_len, &inc_len);
    int close_status = clohnd(mc, file);
    if (read_status != 0 || close_status != 0 || inc_len == inc_read_len) {
        // :2797
        mc->si = cr_ptr;
        error(mc, '#', 18);
    }
    inc_mml += inc_len;

    // :2746
    // As far as I can tell, the inc_eof_chk_loop in the original source code
    // does not behave as intended, because bx is never set to anything useful
    // before it executes. It may have been copied from eof_chk_loop (:290),
    // and should probably be checking di instead of bx.
    // Regardless, it doesn't matter for this version, since the mc_sys
    // functions don't require this logic.
    if (inc_mml[-2] != '\r' || inc_mml[-1] != '\n') {
        *inc_mml++ = '\r';
        *inc_mml++ = '\n';
    }

    // :2768
    *inc_mml++ = 2;
    *inc_mml++ = '\n';
    if (inc_mml > rest_ptr) {
        // :2797
        mc->si = cr_ptr;
        error(mc, '#', 18);
    }

    // :2778
    memmove(inc_mml, rest_ptr, rest_len);
    mc->mml_endadr = inc_mml + rest_len;
    mc->si = cr_ptr;
}

// :2804
// c: al
static int partcheck(char c) {
    return c < 'L' || c == 'R' || (uint8_t)c > 0x7f;
}

// :2821
static char *set_strings(struct mc *mc, char *out) {
    while (*mc->si == '\t' || *mc->si == 0x1b || !is_cntrl(*mc->si)) {
        *out++ = *mc->si++;
    }
    *out++ = 0;
    return out;
}

// :2838
static char *set_strings2(struct mc *mc, char *out) {
    while (*mc->si == '\t' || *mc->si == 0x1b || !is_cntrl(*mc->si)) {
        *out++ = to_upper(*mc->si++);
    }
    *out++ = 0;
    return out;
}

// :2865
static int move_next_param(struct mc *mc) {
    for (char c = *mc->si++; c != '\t' && c != ' '; c = *mc->si++) {
        if (is_cntrl(c)) return 1;
    }
    char c = *mc->si;
    while (c == '\t' || c == ' ') c = *++mc->si;
    return is_cntrl(c) && c != 0x1b;
}

// :2894
static void hsset(struct mc *mc) {
    char **ptr;
    uint16_t n;
    if (lngset(mc, &n) == 0) {
        ptr = &mc->hsbuf2[n & 0xff];
    } else {
        char *old_si = mc->si;
        struct mc_hs3 *found, *prefix_found;
        int search_status = search_hs3(mc, &found, &prefix_found);
        mc->si = old_si;
        if (search_status != 0) {
            // :2919
            for (int i = 0; i < 256; i++) {
                if (mc->hsbuf3[i].ptr) continue;
                // :2926
                found = &mc->hsbuf3[i];
                char *new_name = found->name;
                for (int j = 0; j < 30; j++) {
                    if (!is_graph(*mc->si)) break;
                    *new_name++ = *mc->si++;
                }
                // :2937
                while (is_graph(*mc->si)) mc->si++;
                goto found_hs3;
            }
            // :2924
            error(mc, '!', 33);
        }
    found_hs3:
        ptr = &found->ptr;
    }

    // :2946
    char *old_si = mc->si;
    for (;;) {
        char c = *mc->si++;
        if (c == '\r') break;
        if (!is_graph(c)) {
            *ptr = mc->si;
            break;
        }
    }
    mc->si = old_si;
}

// :2986
static void new_neiro_set(struct mc *mc) {
    // :2994
    if (mc->opl_flg == 1) {
        opl_nns(mc);
        return;
    }

    // :2998
    mc->newprg_num = 0;
    mc->alg_fb = 0;
    memset(mc->slot_1, 0, sizeof(mc->slot_1));
    memset(mc->slot_2, 0, sizeof(mc->slot_2));
    memset(mc->slot_3, 0, sizeof(mc->slot_3));
    memset(mc->slot_4, 0, sizeof(mc->slot_4));

    // :3009
    mc->newprg_num = get_param(mc);
    uint8_t alg = get_param(mc) & 7;
    uint8_t fb = get_param(mc) & 7;
    mc->alg_fb = alg | (fb << 3);
    mc->prg_name[0] = 0;

    // :3025
    slot_get(mc, mc->slot_1);
    slot_get(mc, mc->slot_2);
    slot_get(mc, mc->slot_3);
    slot_get(mc, mc->slot_4);

    // :3034
    uint8_t *voice = mc->voice_buf;
#if split
    voice++;
#endif
    voice += 32 * mc->newprg_num;

    // :3049
    slot_trans(voice++, mc->slot_1);
    slot_trans(voice++, mc->slot_3);
    slot_trans(voice++, mc->slot_2);
    slot_trans(voice, mc->slot_4);
    voice += 21;
    *voice++ = mc->alg_fb;

    // :3070
    nns_pname_set(mc, voice);
}

// :3070
static void nns_pname_set(struct mc *mc, uint8_t *dst) {
    char *prg_name = mc->prg_name;
    for (int i = 0; i < 7; i++) {
        *dst++ = *prg_name;
        if (*prg_name != 0) prg_name++;
    }
}

// :3090
static void opl_nns(struct mc *mc) {
    // CHECK: prg_name isn't being null-terminated here? Won't that cause corrupted instrument names?
    memset(mc->oplbuf, 0, sizeof(mc->oplbuf));

    // :3101
    mc->newprg_num = get_param(mc);
    const uint8_t *table = oplprg_table;
    uint8_t *oplbuf = mc->oplbuf;
    for (int i = 0; i < 2 + 11 * 2; i++) {
        // :3110
        uint8_t param = get_param(mc) & table[1];
        param <<= table[2];
        oplbuf[table[0]] |= param;
        table += 3;
    }

    // :3127
    uint8_t *voice = mc->voice_buf + 16 * mc->newprg_num;
    memcpy(voice, mc->oplbuf, 9);
    nns_pname_set(mc, voice + 9);
}

// :3154
static void slot_trans(uint8_t *dst, uint8_t *src) {
    for (int i = 0; i < 6; i++) {
        *dst = *src++;
        dst += 4;
    }
}

// :3168
static void slot_get(struct mc *mc, uint8_t *slot) {
    slot[2] = get_param(mc) & 0x1F;
    slot[3] = get_param(mc) & 0x1F;
    slot[4] = get_param(mc) & 0x1F;
    slot[5] = get_param(mc) & 0x0F;
    slot[5] |= (get_param(mc) & 0x0F) << 4;
    slot[1] = get_param(mc) & 0x7F;
    slot[2] |= (get_param(mc) & 0x03) << 6;
    slot[0] = get_param(mc) & 0x0F;
    uint8_t dt = get_param(mc);
    if ((dt & 0x80) != 0) {
        dt = (-dt & 0x3) | 0x4;
    }
    slot[0] |= (dt & 0x7) << 4;
    // :3213
    if (mc->dt2_flg != 0) {
        slot[4] |= (get_param(mc) & 0x3) << 6;
    }
    // :3221
    slot[3] |= (get_param(mc) & 0x1) << 7;
}

// :3231
static uint8_t get_param(struct mc *mc) {
    for (;;) {
        char c = *mc->si++;
        if (sjis_check(c)) {
            mc->si++;
        } else if (c == '\t' || c == ' ') {
            // :3239, :3241
        } else if (is_cntrl(c) || c == ';') {
            // :3271
            line_skip(mc);
            if (*mc->si == 0) error(mc, '@', 6);
        } else if (c == '`') {
            // :3249
            mc->skip_flag ^= 2;
        } else if ((mc->skip_flag & 2) != 0) {
            // :3253
        } else if (c == '=') {
            // :3278
            while (*mc->si == ' ' || *mc->si == '\t') mc->si++;
            char *prg_name = mc->prg_name;
            for (int i = 0; i < 7; i++) {
                if (*mc->si == '\t' || *mc->si == '\r') break;
                *prg_name++ = *mc->si++;
            }
            *prg_name = 0;
            // :3271
            line_skip(mc);
            if (*mc->si == 0) error(mc, '@', 6);
        } else {
            // :3258
            mc->si--;
            break;
        }
    }

    // :3259
    if (*mc->si != '+' && *mc->si != '-') {
        uint8_t digit;
        if (numget(mc, &digit) != 0) error(mc, '@', 1);
        mc->si--;
    }
    return getnum(mc);
}

// :3308
// Returns true if the part should continue being processed.
static bool one_line_compile(struct mc *mc) {
    mc->lastprg = 0;
    for (;;) {
        char c = *mc->si++;
        if (sjis_check(c)) {
            mc->si++;
            continue;
        } else if (c == '\r') {
            // :3384
            mc->si++;
            // :3387
            return true;
        } else if (c == ';') {
            // :5405
            line_skip(mc);
            // :3387
            return true;
        } else if (c == '`') {
            mc->skip_flag ^= 2;
            continue;
        } else if ((mc->skip_flag & 2) != 0) {
            // :3391
            if (olc_skip2(mc)) {
                break;
            } else {
                // :3387
                return true;
            }
        } else if (!is_graph(c)) {
            // :3329
            olc02(mc);
            break;
        }
    }

    // :3336
    // In the original, olc0, olc02, and olc03 are simply labels within the
    // larger one_line_compile "unit", and commands jump back to one of them
    // depending on the post-processing they need.
    // This structure is not directly implementable in (readable) C code:
    // instead, the olc0 and olc02 post-processing logic is extracted into
    // functions of the same name, which command implementations may call
    // before they return. A return statement in a command is equivalent to
    // "jmp olc03" in the original, as olc03 encapsulates the main command
    // loop.
    return olc03(mc);
}

// :3331
static void olc0(struct mc *mc) {
    mc->prsok = 0;
    olc02(mc);
}

// :3333
static void olc02(struct mc *mc) {
    if (*mbuf_end(mc) != 0x7f) error(mc, 0, 19);
}

// :3336
static bool olc03(struct mc *mc) {
    for (;;) {
        char c = *mc->si++;
        if (sjis_check(c)) {
            mc->si++;
            continue;
        } else if (c == '\t' || c == ' ') {
            continue;
        } else if (is_cntrl(c)) {
            // :3346 -> :3382
        } else if (c == ';') {
            // :5405
            line_skip(mc);
            // :3387
            return true;
        } else if (c == '`') {
            // :3352
            mc->skip_flag ^= 2;
            continue;
        } else if ((mc->skip_flag & 2) != 0) {
            // :3391
            if (olc_skip2(mc)) {
                continue;
            } else {
                // :3387
                return true;
            }
        } else if (c == '"') {
            // :3360
            mc->skip_flag ^= 1;
            parset(mc, 0xc0, mc->skip_flag & 1);
            continue;
        } else if (c == '\'') {
            // :3369
            mc->skip_flag &= 0xfe;
            parset(mc, 0xc0, 0x00);
            continue;
        }
#if !efc
        else if (c == '|') {
            // :3376
            if (skip_mml(mc)) {
                continue;
            } else {
                // :3384
                mc->si++;
                // :3387
                return true;
            }
        }
#endif
        else if (c == '/') {
            // :3409
            return false;
        }
        // :3382
        if (c == '\r') {
            // :3384
            mc->si++;
            // :3387
            return true;
        }
        // :3474
        for (struct mc_com *comtbl = mc->comtbl; ; comtbl++) {
            // :3476
            if (comtbl->cmd == c) {
                // :3484
                comtbl->impl(mc, c);
                break;
            }
            // :3479
            if (comtbl->cmd == 0) error(mc, 0, 1);
        }
    }
}

// :3391
// Returns true if the line should continue being compiled.
static bool olc_skip2(struct mc *mc) {
    for (char c = *mc->si++; ; c = *mc->si++) {
        if (sjis_check(c)) {
            mc->si++;
        } else if (c == '\n') {
            return false;
        } else if (c == '`') {
            mc->skip_flag &= 0xfd;
            return true;
        }
    }
}

// :3419
// Returns true if the line should continue being compiled.
static bool skip_mml(struct mc *mc) {
    for (;;) {
        char part = mc->part + 'A' - 1;
        // :3423
        if (!is_graph(*mc->si)) return true;
        if (*mc->si == '!') {
            // :3427
            mc->si++;
            // :3428
            for (;;) {
                char c = *mc->si++;
                if (c == part) {
                    if (part_not_found(mc)) {
                        // :3456
                        break;
                    } else {
                        // :3454
                        return false;
                    }
                } else if (c == '\r') {
                    return false;
                } else if (!is_graph(c)) {
                    // :3436
                    mc->si--;
                    // :3468, :3465
                    return part_found(mc);
                }
            }
        } else {
            // :3439
            for (;;) {
                char c = *mc->si++;
                if (c == part) {
                    // :3468, :3465
                    return part_found(mc);
                } else if (c == '\r') {
                    // :3444
                    return false;
                } else if (!is_graph(c)) {
                    // :3451
                    if (part_not_found(mc)) {
                        // :3456
                        break;
                    } else {
                        // :3454
                        return false;
                    }
                }
            }
        }
    }
}

// :3451
// Returns true if the line should continue being compiled.
static bool part_not_found(struct mc *mc) {
    for (;;) {
        char c = *mc->si++;
        if (c == '\r') return false;
        if (c == '|') return true;
    }
}

// :3462
// Returns true if the line should continue being compiled.
static bool part_found(struct mc *mc) {
    for (;;) {
        char c = *mc->si++;
        if (c == '\r') return false;
        if (!is_graph(c)) return true;
    }
}

// :3662
static void adp_set(struct mc *mc, char cmd) {
    (void)cmd;
    *mc->di++ = 0xc0;
    *mc->di++ = 0xf7;
    *mc->di++ = getnum(mc);
    olc0(mc);
}

// :3673
static void tl_set(struct mc *mc, char cmd) {
    (void)cmd;
    if (mc->part == rhythm) error(mc, 'O', 17);
    *mc->di++ = 0xb8;
    uint8_t n = getnum(mc);
    if (n >= 16) error(mc, 'O', 6);
    *mc->di++ = n;
    if (*mc->si++ != ',') error(mc, 'O', 6);
    if (*mc->si == '+' || *mc->si == '-') {
        mc->di[-1] |= 0xf0;
    }
    *mc->di++ = getnum(mc);
    olc0(mc);
}

// :3704
static void partmask_set(struct mc *mc, char cmd) {
    (void)cmd;
    uint16_t n;
    if (lngset(mc, &n) != 0) error(mc, 'm', 6);
    if (n >= 2) error(mc, 'm', 2);
    parset(mc, 0xc0, n);
}

// :3718
static void slotmask_set(struct mc *mc, char cmd) {
    (void)cmd;
    if (*mc->si == 'd') {
        slotdetune_set(mc);
        return;
    } else if (*mc->si == 'k') {
        slotkeyondelay_set(mc);
        return;
    }
    uint16_t n1;
    lngset(mc, &n1);
    *mc->di++ = 0xcf;
    uint16_t n2 = 0;
    if (*mc->si == ',') {
        mc->si++;
        lngset(mc, &n2);
    }
    uint8_t mask = ((n1 << 4) & 0xf0) | (n2 & 0x0f);
    *mc->di++ = mask;
    olc0(mc);
}

// :3746
static void slotdetune_set(struct mc *mc) {
    uint8_t cmd = 0xc8;
    mc->si++;
    if (*mc->si == 'd') {
        cmd--;
        mc->si++;
    }
    *mc->di++ = cmd;
    *mc->di++ = getnum(mc);
    if (*mc->si != ',') error(mc, 's', 6);
    mc->si++;
    stosw(mc, getnum(mc));
    olc0(mc);
}

// :3770
static void slotkeyondelay_set(struct mc *mc) {
    mc->si++;
    *mc->di++ = 0xb5;
    uint8_t slot = getnum(mc);
    *mc->di++ = slot;
    if (*mc->si != ',') {
        // :3786
        if (slot != 0) error(mc, 's', 6);
        // :3783
        *mc->di++ = slot;
        return;
    }
    // :3780
    mc->si++;
    *mc->di++ = get_clock(mc, 's');
}

// :3794
static void ssg_efct_set(struct mc *mc, char cmd) {
    (void)cmd;
    uint16_t n;
    lngset(mc, &n);
    if (mc->skip_flag != 0) return;
    *mc->di++ = 0xd4;
    *mc->di++ = n;
    olc0(mc);
}

// :3806
static void fm_efct_set(struct mc *mc, char cmd) {
    (void)cmd;
    uint16_t n;
    lngset(mc, &n);
    if (mc->skip_flag != 0) return;
    *mc->di++ = 0xd3;
    *mc->di++ = n;
    olc0(mc);
}

// :3818
static void fade_set(struct mc *mc, char cmd) {
    (void)cmd;
    if (*mc->si == 'B') {
        fb_set(mc);
        return;
    }
    uint16_t n;
    lngset(mc, &n);
    *mc->di++ = 0xd2;
    *mc->di++ = n;
    olc0(mc);
}

// :3830
static void fb_set(struct mc *mc) {
    mc->si++;
    *mc->di++ = 0xb6;
    if (*mc->si == '+' || *mc->si == '-') {
        // :3844
        uint8_t val = getnum(mc);
        if ((uint8_t)(val + 7) >= 15) error(mc, 'F', 2);
        *mc->di++ = 0x80 | val;
    } else {
        // :3837
        uint8_t val = getnum(mc);
        if (val >= 8) error(mc, 'F', 2);
        *mc->di++ = val;
    }
    olc0(mc);
}

// :3858
static void porta_start(struct mc *mc, char cmd) {
    (void)cmd;
    if (mc->skip_flag != 0) return;
    if (mc->porta_flag != 0) error(mc, '{', 9);
    *mc->di++ = 0xda;
    mc->porta_flag = 1;
    // :3869
    mc->bunsan_start = NULL;
    if (*mc->si == '{') {
        mc->si++;
        mc->bunsan_start = mc->di;
    }
    olc0(mc);
}

// :3880
static void porta_end(struct mc *mc, char cmd) {
    (void)cmd;
    if (*mc->si == '}') {
        bunsan_end(mc);
        return;
    }

    // :3884
    if (mc->skip_flag != 0) {
        // :3940
        uint16_t n;
        lngset2(mc, &n);
        if (n >= 256) error(mc, '}', 8);
        futen_skip(mc);
        if (*mc->si != ',') return;
        mc->si++;
        if (lngset2(mc, &n) != 0) error(mc, '}', 6);
        futen_skip(mc);
        return;
    }

    // :3886
    if (mc->porta_flag != 1) error(mc, '}', 13);
    if (mc->di[-5] != 0xda) error(mc, '}', 14);
    if (mc->di[-4] == 0x0f || mc->di[-2] == 0x0f) error(mc, '}', 15);
    mc->di[-3] = mc->di[-2];
    mc->di -= 2;
    mc->porta_flag = 0;
    uint16_t n;
    lngset2(mc, &n);
    if (n >= 256) error(mc, '}', 8);
    lngcal(mc);
    uint8_t len;
    if (futen(mc, &len) != 0) error(mc, '}', 8);
    *mc->di++ = len;

    // :3912
    if (*mc->si != ',') {
        // :3936
        mc->prsok = 9;
        olc02(mc);
        return;
    }

    // :3916
    mc->si++;
    if (lngset2(mc, &n) != 0) error(mc, '}', 6);
    lngcal(mc);
    futen(mc, &len);
    if (len >= mc->di[-1]) error(mc, '}', 8);

    // :3926
    write16(&mc->di[1], read16(&mc->di[-2]));
    write16(&mc->di[-1], read16(&mc->di[-4]));
    mc->di[-4] = mc->di[-3];
    mc->di[-3] = len;
    mc->di[-2] = 0xfb;
    mc->di[2] -= len;
    mc->di += 3;

    // :3936
    mc->prsok = 9;
    olc02(mc);
}

// :3961
static void bunsan_end(struct mc *mc) {
    mc->si++;
    if (mc->skip_flag != 0) {
        // :4183
        uint16_t n;
        lngset2(mc, &n);
        futen_skip(mc);
        if (*mc->si != ',') return;
        // :4188
        mc->si++;
        lngset2(mc, &n);
        futen_skip(mc);
        if (*mc->si != ',') return;
        // :4193
        mc->si++;
        lngset2(mc, &n);
        if (*mc->si != ',') return;
        // :4197
        mc->si++;
        lngset2(mc, &n);
        if (*mc->si != ',') return;
        // :4201
        mc->si++;
        getnum(mc);
        return;
    }

    // :3967
    uint8_t *bunsan = mc->bunsan_start;
    if (!bunsan) error(mc, '}', 34);
    uint16_t bunsan_len = mc->di - bunsan;
    if (bunsan_len == 0) bunsan_error(mc);
    if ((bunsan_len & 1) != 0) bunsan_error(mc);
    bunsan_len >>= 1;
    if (bunsan_len >= 17) bunsan_error(mc);
    mc->bunsan_count = bunsan_len;

    // :3981
    uint8_t *work = mc->bunsan_work;
    // :3982
    for (; bunsan != mc->di; bunsan += 2) {
        uint8_t cmd = *bunsan;
        if ((cmd & 0x80) != 0) bunsan_error(mc);
        *work++ = cmd;
    }

    // :3994
    uint16_t len16;
    lngset2(mc, &len16);
    if (len16 >= 256) bunsan_error(mc);
    lngcal(mc);
    uint8_t len;
    if (futen(mc, &len) != 0) bunsan_error(mc);
    mc->bunsan_length = len;

    // :4002
    mc->bunsan_vol = mc->bunsan_gate = 0;
    mc->bunsan_1cnt = mc->bunsan_tieflag = 1;

    // :4009
    if (*mc->si != ',') {
        bunsan_main(mc);
        return;
    }
    mc->si++;
    if (lngset2(mc, &len16) == 0) {
        // :4014
        if (len16 >= 256) bunsan_error(mc);
        lngcal(mc);
        if (futen(mc, &len) != 0) bunsan_error(mc);
        mc->bunsan_1cnt = len;
    }

    // :4020
    if (*mc->si != ',') {
        bunsan_main(mc);
        return;
    }
    mc->si++;
    if (lngset2(mc, &len16) == 0) {
        // :4026
        if (len16 >= 2) bunsan_error(mc);
        mc->bunsan_tieflag = len16;
    }

    // :4031
    if (*mc->si != ',') {
        bunsan_main(mc);
        return;
    }
    mc->si++;
    if (lngset2(mc, &len16) == 0) {
        // :4037
        if (len16 >= mc->bunsan_length) bunsan_error(mc);
        mc->bunsan_gate = len16;
        mc->bunsan_length -= len16;
    }

    // :4043
    if (*mc->si != ',') {
        bunsan_main(mc);
        return;
    }
    mc->si++;
    mc->bunsan_vol = getnum(mc);
    bunsan_main(mc);
}

// :4052
static void bunsan_main(struct mc *mc) {
    mc->di = mc->bunsan_start - 1;

    // :4057
    uint16_t bunsan_1loop = mc->bunsan_1cnt * mc->bunsan_count;
    if (bunsan_1loop >= 256) bunsan_error(mc);
    mc->bunsan_1loop = bunsan_1loop;

    // :4062
    if (mc->bunsan_1loop >= mc->bunsan_length) {
        bunsan_last(mc);
        return;
    }
    // :4067
    uint8_t n_loops = mc->bunsan_length / mc->bunsan_1loop;
    if (mc->bunsan_tieflag != 0 && mc->bunsan_length % mc->bunsan_1loop == 0) n_loops--;

    // :4074
    if (n_loops == 1) {
        // :4103
        bunsan_set1loop(mc);
        mc->bunsan_length -= mc->bunsan_1loop;
        bunsan_last(mc);
        return;
    }

    // :4078
    *mc->di++ = 0xf9;
    uint8_t *loop_end = mc->di;
    mc->di += 2;
    bunsan_set1loop(mc);
    *mc->di++ = 0xf8;
    *mc->di++ = n_loops;
    *mc->di++ = n_loops;
    // :4092
    mc->bunsan_length -= n_loops * mc->bunsan_1loop;
    // :4094
    stosw(mc, loop_end - m_buf(mc));
    write16(loop_end, mc->di - 4 - m_buf(mc));
    bunsan_last(mc);
}

// :4109
static void bunsan_last(struct mc *mc) {
    if (mc->bunsan_length == 0) {
        bunsan_exit(mc);
        return;
    }
    // :4112
    uint8_t *bunsan_work = mc->bunsan_work;
    for (;;) {
        *mc->di++ = *bunsan_work++;
        if (mc->bunsan_1cnt >= mc->bunsan_length) {
            // :4129
            *mc->di++ = mc->bunsan_length;
            bunsan_exit(mc);
            return;
        }
        // :4121
        *mc->di++ = mc->bunsan_1cnt;
        mc->bunsan_length -= mc->bunsan_1cnt;
        if (mc->bunsan_tieflag == 1) *mc->di++ = 0xfb;
    }
}

// :4134
static void bunsan_exit(struct mc *mc) {
    if (mc->bunsan_gate != 0) {
        // :4138
        *mc->di++ = 0x0f;
        *mc->di++ = mc->bunsan_gate;
    }
    // :4142
    mc->bunsan_start = NULL;
    mc->porta_flag = 0;
    olc0(mc);
}

// :4149
static void bunsan_set1loop(struct mc *mc) {
    uint8_t *work = mc->bunsan_work;
    // :4151
    for (int i = 0; i < mc->bunsan_count; i++) {
        *mc->di++ = *work++;
        *mc->di++ = mc->bunsan_1cnt;
        if (mc->bunsan_tieflag == 1) *mc->di++ = 0xfb;
    }
    // :4165
    uint8_t vol = mc->bunsan_vol;
    if (vol == 0) return;
    uint8_t vol_cmd = 0xe3;
    if ((vol & 0x80) != 0) {
        vol_cmd--;
        vol = -vol;
    }
    *mc->di++ = vol_cmd;
    *mc->di++ = vol;
}

// :4179
PMDC_NORETURN static void bunsan_error(struct mc *mc) {
    error(mc, '}', 35);
}

// :4210
static void status_write(struct mc *mc, char cmd) {
    (void)cmd;
    if (*mc->si == '-' || *mc->si == '+') {
        // :4219
        parset(mc, 0xdb, getnum(mc));
    } else {
        // :4215
        uint16_t n;
        lngset(mc, &n);
        parset(mc, 0xdc, n);
    }
}

// :4228
static void giji_echo_set(struct mc *mc, char cmd) {
    (void)cmd;
    mc->ge_delay = get_clock(mc, 'W');
    if (mc->ge_delay == 0) {
        // :4255
        mc->ge_depth = mc->ge_depth2 = 0;
        // :4259
        if (*mc->si != ',') return;
        mc->si++;
        getnum(mc);
        // :4263
        if (*mc->si != ',') return;
        mc->si++;
        getnum(mc);
        return;
    }

    // :4234
    mc->ge_tie = mc->ge_dep_flag = 0;
    mc->ge_depth = mc->ge_depth2 = -1;
    // :4238
    if (*mc->si != ',') return;
    mc->si++;
    if (*mc->si == '%') {
        mc->si++;
        mc->ge_dep_flag = 1;
    }
    // :4245
    mc->ge_depth = mc->ge_depth2 = getnum(mc);
    // :4249
    if (*mc->si != ',') return;
    mc->si++;
    uint16_t len;
    lngset(mc, &len);
    mc->ge_tie = len;
}

// :4273
static void sousyoku_onp_set(struct mc *mc, char cmd) {
    (void)cmd;
    if (*mc->si == 'E') {
        ssgeg_set(mc);
        return;
    }

    // :4276
    mc->ss_speed = get_clock(mc, 'S');
    if (mc->ss_speed == 0) {
        // :4308
        mc->ss_depth = mc->ss_length = 0;
        // :4312
        if (*mc->si != ',') return;
        mc->si++;
        uint16_t dummy;
        lngset(mc, &dummy);
        return;
    }

    // :4281
    mc->ss_tie = 1;
    mc->ss_depth = -1;
    // :4283
    if (*mc->si == ',') {
        mc->si++;
        mc->ss_depth = getnum(mc);
        // :4288
        if (*mc->si == ',') {
            mc->si++;
            uint16_t tie;
            lngset(mc, &tie);
            mc->ss_tie = tie;
        }
    }

    // :4293
    int8_t length = mc->ss_depth;
    if (length < 0) length = -length;
    // :4298
    if (mc->ss_speed != 1) {
        if (length * mc->ss_speed >= 256) error(mc, 'S', 2);
        length *= mc->ss_speed;
    }
    mc->ss_length = length;
}

// :4322
static void ssgeg_set(struct mc *mc) {
#if !efc
    if (mc->ongen >= psg) error(mc, 'S', 17);
#endif
    if (mc->opl_flg != 0 || mc->x68_flg != 0) error(mc, 'S', 17);
    mc->si++;

    // :4333
    uint16_t slot16;
    if (lngset2(mc, &slot16) != 0) error(mc, 'S', 6);
    if (*mc->si != ',') error(mc, 'S', 6);
    uint8_t slot = slot16;
    if (slot == 0 || slot >= 16) error(mc, 'S', 2);

    // :4344
    mc->si++;
    uint16_t num16;
    if (lngset2(mc, &num16) != 0) error(mc, 'S', 6);
    uint8_t num = num16;
    if (num >= 16) error(mc, 'S', 2);

    // :4354
    uint8_t ssgeg_reg = mc->part - 1;
    if (ssgeg_reg >= 6) ssgeg_reg = 2;
    if (ssgeg_reg >= 3) ssgeg_reg -= 3;
    ssgeg_reg += 0x90;

    // :4366
    if ((slot & 1) != 0) sss_set1slot(mc, ssgeg_reg, num);
    // :4369
    ssgeg_reg += 8;
    if ((slot & 2) != 0) sss_set1slot(mc, ssgeg_reg, num);
    // :4374
    ssgeg_reg -= 4;
    if ((slot & 4) != 0) sss_set1slot(mc, ssgeg_reg, num);
    // :4379
    ssgeg_reg += 8;
    if ((slot & 8) != 0) sss_set1slot(mc, ssgeg_reg, num);

    // :4384
    olc0(mc);
}

// :4387
static void sss_set1slot(struct mc *mc, uint8_t ssgeg_reg, uint8_t num) {
    *mc->di++ = 0xef;
    *mc->di++ = ssgeg_reg;
    *mc->di++ = num;
}

// :4398
static void syousetu_lng_set(struct mc *mc, char cmd) {
    (void)cmd;
    uint16_t len;
    lngset(mc, &len);
    parset(mc, 0xdf, len);
}

// :4409
static void hardlfo_set(struct mc *mc, char cmd) {
    (void)cmd;
#if efc
    error(mc, 'H', 11);
#else
    uint16_t pms;
    lngset(mc, &pms);
    if (pms >= 8) error(mc, 'H', 2);
    uint16_t ams = 0;
    if (*mc->si == ',') {
        // :4420
        mc->si++;
        lngset(mc, &ams);
        if (ams >= 4) error(mc, 'H', 2);
    }
    // :4431
    *mc->di++ = 0xe1;
    *mc->di++ = ((ams << 4) | pms) & 0x37;
    // :4443
    if (*mc->si == ',') {
        mc->si++;
        hdelay_set2(mc, 'H');
        return;
    }
    olc0(mc);
#endif
}

// :4460
static void hardlfo_onoff(struct mc *mc, char cmd) {
    (void)cmd;
#if efc
    error(mc, '#', 11);
#else
    switch (*mc->si++) {
    case 'D':
        hdelay_set(mc);
        return;
    case 'f':
        opm_hf_set(mc);
        return;
    case 'w':
        opm_wf_set(mc);
        return;
    case 'p':
        opm_pmd_set(mc);
        return;
    case 'a':
        opm_amd_set(mc);
        return;
    case '#':
        opm_all_set(mc);
        return;
    }
    // :4479
    mc->si--;
    uint16_t on;
    lngset(mc, &on);
    if (on == 0) {
        // :4483
        if (*mc->si == ',') {
            mc->si++;
            uint16_t dummy;
            lngset(mc, &dummy);
        }
        parset(mc, 0xe0, 0x00);
        return;
    }
    // :4490
    if (*mc->si != ',') error(mc, '#', 6);
    mc->si++;
    uint16_t depth;
    lngset(mc, &depth);
    if (depth >= 8) error(mc, '#', 2);
    parset(mc, 0xe0, depth | 0x08);
#endif
}

// :4504
static void hdelay_set(struct mc *mc) {
    hdelay_set2(mc, '#');
}

// :4506
static void hdelay_set2(struct mc *mc, char cmd) {
    parset(mc, 0xe4, get_clock(mc, cmd));
}

// :4512
static void opm_hf_set(struct mc *mc) {
    hf_set(mc);
    olc0(mc);
}

// :4516
static void opm_wf_set(struct mc *mc) {
    wf_set(mc);
    olc0(mc);
}

// :4520
static void opm_pmd_set(struct mc *mc) {
    pmd_set(mc);
    olc0(mc);
}

// :4524
static void opm_amd_set(struct mc *mc) {
    amd_set(mc);
    olc0(mc);
}

// :4528
static void opm_all_set(struct mc *mc) {
    hf_set(mc);
    if (*mc->si++ != ',') error(mc, '#', 6);
    wf_set(mc);
    if (*mc->si++ != ',') error(mc, '#', 6);
    pmd_set(mc);
    if (*mc->si++ != ',') error(mc, '#', 6);
    amd_set(mc);
    olc0(mc);
}

// :4545
static void hf_set(struct mc *mc) {
    *mc->di++ = 0xd7;
    *mc->di++ = getnum(mc);
}

// :4553
static void wf_set(struct mc *mc) {
    uint16_t wf;
    if (lngset(mc, &wf) != 0) error(mc, '#', 2);
    *mc->di++ = 0xd9;
    *mc->di++ = wf;
}

// :4563
static void pmd_set(struct mc *mc) {
    *mc->di++ = 0xd8;
    *mc->di++ = getnum(mc) | 0x80;
}

// :4572
static void amd_set(struct mc *mc) {
    *mc->di++ = 0xd8;
    *mc->di++ = getnum(mc) & 0x7f;
}

// :4585
static void octrev(struct mc *mc, char cmd) {
    (void)cmd;
    char old_up = *ou00(mc);
    *ou00(mc) = *od00(mc);
    *od00(mc) = old_up;
}

// :4593
static void lngmul(struct mc *mc, char cmd) {
    (void)cmd;
    uint16_t mul;
    lngset(mc, &mul);
    if (mul == 0) error(mc, '^', 2);
    // :4598
    if (mc->skip_flag != 0) return;
    // :4601
    if ((mc->prsok & 1) == 0) error(mc, '^', 16);
    if ((mc->prsok & 2) != 0) error(mc, '^', 31);
    // :4608
    if (--mul == 0) return;

    // :4610
    uint8_t loops = mul;
    uint8_t len_old = mc->di[-1];
    uint8_t len_new = len_old;
    for (int i = 0; i < loops; i++) {
        bool overflow = len_new + len_old >= 256;
        len_new += len_old;
        if (overflow) lm_over(mc, &len_new);
    }
    mc->di[-1] = len_new;
}

// :4624
static void lm_over(struct mc *mc, uint8_t *len_new) {
    if ((mc->prsok & 8) != 0) error(mc, '^', 8);
    *len_new += 1;
    mc->di[-1] = 0xff;
    if (mc->part == rhythm) {
        // :4639
        *mc->di++ = 0x0f;
        *mc->di++ = 0x00;
    } else {
        // :4632
        *mc->di = 0xfb;
        mc->di[1] = mc->di[-2];
        mc->di[2] = 0;
        mc->di += 3;
    }
    mc->prsok |= 2;
}

// :4650
static void lngrew(struct mc *mc, char cmd) {
    if (mc->skip_flag != 0) {
        lng_skip_ret(mc);
        return;
    }
    // :4653
    if ((mc->prsok & 1) == 0) error(mc, cmd, 16);
    if ((mc->prsok & 2) != 0) error(mc, cmd, 31);

    // :4660
    mc->di--;
    if (*mc->si == '.') {
        // :4663
        mc->leng = *mc->di;
    } else {
        // :4666
        uint16_t len_base;
        lngset2(mc, &len_base);
        if (len_base >= 256) error(mc, '=', 8);
        lngcal(mc);
    }
    // :4672
    uint8_t len;
    int futen_status = futen(mc, &len);
    *mc->di++ = len;
    if (futen_status == 0) return;
    if ((mc->prsok & 8) == 0) return;
    error(mc, '^', 8);
}

// :4681
static void lngrew_2(struct mc *mc, char cmd) {
    mc->si--;
    lngrew(mc, cmd);
}

// :4688
static void lng_dec(struct mc *mc, char cmd) {
    (void)cmd;
    if (mc->skip_flag != 0) {
        lng_skip_ret(mc);
        return;
    }
    // :4692
    if ((mc->prsok & 1) == 0) error(mc, '-', 16);

    // :4696
    uint16_t len_base;
    lngset2(mc, &len_base);
    if (len_base >= 256) error(mc, '-', 8);
    lngcal(mc);
    uint8_t len;
    if (futen(mc, &len) != 0) error(mc, '-', 8);

    // :4705
    if (mc->di[-1] <= len) error(mc, '-', 8);
    mc->di[-1] -= len;
}

// :4711
static void lng_skip_ret(struct mc *mc) {
    uint16_t len;
    lngset2(mc, &len);
    // BUG: this gives the wrong error message for '='
    if (len >= 256) error(mc, '-', 8);
    futen_skip(mc);
}

// :4722
static void otoc(struct mc *mc, char cmd) {
    otoset(mc, cmd, 0, mc->def_c);
}

// :4725
static void otod(struct mc *mc, char cmd) {
    otoset(mc, cmd, 2, mc->def_d);
}

// :4728
static void otoe(struct mc *mc, char cmd) {
    otoset(mc, cmd, 4, mc->def_e);
}

// :4731
static void otof(struct mc *mc, char cmd) {
    otoset(mc, cmd, 5, mc->def_f);
}

// :4734
static void otog(struct mc *mc, char cmd) {
    otoset(mc, cmd, 7, mc->def_g);
}

// :4737
static void otoa(struct mc *mc, char cmd) {
    otoset(mc, cmd, 9, mc->def_a);
}

// :4740
static void otob(struct mc *mc, char cmd) {
    otoset(mc, cmd, 0x0b, mc->def_b);
}

// :4743
static void otox(struct mc *mc, char cmd) {
    otoset(mc, cmd, 0x0c, 0);
}

// :4745
static void otor(struct mc *mc, char cmd) {
    (void)cmd;
    // :4952
#if !efc
    if (mc->towns_flg != 1 && mc->part == rhythm2) error(mc, 'r', 17);
#endif
    otoset_x(mc, 0x0f);
}

// :4748
static void otoset(struct mc *mc, char cmd, uint8_t note, int8_t shift) {
#if !efc
    if (mc->towns_flg != 1 && mc->part == rhythm2) error(mc, cmd, 17);
    // :4755
    if (mc->part == rhythm) {
        if (mc->lastprg == 0) error(mc, cmd, 30);
        if (mc->skip_flag == 0) {
            // :4767
            *mc->di++ = mc->lastprg >> 8;
            *mc->di++ = mc->lastprg;
            mc->length_check1 = mc->length_check2 = 1;
            mc->prsok = 0;
        }
        // :4766, :4774
        bp9(mc);
        return;
    }
#endif
    // :4779
    if (note == 0x0c) {
        otoset_x(mc, note);
        return;
    }

    // :4783
    uint8_t octave = mc->octave & 0x0f;
    // :4786
    if (*mc->si == '=') {
        // :4792
        mc->si++;
    } else if (shift != 0) {
        // :4790
        note += shift;
        goto bp3;
    }
    // :4794
    do {
        if (*mc->si == '+') {
            note++;
            mc->si++;
        }
        // :4800
        if (*mc->si == '-') {
            note--;
            mc->si++;
        }
    bp3:
        // :4809
        note &= 0x0f;
        if (note == 0x0f) {
            // :4813
            octave--;
            if (octave == 0xff) error(mc, cmd, 26);
            note = 0x0b;
        }
        // :4818
        if (note == 0x0c) {
            // :4821
            octave++;
            if (octave == 8) error(mc, cmd, 26);
            note = 0;
        }
        // :4826
    } while (*mc->si == '+' || *mc->si == '-');

    // :4836
    uint8_t onkai = (octave << 4) | note;

    // :4842
    ots002(mc, onkai);
    // :4968
    bp8(mc, onkai);
}

// :4842
static void ots002(struct mc *mc, uint8_t onkai) {
    if (mc->bend == 0) return;

    // :4847
    uint16_t detune = 0;
    if (mc->pitch != 0) {
#if !efc
        if (mc->ongen >= psg) {
            // :4857
            detune = mc->pitch >> 7;
            if ((detune & 0x8000) != 0) detune++;
        } else
#endif
        detune = fmpt(mc, onkai);
    }
    // :4919
    detune += mc->detune + mc->master_detune;
    if (detune != mc->alldet) {
        if (mc->porta_flag == 1) {
            porta_pitchset(mc, detune);
        } else {
            // :4926
            *mc->di++ = 0xfa;
            stosw(mc, detune);
        }
        mc->alldet = detune;
    }
}

// :4871
static uint16_t fmpt(struct mc *mc, uint8_t onkai) {
    // Note: in the original, the address into fnumdat_seg is byte-oriented,
    // while in this version, it is indexed. Hence, all address calculations in
    // this version need to be divided by 2.
    uint16_t fnum_adr = (onkai & 0x0f) << 4;
    // :4880
    int16_t pitch_shift = 32 * mc->bend * mc->pitch / 8192;
    // :4892
    uint16_t fnum = fnumdat_seg[fnum_adr];
    fnum_adr += pitch_shift;
    // :4901
    for (;;) {
        if ((fnum_adr & 0x8000) != 0) {
            // :4903
            fnum_adr += 32 * 12;
            fnum += 0x026a;
        } else if (fnum_adr >= 32 * 12) {
            // :4908
            fnum_adr -= 32 * 12;
            fnum -= 0x026a;
        } else {
            break;
        }
    }
    // :4912
    fnum -= fnumdat_seg[fnum_adr];
    return -fnum;
}

// :4935
static void porta_pitchset(struct mc *mc, uint16_t detune) {
    if (mc->di[-1] != 0xda) error(mc, 0, 14);
    mc->di[-1] = 0xfa;
    stosw(mc, detune);
    *mc->di++ = 0xda;
}

// :4961
static void otoset_x(struct mc *mc, uint8_t note) {
    bp8(mc, note);
}

// :4968
static void bp8(struct mc *mc, uint8_t onkai) {
    if (mc->skip_flag != 0) {
        // :4970
        if (mc->acc_adr == mc->di) {
            mc->di -= 2;
            mc->acc_adr = NULL;
        }
        // :4974
        uint16_t len;
        lngset2(mc, &len);
        if (len >= 256) error(mc, 0, 8);
        futen_skip(mc);
        // :5029
        mc->tie_flag = 0;
        olc02(mc);
        return;
    }

    // :4982
    mc->length_check1 = mc->length_check2 = 1;
    // CHECK: does x get translated to a tone of 0x0c by the real mc?
    *mc->di++ = onkai;

    // :4991
    bp9(mc);
}

// :4991
static void bp9(struct mc *mc) {
    uint16_t len_base;
    lngset2(mc, &len_base);
    if (len_base >= 256) error(mc, 0, 8);
    lngcal(mc);
    mc->prsok &= 0xfd;
    uint8_t len;
    futen(mc, &len);
    press(mc, len);

    // :5003
    *mc->di++ = mc->leng;
    mc->prsok |= 1;
    mc->prsok &= 0xf3;

    // :5008
    if ((mc->di[-2] & 0x0f) != 0x0f && mc->tie_flag == 0 && mc->porta_flag == 0) {
        mc->ge_flag1 = mc->ge_flag2 = 0;
        if (mc->ge_delay != 0) {
            // :5023
            ge_set(mc);
        } else if (mc->ss_length != 0) {
            // :5028
            ss_set(mc);
        }
    }
    // :5029
    mc->tie_flag = 0;

    // :5032
    olc02(mc);
}

// :5037
static void ge_set(struct mc *mc) {
    mc->ge_depth = mc->ge_depth2;
    // :5040
    for (;;) {
        if (mc->di[-1] <= mc->ge_delay) break;
        uint8_t rest_len = mc->di[-1] - mc->ge_delay;
        uint8_t onkai = mc->di[-2];
        mc->di[-1] = mc->ge_delay;
        // :5048
        if (mc->ss_length != 0 && mc->ss_length < mc->di[-1]) {
            // :5055
            if (mc->ge_flag1 != 0 && mc->ge_flag1 == mc->di[-4] && mc->ge_flag2 == mc->di[-3]) {
                // :5063
                write16(&mc->di[-4], read16(&mc->di[-2]));
                mc->di -= 2;
            }
            // :5068
            ss_set(mc);
        }
        // :5072
        if ((mc->ge_tie & 1) != 0) *mc->di++ = 0xfb;
        // :5078
        ge_set_vol(mc);
        // :5081
        *mc->di++ = onkai;
        *mc->di++ = rest_len;
        mc->prsok |= 2;
        // :5086
        if ((mc->ge_tie & 2) != 0) break;
    }
    // :5090
    if (mc->ss_length != 0) ss_set(mc);
}

// :5096
static void ge_set_vol(struct mc *mc) {
    int8_t vol = mc->ge_depth;
    if (vol == 0) return;
    if (vol > 0) {
        // :5105
        if (mc->ge_dep_flag != 1) vol = ongen_sel_vol(mc, vol);
        // :5108
        if (vol != 0) {
            // :5111
            *mc->di++ = mc->ge_flag1 = 0xde;
            *mc->di++ = mc->ge_flag2 = vol;
        }
        // :5116
        int8_t new_depth = mc->ge_depth + mc->ge_depth2;
        if (mc->ge_dep_flag != 1) {
            // :5121
            if (new_depth >= 16) new_depth = 15;
            mc->ge_depth = new_depth;
        } else {
            // :5127
            if (new_depth < 0) new_depth = 127;
            mc->ge_depth = new_depth;
        }
    } else {
        // :5136
        vol = -vol;
        if (mc->ge_dep_flag != 1) vol = ongen_sel_vol(mc, vol);
        // :5141
        if (vol != 0) {
            // :5144
            *mc->di++ = mc->ge_flag1 = 0xdd;
            *mc->di++ = mc->ge_flag2 = vol;
        }
        // :5149
        int8_t new_depth = mc->ge_depth + mc->ge_depth2;
        if (mc->ge_dep_flag != 1) {
            // :5154
            if (new_depth < -15) new_depth = -15;
            mc->ge_depth = new_depth;
        } else {
            // :5160
            if (new_depth >= 0) new_depth = -127;
            mc->ge_depth = new_depth;
        }
    }
}

// :5169
static uint8_t ongen_sel_vol(struct mc *mc, uint8_t vol) {
#if !efc
    if (mc->part == pcmpart || mc->ongen == pcm_ex) return vol << 4;
    if (mc->towns_flg == 1 && mc->part == rhythm2) return vol << 4;
    if (mc->ongen == psg) return vol;
#else
    (void)mc;
#endif
    return vol << 2;
}

// :5200
static void ss_set(struct mc *mc) {
    // CHECK: does this mean the S command doesn't work with 'x' notes?
    if (mc->ss_length >= mc->di[-1] || mc->di[-2] == 0x0c) return;
    mc->di -= 2;

    uint8_t base_onkai = mc->di[0];
    uint8_t base_len = mc->di[1];
    if (mc->ss_depth < 0) {
        // :5217
        uint8_t onkai = base_onkai;
        for (int i = 0; i < -mc->ss_depth; i++) onkai = one_down(mc, onkai);
        // :5225
        do {
            if (mc->ge_flag1 != 0) {
                *mc->di++ = mc->ge_flag1;
                *mc->di++ = mc->ge_flag2;
            }
            // :5232
            *mc->di++ = onkai;
            *mc->di++ = mc->ss_speed;
            // :5238
            if (mc->ss_tie != 0) *mc->di++ = 0xfb;
            // :5243
            onkai = one_up(mc, onkai);
        } while (onkai != base_onkai);
    } else {
        // :5253
        uint8_t onkai = base_onkai;
        for (int i = 0; i < mc->ss_depth; i++) onkai = one_up(mc, onkai);
        // :5261
        do {
            if (mc->ge_flag1 != 0) {
                *mc->di++ = mc->ge_flag1;
                *mc->di++ = mc->ge_flag2;
            }
            // :5268
            *mc->di++ = onkai;
            *mc->di++ = mc->ss_speed;
            // :5274
            if (mc->ss_tie != 0) *mc->di++ = 0xfb;
            // :5279
            onkai = one_down(mc, onkai);
        } while (onkai != base_onkai);
    }

    // :5288
    if (mc->ge_flag1 != 0) {
        *mc->di++ = mc->ge_flag1;
        *mc->di++ = mc->ge_flag2;
    }
    // :5295
    *mc->di++ = base_onkai;
    *mc->di++ = base_len - mc->ss_length;
    mc->prsok |= 2;
}

// :5309
static uint8_t one_down(struct mc *mc, uint8_t onkai) {
    onkai--;
    if ((onkai & 0x0f) != 0x0f) return onkai;
    // :5315
    onkai = (onkai & 0xf0) | 0x0b;
    if ((onkai & 0x80) != 0) error(mc, 'S', 26);
    return onkai;
}

// :5329
static uint8_t one_up(struct mc *mc, uint8_t onkai) {
    onkai++;
    if ((onkai & 0x0f) != 0x0c) return onkai;
    // :5335
    onkai = (onkai & 0xf0) + 0x10;
    if ((onkai & 0x80) != 0) error(mc, 'S', 26);
    return onkai;
}

// :5348
static void press(struct mc *mc, uint8_t len) {
    if ((mc->prsok & 0x80) != 0 || (mc->prsok & 8) != 0 || mc->skip_flag != 0) return;
    if ((mc->prsok & 4) == 0) {
        // :5356
        if (mc->di[-1] != 0x0f || (mc->prsok & 1) == 0) return;
        restprs(mc, len);
        return;
    }
    // :5363
#if !efc
    if (mc->part == rhythm) {
        prs3(mc, len);
        return;
    }
#endif
    // :5369
    uint8_t onkai = mc->di[-1];
    if (mc->di[-4] != onkai) return;
    // :5372
    mc->di -= 3;
    mc->prsok |= 2;
    bool overflow = len + *mc->di >= 256;
    len += *mc->di;
    if (!overflow) {
        // :5383
        mc->leng = len;
        return;
    }
    // :5376
    *mc->di = 255;
    len++;
    mc->di += 3;
    if (onkai == 0x0f) {
        // :5381
        mc->di--;
        mc->di[-1] = 0x0f;
    }
    // :5383
    mc->leng = len;
}

// :5386
static void restprs(struct mc *mc, uint8_t len) {
#if !efc
    if (mc->part == rhythm) {
        prs3(mc, len);
        return;
    }
#endif
    if (mc->di[-3] == 0x0f) prs3(mc, len);
}

// :5394
static void prs3(struct mc *mc, uint8_t len) {
    mc->di -= 2;
    mc->prsok |= 2;
    bool overflow = len + *mc->di >= 256;
    len += *mc->di;
    if (!overflow) {
        // :5383
        mc->leng = len;
        return;
    }
    // :5399
    *mc->di = 255;
    len++;
    mc->di += 2;
    // :5383
    mc->leng = len;
}

// :5409
static int lngset(struct mc *mc, uint16_t *ret) {
    if (lngset2(mc, ret) != 0) {
        // :5449
        mc->leng = *ret = 1;
        return 1;
    }
    return 0;
}

// :5420
static int lngset2(struct mc *mc, uint16_t *ret) {
    while (*mc->si == ' ' || *mc->si == '\t') mc->si++;
    // :5426
    mc->calflg = 0;
    if (*mc->si == '%') {
        mc->si++;
        mc->calflg = 1;
    }
    // :5432
    if (*mc->si == '$') {
        mc->si++;
        // :5468
        uint8_t hex;
        if (hexget8(mc, &hex) != 0) {
            // :5440
            mc->leng = *ret = mc->deflng;
            mc->calflg = 1;
            return 1;
        } else {
            // :5469
            mc->leng = *ret = hex;
            return 0;
        }
    }

    // :5436
    uint8_t num;
    if (numget(mc, &num) != 0) {
        // :5440
        mc->leng = *ret = mc->deflng;
        mc->calflg = 1;
        return 1;
    }
    *ret = num;

    // :5445
    for (;;) {
        if (numget(mc, &num) != 0) {
            // :5448
            mc->leng = *ret;
            return 0;
        }
        // :5454
        *ret = 10 * *ret + num;
    }
}

// :5473
static int hexget8(struct mc *mc, uint8_t *ret) {
    uint8_t digit;
    if (hexcal8(*mc->si++, &digit) != 0) return 1;
    *ret = digit;
    if (hexcal8(*mc->si, &digit) == 0) {
        mc->si++;
        *ret = 16 * *ret + digit;
    }
    return 0;
}

// :5495
static int hexcal8(char c, uint8_t *ret) {
    c = to_upper(c);
    if (is_digit(c)) {
        *ret = c - '0';
        return 0;
    } else if (is_upper(c)) {
        *ret = c - 'A' + 10;
        return 0;
    } else {
        return 1;
    }
}

// :5520
static int futen(struct mc *mc, uint8_t *ret) {
    uint16_t len = mc->leng;
    uint8_t add_len = len;
    for (; *mc->si == '.'; mc->si++) {
        if ((add_len & 1) != 0) error(mc, '.', 21);
        len += add_len >>= 1;
    }
    // :5531
    if (len < 256) {
        mc->leng = *ret = len;
        return 0;
    }
    // :5537
    for (; len >= 256; len -= 255) {
        if (mc->ge_delay != 0 || mc->ss_length != 0) error(mc, '.', 20);
        // :5543
#if !efc
        if (mc->part == rhythm) {
            // :5562
            *mc->di++ = 255;
            *mc->di++ = 0x0f;
        } else
#endif
        {
            // :5547
            *mc->di++ = 255;
            *mc->di++ = 0xfb;
            uint8_t note = mc->di[-3];
            *mc->di++ = note;
        }
    }
    // :5556
    mc->leng = *ret = len;
    mc->prsok |= 2;
    return 1;
}

// :5567
static void futen_skip(struct mc *mc) {
    while (*mc->si == '.') mc->si++;
}

// :5580
static int numget(struct mc *mc, uint8_t *ret) {
    if (is_digit(*mc->si)) {
        *ret = *mc->si++ - '0';
        return 0;
    } else {
        return 1;
    }
}

// :5594
static void octset(struct mc *mc, char cmd) {
    uint8_t octave;
    if (*mc->si == '+' || *mc->si == '-') {
        // :5610
        mc->octss = getnum(mc);
        octave = mc->octave + mc->octss;
    } else {
        // :5599
        uint16_t octave16;
        if (lngset(mc, &octave16) != 0) error(mc, cmd, 6);
        octave = octave16 - 1 + mc->octss;
    }
    // :5604
    octs0(mc, cmd, octave);
}

// :5604
static void octs0(struct mc *mc, char cmd, uint8_t octave) {
    if (octave >= 8) error(mc, cmd, 26);
    mc->octave = octave;
}

// :5620
static void octup(struct mc *mc, char cmd) {
    octs0(mc, cmd, mc->octave + 1);
}

// :5624
static void octdown(struct mc *mc, char cmd) {
    octs0(mc, cmd, mc->octave - 1);
}

// :5635
static void lengthset(struct mc *mc, char cmd) {
    switch (*mc->si++) {
    case '=':
        lngrew(mc, cmd);
        return;
    case '-':
        lng_dec(mc, cmd);
        return;
    case '+':
        tieset_2(mc, cmd);
        return;
    case '^':
        lngmul(mc, cmd);
        return;
    }
    // :5645
    mc->si--;
    uint16_t len16;
    if (lngset2(mc, &len16) != 0) error(mc, 'l', 6);
    if (len16 >= 256) error(mc, 'l', 8);
    lngcal(mc);
    uint8_t len;
    if (futen(mc, &len) != 0) error(mc, 'l', 8);
    mc->deflng = len;
}

// :5662
static void zenlenset(struct mc *mc, char cmd) {
    (void)cmd;
    uint16_t len16;
    lngset(mc, &len16);
    uint8_t len = len16;
    mc->zenlen = len;
    mc->deflng = len >> 2;
    // :4400
    parset(mc, 0xdf, len);
}

// :5678
static uint8_t lngcal(struct mc *mc) {
    if (mc->leng == 0) error(mc, 0, 21);
    if (mc->calflg != 0) return mc->leng;
    // :5686
    if (mc->zenlen % mc->leng != 0) error(mc, 0, 21);
    return mc->leng = mc->zenlen / mc->leng;
}

// :5698
static void parset(struct mc *mc, uint8_t dh, uint8_t dl) {
    *mc->di++ = dh;
    *mc->di++ = dl;
    olc0(mc);
}

// :5707
static void tempoa(struct mc *mc, char cmd) {
    (void)cmd;
#if efc
    error(mc, 't', 12);
#else
    if (*mc->si == '+' || *mc->si == '-') {
        tempo_ss(mc, 0xfd);
        return;
    }
    // :5718
    uint16_t tempo;
    lngset(mc, &tempo);
    if (tempo < 18) error(mc, 't', 2);
    // :5723
#if !tempo_old_flag
    // :5724
    *mc->di++ = 0xfc;
    *mc->di++ = 0xff;
    *mc->di++ = tempo;
    olc0(mc);
#else
    // :5730
    tset(mc, timerb_get(tempo));
#endif
#endif
}

// :5740
static void tempob(struct mc *mc, char cmd) {
    (void)cmd;
#if efc
    error(mc, 'T', 12);
#else
    if (*mc->si == '+' || *mc->si == '-') {
        tempo_ss(mc, 0xfe);
        return;
    }
    // :5751
    uint16_t timerb;
    lngset(mc, &timerb);
    tset(mc, timerb);
#endif
}

// :5753
static void tset(struct mc *mc, uint8_t timerb) {
    if (timerb >= 251) error(mc, 't', 2);
    parset(mc, 0xfc, timerb);
}

// :5761
static void tempo_ss(struct mc *mc, uint8_t subcmd) {
    *mc->di++ = 0xfc;
    *mc->di++ = subcmd;
    uint8_t delta = getnum(mc);
    if (delta != 0) {
        // :5768
        *mc->di++ = delta;
        olc0(mc);
    } else {
        // :5770
        mc->di -= 2;
        olc02(mc);
    }
}

// :5784
static uint8_t timerb_get(uint8_t tempo) {
    uint8_t timerb = 256 - 0x112c / tempo;
    uint8_t rem = 0x112c % tempo;
    if ((rem & 0x80) != 0) timerb--;
    return timerb;
}

// :5802
static uint8_t get_clock(struct mc *mc, char cmd) {
    if (*mc->si != 'l') {
        uint16_t len;
        lngset(mc, &len);
        return len;
    }
    mc->si++;
    uint16_t len;
    lngset2(mc, &len);
    if (len >= 256) error(mc, cmd, 8);
    lngcal(mc);
    uint8_t clock;
    if (futen(mc, &clock) != 0) error(mc, cmd, 8);
    return clock;
}

// :5824
static void qset(struct mc *mc, char cmd) {
    (void)cmd;
    if (*mc->si == ',') {
        // :5856
        qsetb(mc);
        return;
    }
    // :5826
    uint8_t clock = get_clock(mc, 'q');
    *mc->di++ = 0xfe;
    *mc->di++ = clock;
    // :5833
    if (*mc->si == '-') {
        // :5836
        mc->si++;
        uint8_t clock2 = get_clock(mc, 'q');
        bool overflow = clock2 < clock;
        clock2 -= clock;
        if (overflow) clock2 = (uint8_t)-clock2 | 0x80;
        // :5847
        if (clock2 != 0) {
            *mc->di++ = 0xb1;
            *mc->di++ = clock2;
        }
    }
    // :5854
    if (*mc->si != ',') {
        olc0(mc);
        return;
    }
    qsetb(mc);
}

// :5856
static void qsetb(struct mc *mc) {
    mc->si++;
    parset(mc, 0xb3, get_clock(mc, 'q'));
}

// :5866
static void qset2(struct mc *mc, char cmd) {
    (void)cmd;
    if (*mc->si == '%') {
        // :5888
        mc->si++;
        uint16_t len;
        lngset(mc, &len);
        parset(mc, 0xc4, ~len);
        return;
    }
    // :5868
    uint16_t len;
    lngset(mc, &len);
    if (len >= 9) error(mc, 'Q', 2);
    if (len != 0) {
        len = 32 * len - 1;
    }
    // :5879
    parset(mc, 0xc4, ~len);
}

// :5899
static void vseta(struct mc *mc, char cmd) {
    (void)cmd;
    switch (*mc->si) {
    case '+':
    case '-':
        vss(mc);
        return;
    case ')':
        vss2(mc);
        return;
    case '(':
        vss2m(mc);
        return;
    }
    uint16_t vol;
    lngset(mc, &vol);
    vol += mc->volss2;
    if (vol >= 17) error(mc, 'v', 2);

    // :5914
#if !efc
    if (mc->part == pcmpart || mc->ongen == pcm_ex) {
        vsetm(mc, vol);
        return;
    }
    if (mc->towns_flg == 1 && mc->part == rhythm2) {
        vsetm(mc, vol);
        return;
    }
    if (mc->ongen >= psg) {
        vset(mc, vol);
        return;
    }
#endif
    // :5927
    vset(mc, fmvol[vol]);
}

// :5932
static void vset(struct mc *mc, uint8_t vol) {
    vol += mc->volss;
    if (vol >= 0x80) {
        // :5937
        if (mc->volss < 0) {
            // :5939
            vol = 0;
        }
#if !efc
        else if (mc->ongen >= psg) {
            // :5945
            vol = 15;
        }
#endif
        else {
            // :5948
            vol = 0x7f;
        }
    }
    // :5949
    mc->nowvol = vol;
    parset(mc, 0xfd, vol);
}

// :5956
static void vsetb(struct mc *mc, char cmd) {
    (void)cmd;
    uint16_t vol;
    lngset(mc, &vol);
#if !efc
    if (mc->part == pcmpart || mc->ongen == pcm_ex) {
        vsetm1(mc, vol);
        return;
    }
    if (mc->towns_flg == 1 && mc->part == rhythm2) {
        vsetm1(mc, vol);
        return;
    }
#endif
    vset(mc, vol);
}

// :5973
static void vsetm(struct mc *mc, uint8_t vol) {
    if (mc->pcm_vol_ext == 1) {
        // :5982
        vol = vol * vol < 256 ? vol * vol : 255;
    } else {
        // :5975
        vol = vol * 16 < 256 ? vol * 16 : 255;
    }
    // :5988
    vsetm1(mc, vol);
}

// :5988
static void vsetm1(struct mc *mc, uint8_t vol) {
    if (mc->volss < 0) {
        // :5993
        vol = vol + mc->volss >= 0 ? vol + mc->volss : 0;
    } else {
        // :5997
        vol = vol + mc->volss < 256 ? vol + mc->volss : 255;
    }
    // :6000 -> :5949
    mc->nowvol = vol;
    parset(mc, 0xfd, vol);
}

// :6007
static void vss(struct mc *mc) {
    uint8_t volss_raw = (uint8_t)getnum(mc);
    mc->volss = (int8_t)volss_raw;
#if !efc
    if (mc->part == pcmpart || mc->ongen == pcm_ex) {
        vsetm1(mc, volss_raw);
        return;
    }
    if (mc->towns_flg == 1 && mc->part == rhythm2) {
        vsetm1(mc, volss_raw);
        return;
    }
#endif
    vset(mc, mc->nowvol);
}

// :6025
static void vss2(struct mc *mc) {
    mc->si++;
    mc->volss2 = getnum(mc);
}

// :6030
static void vss2m(struct mc *mc) {
    mc->si++;
    mc->volss2 = -getnum(mc);
}

// :6039
static void neirochg(struct mc *mc, char cmd) {
    (void)cmd;
    uint8_t add_num = 0;
    if (*mc->si == '@') {
        // :6043
        mc->si++;
        add_num = 128;
    }
    // :6045
    uint16_t prg;
    lngset(mc, &prg);
    prg += add_num;
    if (mc->prg_flg != 0) set_prg(mc, prg);
    // :6052
#if !efc
    if (mc->part == rhythm) {
        rhyprg(mc, prg);
        return;
    }
    if (mc->ongen == psg) {
        psgprg(mc, prg);
        return;
    }
#endif
    // :6059
    // CHECK: what if prg is 255?
    if ((uint8_t)(prg + 1) > mc->maxprg) mc->maxprg = prg + 1;
    // :6066
#if efc
    // :6068
    parset(mc, 0xff, prg);
#else
    // :6072
    if (mc->part == pcmpart || mc->ongen == pcm_ex) {
        repeat_check(mc, prg);
        return;
    }
    if (mc->part != rhythm2) {
        parset(mc, 0xff, prg);
        return;
    }
    if (mc->towns_flg == 1) {
        repeat_check(mc, prg);
        return;
    }
    if (mc->skip_flag == 0) {
        // :6082
        *mc->di++ = prg;
        mc->length_check1 = mc->length_check2 = 1;
    }
    // :6086
    olc0(mc);
#endif
}

#if !efc

// :6088
static void rhyprg(struct mc *mc, uint16_t prg) {
    if (prg >= 0x4000) error(mc, '@', 2);
    mc->lastprg = prg | 0x8000;
}

// :6095
static void psgprg(struct mc *mc, uint8_t prg) {
    if (prg >= psgenvdat_max + 1) prg = 0;
    // :6099
    *mc->di++ = 0xf0;
    memcpy(mc->di, psgenvdat[prg], sizeof(psgenvdat[prg]));
    mc->di += sizeof(psgenvdat[prg]);
    olc0(mc);
}

#endif

// :6114
static void repeat_check(struct mc *mc, uint8_t prg) {
    if (*mc->si != ',') {
        parset(mc, 0xff, prg);
        return;
    }
    // :6118
    *mc->di++ = 0xff;
    *mc->di++ = prg;
    mc->si++;
    *mc->di++ = 0xce;
    stosw(mc, getnum(mc));
    // :6127
    if (*mc->si != ',') {
        // :6141
        stosw(mc, 0);
        stosw(mc, 0x8000);
        olc0(mc);
        return;
    }
    // :6129
    mc->si++;
    stosw(mc, getnum(mc));
    if (*mc->si != ',') {
        // :6144
        stosw(mc, 0x8000);
        olc0(mc);
        return;
    }
    // :6135
    mc->si++;
    stosw(mc, getnum(mc));
    olc0(mc);
}

// :6153
static void set_prg(struct mc *mc, uint8_t prg) {
#if !efc
    if (mc->ongen >= psg) return;
#endif
    mc->prg_num[prg] = 1;
}

// :6171
static void tieset(struct mc *mc, char cmd) {
    (void)cmd;
    if (*mc->si == '&') sular(mc); else tieset_2(mc, cmd);
}

// :6175
static void tieset_2(struct mc *mc, char cmd) {
    (void)cmd;
    if (mc->skip_flag != 0) {
        tie_skip(mc);
        return;
    }
    // :6179
    uint16_t base_len;
    if (lngset2(mc, &base_len) != 0) {
        tie_norm(mc);
        return;
    }
    // BUG: is this a typo?
    if (base_len >= 256) error(mc, ']', 8);
    if ((mc->prsok & 1) == 0) error(mc, ']', 22);
    // BUG: duplicate check at :6187?

    // :6191
    mc->di--;
    lngcal(mc);
    uint8_t len;
    if (futen(mc, &len) != 0) error(mc, '&', 8);
    // :6197
    uint8_t old_len = *mc->di;
    bool overflow = old_len + len >= 256;
    if (!overflow) {
        // :6200
        *mc->di++ = old_len + len;
        mc->prsok |= 2;
        olc02(mc);
        return;
    }

    // :6205
    mc->di++;
    if ((mc->prsok & 8) != 0) error(mc, '^', 8);
    // :6210
#if !efc
    if (mc->part == rhythm) {
        // :6222
        *mc->di++ = 0x0f;
        *mc->di++ = len;
        mc->prsok = 3;
        olc02(mc);
        return;
    }
#endif
    // :6214
    uint8_t onkai = mc->di[-2];
    *mc->di++ = 0xfb;
    *mc->di++ = onkai;
    *mc->di++ = len;
    mc->prsok = 1;
    olc02(mc);
}

// :6229
static void tie_norm(struct mc *mc) {
    mc->tie_flag = 1;
#if !efc
    if (mc->part == rhythm) error(mc, '&', 32);
#endif
    *mc->di++ = 0xfb;
    if ((mc->prsok & 1) == 0) {
        // :6239
        olc0(mc);
    } else {
        // :6240
        mc->prsok |= 4;
        mc->prsok &= 0xfe;
        olc02(mc);
    }
}

// :6243
static void tie_skip(struct mc *mc) {
    uint16_t len;
    lngset2(mc, &len);
    if (len >= 256) error(mc, '&', 8);
    futen_skip(mc);
}

// :6251
static void sular(struct mc *mc) {
#if !efc
    if (mc->part == rhythm) error(mc, '&', 32);
#endif
    mc->si++;
    if (mc->skip_flag != 0) {
        tie_skip(mc);
        return;
    }
    // :6261
    *mc->di++ = 0xc1;
    char *old_si = mc->si;
    uint16_t len;
    int lngset_status = lngset2(mc, &len);
    if (len >= 256) error(mc, '&', 8);
    mc->si = old_si;
    if (lngset_status != 0) {
        olc0(mc);
        return;
    }
    // :6273
    uint8_t onkai = mc->di[-3];
    ots002(mc, onkai);
    bp8(mc, onkai);
}

// :6279
static void detset(struct mc *mc, char cmd) {
    (void)cmd;
    switch (*mc->si++) {
    case 'D':
        detset_2(mc);
        return;
    case 'X':
        extdet_set(mc);
        return;
    case 'M':
        mstdet_set(mc);
        return;
    case 'F':
        vd_fm(mc);
        return;
    case 'S':
        vd_ssg(mc);
        return;
    case 'P':
        vd_pcm(mc);
        return;
    case 'R':
        vd_rhythm(mc);
        return;
    case 'Z':
        vd_ppz(mc);
        return;
    }
    // :6298
    mc->si--;
    mc->detune = getnum(mc);
    detset_exit(mc, mc->master_detune + mc->detune);
}

// :6303
static void detset_exit(struct mc *mc, uint16_t detune) {
    if (mc->bend != 0) return;
    *mc->di++ = 0xfa;
    stosw(mc, detune);
    olc0(mc);
}

// :6316
static void detset_2(struct mc *mc) {
    *mc->di++ = 0xd5;
    stosw(mc, getnum(mc));
    olc0(mc);
}

// :6329
static void mstdet_set(struct mc *mc) {
    mc->master_detune = getnum(mc);
    detset_exit(mc, mc->master_detune + mc->detune);
}

// :6338
static void extdet_set(struct mc *mc) {
    *mc->di++ = 0xcc;
    *mc->di++ = getnum(mc);
    olc0(mc);
}

// :6349
static void vd_fm(struct mc *mc) {
    vd_main(mc, 0xfe);
}

// :6351
static void vd_ssg(struct mc *mc) {
    vd_main(mc, 0xfc);
}

// :6353
static void vd_pcm(struct mc *mc) {
    vd_main(mc, 0xfa);
}

// :6355
static void vd_rhythm(struct mc *mc) {
    vd_main(mc, 0xf8);
}

// :6358
static void vd_ppz(struct mc *mc) {
    vd_main(mc, 0xf5);
}

// :6360
static void vd_main(struct mc *mc, uint8_t subcmd) {
    if (*mc->si != '+' && *mc->si != '-') subcmd++;
    // :6366
    *mc->di++ = 0xc0;
    *mc->di++ = subcmd;
    *mc->di++ = getnum(mc);
    olc0(mc);
}

// :6380
static uint16_t getnum(struct mc *mc) {
    while (*mc->si == ' ' || *mc->si == '\t') mc->si++;
    bool negative;
    switch (*mc->si) {
    case '+':
        negative = false;
        mc->si++;
        break;
    case '-':
        negative = true;
        mc->si++;
        break;
    default:
        negative = false;
        break;
    }

    // :6398
    uint16_t n;
    lngset(mc, &n);
    return negative ? -n : n;
}

// :6414
static void stloop(struct mc *mc, char cmd) {
    (void)cmd;
    *mc->di++ = 0xf9;
    uint8_t loop_idx = mc->lopcnt++;
    // :6420
    mc->loptbl[loop_idx] = mc->di - m_buf(mc);
    // :6433
    mc->lextbl[loop_idx] = NULL;
    // :6437
    mc->di += 2;
    // :6440
    mc->length_check2 = 0;
    // :6442
    olc0(mc);
}

// :6447
static void edloop(struct mc *mc, char cmd) {
    (void)cmd;
    *mc->di++ = 0xf8;
    uint16_t loops;
    if (lngset(mc, &loops) == 0) {
        // :6451
        edl00(mc, loops);
    } else {
        // :6452
        edl00b(mc, mc->loop_def);
    }
}

// :6454
static void edl00(struct mc *mc, uint16_t loops) {
    if (loops >= 256) error(mc, ']', 2);
    edl00b(mc, loops);
}

// :6459
static void edl00b(struct mc *mc, uint8_t loops) {
    *mc->di++ = loops;
    if (loops == 0) {
        // :6464
        if (mc->length_check2 == 0) error(mc, '[', 24);
    }
    // :6468
    *mc->di++ = 0;

    // :6473
    uint8_t loop_index = --mc->lopcnt;
    if (loop_index == 0xff) error(mc, ']', 23);
    // :6479
    uint16_t loop_start_offset = mc->loptbl[loop_index];
    write16(mc->di, loop_start_offset);
    // :6491
    uint16_t repeat_count_offset = mc->di - 2 - m_buf(mc);
    write16(&m_buf(mc)[loop_start_offset], repeat_count_offset);

    // :6508
    uint8_t *lex = mc->lextbl[loop_index];
    if (lex) {
        // :6513
        write16(lex, repeat_count_offset);
        // :6516
        if (mc->bend != 0) mc->alldet = 0x8000;
    }

    // :6520
    mc->di += 2;
    if (mc->si) olc0(mc);
}

// :6531
static void extloop(struct mc *mc, char cmd) {
    (void)cmd;
    *mc->di++ = 0xf7;
    // :6534
    uint8_t loop_index = mc->lopcnt - 1;
    if (loop_index == 0xff) error(mc, ':', 23);
    // :6540
    if (mc->lextbl[loop_index]) error(mc, ':', 25);
    // :6549
    mc->lextbl[loop_index] = mc->di;
    mc->di += 2;
    // :6553
    olc0(mc);
}

// :6558
static void lopset(struct mc *mc, char cmd) {
    (void)cmd;
#if efc
    error(mc, 'L', 17);
#else
    if (mc->part == rhythm) error(mc, 'L', 17);
    // :6565
    *mc->di++ = 0xf6;
    mc->allloop_flag = 1;
    mc->length_check1 = 0;
    olc0(mc);
#endif
}

// :6576
static void oshift(struct mc *mc, char cmd) {
    (void)cmd;
    switch (*mc->si) {
    case 'M':
        master_trans_set(mc);
        return;
    case '{':
        def_onkai_set(mc);
        return;
    case '_':
        // :6583
        mc->si++;
        parset(mc, 0xe7, getnum(mc));
        return;
    default:
        // :6587
        parset(mc, 0xf5, getnum(mc));
        return;
    }
}

// :6592
static void master_trans_set(struct mc *mc) {
    mc->si++;
    parset(mc, 0xb2, getnum(mc));
}

// :6598
static void def_onkai_set(struct mc *mc) {
    mc->si++;
    int8_t inc;
    switch (*mc->si++) {
    case '=': inc =  0; break;
    case '+': inc =  1; break;
    case '-': inc = -1; break;
    default: error(mc, '{' ,2);
    }
    // :6611
    for (char c = *mc->si++; c != '}'; c = *mc->si++) {
        if (c == ' ' || c == '\t' || c == ',') continue;
        // :6622
        switch (c) {
        case 'a': mc->def_a = inc; break;
        case 'b': mc->def_b = inc; break;
        case 'c': mc->def_c = inc; break;
        case 'd': mc->def_d = inc; break;
        case 'e': mc->def_e = inc; break;
        case 'f': mc->def_f = inc; break;
        case 'g': mc->def_g = inc; break;
        default: error(mc, '{', 2);
        }
    }
}

// :6636
static void volup(struct mc *mc, char cmd) {
    (void)cmd;
    if (*mc->si == '%') {
        // :6653
        mc->si++;
        uint16_t vol;
        lngset(mc, &vol);
        *mc->di++ = 0xe3;
        *mc->di++ = vol;
        olc0(mc);
        return;
    }
    if (*mc->si == '^') {
        // :6660
        mc->si++;
        if (*mc->si == '%') {
            // :6675
            mc->si++;
            uint16_t vol;
            lngset(mc, &vol);
            if (vol == 0) return;
            *mc->di++ = 0xde;
            *mc->di++ = vol;
            if (mc->skip_flag != 0) mc->acc_adr = mc->di;
            olc0(mc);
            return;
        }
        // :6664
        uint16_t vol_base;
        lngset(mc, &vol_base);
        uint8_t vol = ongen_sel_vol(mc, vol_base);
        if (vol == 0) return;
        *mc->di++ = 0xde;
        *mc->di++ = vol;
        if (mc->skip_flag != 0) mc->acc_adr = mc->di;
        olc0(mc);
        return;
    }
    // :6641
    uint16_t vol_base;
    lngset(mc, &vol_base);
    if (vol_base == 1) {
        // :6644
        *mc->di++ = 0xf4;
    } else {
        // :6647
        *mc->di++ = 0xe3;
        *mc->di++ = ongen_sel_vol(mc, vol_base);
    }
    olc0(mc);
}

// :6691
static void voldown(struct mc *mc, char cmd) {
    (void)cmd;
    if (*mc->si == '%') {
        // :6708
        mc->si++;
        uint16_t vol;
        lngset(mc, &vol);
        *mc->di++ = 0xe2;
        *mc->di++ = vol;
        olc0(mc);
        return;
    }
    if (*mc->si == '^') {
        // :6715
        mc->si++;
        if (*mc->si == '%') {
            // :6731
            mc->si++;
            uint16_t vol;
            lngset(mc, &vol);
            if (vol == 0) return;
            *mc->di++ = 0xdd;
            *mc->di++ = vol;
            if (mc->skip_flag != 0) mc->acc_adr = mc->di;
            olc0(mc);
            return;
        }
        // :6719
        uint16_t vol_base;
        lngset(mc, &vol_base);
        uint8_t vol = ongen_sel_vol(mc, vol_base);
        if (vol == 0) return;
        *mc->di++ = 0xdd;
        *mc->di++ = vol;
        if (mc->skip_flag != 0) mc->acc_adr = mc->di;
        olc0(mc);
        return;
    }
    // :6696
    uint16_t vol_base;
    lngset(mc, &vol_base);
    if (vol_base == 1) {
        // :6699
        *mc->di++ = 0xf3;
    } else {
        // :6702
        *mc->di++ = 0xe2;
        *mc->di++ = ongen_sel_vol(mc, vol_base);
    }
    olc0(mc);
}

// :6746
static void lfoset(struct mc *mc, char cmd) {
    (void)cmd;
#if !efc
    if (mc->part == rhythm) error(mc, 'M', 17);
#endif
    char c = *mc->si++;
    switch (c) {
    case 'X':
        extlfo_set(mc);
        return;
    case 'P':
        portaset(mc);
        return;
    case 'D':
        depthset(mc);
        return;
    case 'W':
        waveset(mc);
        return;
    case 'M':
        lfomask_set(mc);
        return;
    }

    // :6764
    uint8_t lfocmd;
    switch (c) {
    case 'A': lfocmd = 0xf2; break;
    default:  lfocmd = 0xf2; mc->si--; break;
    case 'B': lfocmd = 0xbf; break;
    }
    // :6771
    *mc->di++ = lfocmd;

    // :6775
    *mc->di++ = get_clock(mc, 'M');
    if (*mc->si++ != ',') {
        // :6807
        mc->si--;
        mc->di[-2] = lfocmd == 0xf2 ? 0xc2 : 0xb9;
        olc0(mc);
        return;
    }

    // :6785
    *mc->di++ = getnum(mc);
    if (*mc->si++ != ',') error(mc, 'M', 6);

    // :6793
    *mc->di++ = getnum(mc);
    if (*mc->si++ != ',') error(mc, 'M', 6);

    // :6801
    *mc->di++ = getnum(mc);

    // :6805
    olc0(mc);
}

// :6820
static void extlfo_set(struct mc *mc) {
    uint8_t lfocmd;
    switch (*mc->si++) {
    case 'A': lfocmd = 0xca; break;
    default:  lfocmd = 0xca; mc->si--; break;
    case 'B': lfocmd = 0xbb; break;
    }
    // :6829
    *mc->di++ = lfocmd;
    *mc->di++ = getnum(mc);
    olc0(mc);
}

// :6841
static void depthset(struct mc *mc) {
    uint8_t lfocmd;
    switch (*mc->si++) {
    case 'A': lfocmd = 0xd6; break;
    default:  lfocmd = 0xd6; mc->si--; break;
    case 'B': lfocmd = 0xbd; break;
    }
    // :6850
    *mc->di++ = lfocmd;
    uint8_t speed = getnum(mc);
    *mc->di++ = speed;
    // :6857
    if (speed == 0) {
        // :6859
        uint8_t depth = 0;
        if (*mc->si == ',') {
            // :6862
            mc->si++;
            depth = getnum(mc);
        }
        // :6895
        *mc->di++ = depth;
        olc0(mc);
        return;
    }
    // :6865
    if (*mc->si != ',') error(mc, 'M', 6);
    mc->si++;
    *mc->di++ = getnum(mc);
    if (*mc->si != ',') {
        // :6875
        olc0(mc);
        return;
    }
    // :6876
    mc->si++;
    *mc->di++ = 0xb7;
    uint8_t times = getnum(mc);
    if ((times & 0x80) != 0) error(mc, 'M', 2);
    if (lfocmd != 0xd6) times |= 0x80;
    // :6890
    *mc->di++ = times;
    olc0(mc);
}

// :6905
static void portaset(struct mc *mc) {
    uint8_t lfocmd;
    switch (*mc->si++) {
    case 'A': lfocmd = 0xf2; break;
    default:  lfocmd = 0xf2; mc->si--; break;
    case 'B': lfocmd = 0xbf; break;
    }
    // :6914
    mc->bend2 = 0;
    mc->bend3 = 1;
    *mc->di++ = lfocmd;
    mc->bend1 = getnum(mc);
    // :6922
    if (*mc->si == ',') {
        // :6924
        mc->si++;
        mc->bend2 = get_clock(mc, 'M');
        // :6928
        if (*mc->si == ',') {
            // :6930
            mc->si++;
            mc->bend3 = getnum(mc);
        }
    }
    // :6933
    *mc->di++ = mc->bend2;
    *mc->di++ = mc->bend3;
    *mc->di++ = mc->bend1;
    *mc->di++ = 255;
    parset(mc, lfocmd - 1, 1);
}

// :6950
static void waveset(struct mc *mc) {
    uint8_t lfocmd;
    switch (*mc->si++) {
    case 'A': lfocmd = 0xcb; break;
    default:  lfocmd = 0xcb; mc->si--; break;
    case 'B': lfocmd = 0xbc; break;
    }
    // :6959
    parset(mc, lfocmd, getnum(mc));
}

// :6969
static void lfomask_set(struct mc *mc) {
    uint8_t lfocmd;
    switch (*mc->si++) {
    case 'A': lfocmd = 0xc5; break;
    default:  lfocmd = 0xc5; mc->si--; break;
    case 'B': lfocmd = 0xba; break;
    }
    // :6978
    parset(mc, lfocmd, getnum(mc));
}

// :6988
static void lfoswitch(struct mc *mc, char cmd) {
    (void)cmd;
    uint8_t lfocmd;
    switch (*mc->si++) {
    case 'A': lfocmd = 0xf1; break;
    default:  lfocmd = 0xf1; mc->si--; break;
    case 'B': lfocmd = 0xbe; break;
    }
#if !efc
    if (mc->part == rhythm) lfocmd = 0xf1;
#endif
    // :7004
    uint16_t n;
    if (lngset(mc, &n) != 0) error(mc, '*', 6);
    *mc->di++ = lfocmd;
    *mc->di++ = n;

    // :7014
    if (*mc->si != ',') {
        olc0(mc);
        return;
    }
    mc->si++;
#if !efc
    if (mc->part == rhythm) error(mc, '*', 17);
#endif
    // :7023
    switch (*mc->si++) {
    case 'A': lfocmd = 0xf1; break;
    case 'B': lfocmd = 0xbe; break;
    default:  lfocmd = 0xbe; mc->si--; break;
    }
    // :7030
    if (lngset(mc, &n) != 0) error(mc, '*', 6);
    // BUG in original ASM: "cmp bh,-2[di]" lacks ES: segment override,
    // so it reads from DS:DI-2 (MML input area) instead of ES:DI-2
    // (output buffer). The comparison almost never matches, making
    // "sub si,2" effectively dead code.
    // Covered by LFOSWITCH.MML *A1,A2 testcase.
    // if (lfocmd == mc->di[-2]) mc->si -= 2;
    *mc->di++ = lfocmd;
    *mc->di++ = n;
    olc0(mc);
}

// :7047
static void psgenvset(struct mc *mc, char cmd) {
    (void)cmd;
    if (*mc->si == 'X') {
        extenv_set(mc);
        return;
    }
    *mc->di++ = 0xf0;
    // :7052
    for (int i = 0; i < 3; i++) {
        *mc->di++ = getnum(mc);
        if (*mc->si++ != ',') error(mc, 'E', 6);
    }
    // :7063
    *mc->di++ = getnum(mc);
    // :7067
    if (*mc->si != ',') {
        olc0(mc);
        return;
    }

    // :7072
    mc->di[-5] = 0xcd;
    mc->si++;
    mc->di[-1] = (mc->di[-1] & 0x0f) | (getnum(mc) << 4);
    // :7085
    uint8_t al;
    if (*mc->si == ',') {
        // :7088
        mc->si++;
        al = getnum(mc);
    }
    *mc->di++ = al;
    olc0(mc);
}

// :7098
static void extenv_set(struct mc *mc) {
    mc->si++;
    *mc->di++ = 0xc9;
    *mc->di++ = getnum(mc);
    olc0(mc);
}

// :7110
static void ycommand(struct mc *mc, char cmd) {
    (void)cmd;
    *mc->di++ = 0xef;
    // :7114
    uint16_t n;
    lngset(mc, &n);
    *mc->di++ = n;
    if (*mc->si++ != ',') error(mc, 'y', 6);
    lngset(mc, &n);
    *mc->di++ = n;
    // :7125
    olc0(mc);
}

// :7130
static void psgnoise(struct mc *mc, char cmd) {
    (void)cmd;
    if (*mc->si == '+' || *mc->si == '-') {
        // :7144
        *mc->di++ = 0xd0;
        *mc->di++ = getnum(mc);
    } else {
        // :7136
        *mc->di++ = 0xee;
        uint16_t freq;
        lngset(mc, &freq);
        *mc->di++ = freq;
    }
    olc0(mc);
}

// :7155
static void psgpat(struct mc *mc, char cmd) {
    (void)cmd;
    *mc->di++ = 0xed;
    // :7159
    uint16_t n;
    if (lngset(mc, &n) != 0) error(mc, 'P', 6);
    if (n >= 4) error(mc, 'P', 2);
    // :7166
    // The bit twiddling here has been simplified for readability.
    // See also the OPNA manual, section 3-3.
    bool tone = (n & 1) != 0;
    bool noise = (n & 2) != 0;
    *mc->di++ = (noise ? 0x38 : 0) | (tone ? 0x07 : 0);
    // :7183
    olc0(mc);
}

// :7188
static void bendset(struct mc *mc, char cmd) {
    (void)cmd;
    uint8_t bend = getnum(mc);
    if (bend >= 13) error(mc, 'B', 2);
    mc->bend = bend;
    if (bend == 0) mc->alldet = 0x8000;
}

// :7205
static void pitchset(struct mc *mc, char cmd) {
    (void)cmd;
    mc->pitch = getnum(mc);
}

// :7213
static void panset(struct mc *mc, char cmd) {
    (void)cmd;
    if (*mc->si == 'x') {
        // :7225
        mc->si++;
        *mc->di++ = 0xc3;
        *mc->di++ = getnum(mc);
        *mc->di++ = 0;
        if (*mc->si == ',') {
            // :7236
            mc->si++;
            uint8_t phase = getnum(mc);
            if (phase != 0) mc->di[-1] = 1;
        }
    } else {
        // :7216
        uint8_t pan = getnum(mc);
        if (pan >= 4) error(mc, 'p', 2);
        *mc->di++ = 0xec;
        *mc->di++ = pan;
    }
    olc0(mc);
}

// :7246
static void rhycom(struct mc *mc, char cmd) {
    (void)cmd;
    char c = *mc->si++;
    for (struct mc_rcom *rcomtbl = mc->rcomtbl; ; rcomtbl++) {
        // :7249
        if (rcomtbl->cmd == 0) error(mc, '\\', 1);
        // :7252
        if (rcomtbl->cmd == c) {
            switch (rcomtbl->impl(mc)) {
            case MC_RCOM_NORMAL:
                olc0(mc);
                return;
            case MC_RCOM_OLC02:
                olc02(mc);
                return;
            case MC_RCOM_OLC03:
                return;
            }
        }
    }
}

// :7284
static enum mc_rcom_ret mstvol(struct mc *mc) {
    if (*mc->si == '+' || *mc->si == '-') {
        // :7302
        *mc->di++ = 0xe6;
        *mc->di++ = getnum(mc);
    } else {
        // :7289
        *mc->di++ = 0xe8;
        uint16_t vol;
        if (lngset(mc, &vol) != 0) error(mc, '\\', 6);
        if (vol >= 64) error(mc, '\\', 2);
        *mc->di++ = vol;
    }
    return MC_RCOM_NORMAL;
}

// :7309
static enum mc_rcom_ret rthvol(struct mc *mc) {
    uint8_t n = rhysel(mc);
    if (*mc->si == '+' || *mc->si == '-') {
        // :7333
        *mc->di++ = 0xe5;
        // :7337 is unnecessary here, since we already know the return value
        // of n doesn't have any of the lower 5 bits set.
        *mc->di++ = n >> 5;
        *mc->di++ = getnum(mc);
    } else {
        // :7316
        uint16_t vol;
        if (lngset(mc, &vol) != 0) error(mc, '\\', 6);
        if (vol >= 32) error(mc, '\\', 2);
        *mc->di++ = 0xea;
        // :7327 and :7328 are unnecessary here, since we already know the
        // bounds of n and vol.
        *mc->di++ = n | vol;
    }
    return MC_RCOM_NORMAL;
}

// :7360
static uint8_t rhysel(struct mc *mc) {
    uint8_t n;
    switch (*mc->si++) {
    case 'b': n = 1; break;
    case 's': n = 2; break;
    case 'c': n = 3; break;
    case 'h': n = 4; break;
    case 't': n = 5; break;
    case 'i': n = 6; break;
    default: error(mc, '\\', 1);
    }
    // :7382
    return n << 5;
}

// :7347
static enum mc_rcom_ret panlef(struct mc *mc) {
    return rpanset(mc, 2);
}

// :7350
static enum mc_rcom_ret panmid(struct mc *mc) {
    return rpanset(mc, 3);
}

// :7353
static enum mc_rcom_ret panrig(struct mc *mc) {
    return rpanset(mc, 1);
}

// :7355
static enum mc_rcom_ret rpanset(struct mc *mc, uint8_t pan) {
    *mc->di++ = 0xe9;
    *mc->di++ = rhysel(mc) | pan;
    return MC_RCOM_NORMAL;
}

// :7388
static enum mc_rcom_ret bdset(struct mc *mc) {
    return rs00(mc, 1);
}

// :7391
static enum mc_rcom_ret snrset(struct mc *mc) {
    return rs00(mc, 2);
}

// :7394
static enum mc_rcom_ret cymset(struct mc *mc) {
    return rs00(mc, 4);
}

// :7397
static enum mc_rcom_ret hihset(struct mc *mc) {
    return rs00(mc, 8);
}

// :7400
static enum mc_rcom_ret tamset(struct mc *mc) {
    return rs00(mc, 16);
}

// :7403
static enum mc_rcom_ret rimset(struct mc *mc) {
    return rs00(mc, 32);
}

// :7405
static enum mc_rcom_ret rs00(struct mc *mc, uint8_t n) {
    if (mc->skip_flag != 0) {
        // :7436
        if (*mc->si == 'p') mc->si++;
        return MC_RCOM_OLC03;
    }
    if (*mc->si == 'p') {
        // :7410
        mc->si++;
        n |= 0x80;
        // :7423
        *mc->di++ = 0xeb;
        *mc->di++ = n;
        return MC_RCOM_NORMAL;
    }
    // :7413
    if (mc->di[-2] != 0xeb || (mc->di[-1] & 0x80) != 0 || mc->prsok != 0x80) {
        // :7423
        *mc->di++ = 0xeb;
        *mc->di++ = n;
    } else {
        // :7420
        mc->di[-1] |= n;
    }
    // :7431
    mc->prsok = 0x80;
    return MC_RCOM_OLC02;
}

// :7446
static void hscom(struct mc *mc, char cmd) {
    (void)cmd;
    char *hsptr;
    uint16_t n;
    if (lngset(mc, &n) == 0) {
        // :7454
        if (n >= 256) error(mc, '!', 2);
        // :7475
        hsptr = mc->hsbuf2[n];
    } else {
        // :7482
        if (!is_graph(*mc->si)) error(mc, '!', 6);
        struct mc_hs3 *found, *prefix_found;
        if (search_hs3(mc, &found, &prefix_found) == 0) {
            // :7487
            hsptr = found->ptr;
        } else {
            // :7489
            if (!prefix_found) error(mc, '!', 27);
            hsptr = prefix_found->ptr;
        }
    }
    // :7496
    if (!hsptr) error(mc, '!', 27);
    // :7505
    char *old_si = mc->si;
    // :7507
    mc->hsflag++;
    mc->si = hsptr;
    olc02(mc);
    olc03(mc);
    mc->hsflag--;
    // :7512
    mc->si = old_si;
}

// :7523
static int search_hs3(struct mc *mc, struct mc_hs3 **found, struct mc_hs3 **prefix_found) {
    *prefix_found = NULL;
    int miss_len = 0;
    for (int i = 0; i < 256; i++) {
        char *old_si = mc->si;
        struct mc_hs3 *candidate = *found = &mc->hsbuf3[i];
        // :7534
        if (!candidate->ptr) continue;
        char *check_name = candidate->name;
        // :7537
        for (int j = 0; j < 30; j++) {
            // :7540
            if (!is_graph(*mc->si)) {
                // :7555
                if (*check_name == 0) return 0;
                goto hscom3_next;
            }
            // :7542
            if (*check_name == 0) {
                // :7560
                if (j > miss_len) {
                    miss_len = j;
                    *prefix_found = candidate;
                }
                goto hscom3_next;
            }
            // :7544
            if (*mc->si++ != *check_name++) goto hscom3_next;
        }
        while (!is_graph(*mc->si)) mc->si++;
        return 0;
    hscom3_next:
        // :7566
        mc->si = old_si;
    }
    // :7571
    mc->si += miss_len;
    return 1;
}

// :7592
// cmd: dh
// n: dl
PMDC_NORETURN static void error(struct mc *mc, char cmd, uint8_t n) {
    // :7593
    calc_line(mc);
    // :7604
    if (mc->si) {
        print_line(mc, mc->mml_filename);
        print_chr(mc, '(');
        print_16(mc, mc->line);
        print_chr(mc, ')');
        print_chr(mc, ' ');
        print_chr(mc, ':');
    }
    // :7623
    print_mes(mc, errmes_1);
    print_8(mc, n);
    // :7642
    put_part(mc);
    // :7647
    if (cmd != 0) {
        print_mes(mc, errmes_3);
        mc->sys->putc(cmd, mc->user_data);
    }
    // :7670
    print_mes(mc, errmes_4);
    mc->sys->print(err_table[n], mc->user_data);
    // :7691
    if (mc->si && mc->line != 0 && (*mc->linehead == '\t' || !is_cntrl(*mc->linehead))) {
        // :7700
        // This logic has been reworked to avoid possible out of bounds reads
        // (fine in assembly, not in C) and double scanning.
        char *errline_end = mc->linehead;
        while (*errline_end != '\r' && *errline_end != 0) errline_end++;
        if (*errline_end == '\r') errline_end += 2;
        // :7714
        *errline_end = 0;
        print_line(mc, mc->linehead);
        // :7720
        mc->si[1] = 0;
        mc->si[0] = '^';
        if (!is_cntrl(mc->si[-1])) *--mc->si = '^';
        while (mc->si != mc->linehead) {
            if (is_graph(*--mc->si)) *mc->si = ' ';
        }
        // :7733
        mc->sys->print(mc->linehead, mc->user_data);
        print_mes(mc, crlf_mes);

    }
    // :7742
    error_exit(mc, 1);
}

// :7748
static void put_part(struct mc *mc) {
    if (mc->part == 0) return;
    print_mes(mc, errmes_2);
    mc->sys->putc(mc->part + 'A' - 1, mc->user_data);
    if (mc->hsflag == 0) return;
#if !efc
    if (mc->part != rhythm) {
        print_mes(mc, errmes_5);
        return;
    }
#endif
    if (mc->hsflag != 1) print_mes(mc, errmes_5);
}

// :7787
static void calc_line(struct mc *mc) {
    if (!mc->si) {
        // :7851
        mc->line = 0;
        mc->linehead = NULL;
        return;
    }

    uint8_t inc_level = 0;
    // We can't mimic the stack trick used by the original assembly code here
    // in C. As an alternative, we just assume there will be no more than 16
    // nested includes and manage our own stacks here.
    uint16_t line_stack[16];
    char *filename_stack[16];
    char *mml_buf = mc->mml_buf;
    mc->line = 1;

    // :7795
    for (;;) {
next_line:
        mc->linehead = mml_buf;
        if (mml_buf == mc->si) goto found_error;
        for (;;) {
            char c = *mml_buf++;
            if (mml_buf == mc->si) goto found_error;
            switch (c) {
            case 0:
                // :7851
                mc->line = 0;
                mc->linehead = NULL;
                mc->si = NULL;
                return;
            case '\r':
                // :7813
                mml_buf++;
                mc->line++;
                goto next_line;
            case 1:
                // :7818
                line_stack[inc_level] = mc->line;
                filename_stack[inc_level++] = mml_buf;
                mc->line = 1;
                while (*mml_buf++ != 0) ;
                goto next_line;
            case 2:
                // :7830
                mc->line = line_stack[--inc_level];
                mml_buf = filename_stack[inc_level];
                goto next_line;
            }
        }
    }
found_error:

    // :7837
    if (inc_level > 0) {
        mml_buf = filename_stack[--inc_level];
        strcpy(mc->mml_filename, mml_buf);
    }
}

// :7864
// name: si
static char *search_env(struct mc *mc, const char *name) {
    return mc->sys->getenv(name, mc->user_data);
}

// :7892
static void print_8(struct mc *mc, uint8_t n) {
    bool seen_digits = false;
    p8_oneset(mc, &n, 100, &seen_digits);
    p8_oneset(mc, &n, 10, &seen_digits);
    mc->sys->putc('0' + n, mc->user_data);
}

// :7903
static void p8_oneset(struct mc *mc, uint8_t *n, uint8_t div, bool *seen_digits) {
    char c = '0';
    while (div <= *n) {
        *n -= div;
        c++;
    }
    if (*seen_digits || c != '0') {
        mc->sys->putc(c, mc->user_data);
        *seen_digits = true;
    }
}

// :7930
static void print_16(struct mc *mc, uint16_t n) {
    bool seen_digits = false;
    p16_oneset(mc, &n, 10000, &seen_digits);
    p16_oneset(mc, &n, 1000, &seen_digits);
    p16_oneset(mc, &n, 100, &seen_digits);
    p16_oneset(mc, &n, 10, &seen_digits);
    mc->sys->putc('0' + n, mc->user_data);
}

// :7946
static void p16_oneset(struct mc *mc, uint16_t *n, uint16_t div, bool *seen_digits) {
    char c = '0';
    while (div <= *n) {
        *n -= div;
        c++;
    }
    if (*seen_digits || c != '0') {
        mc->sys->putc(c, mc->user_data);
        *seen_digits = true;
    }
}

// :7973
static void usage(struct mc *mc) {
    print_mes(mc, usames);
    error_exit(mc, 1);
}

// :7985
static void space_cut(struct mc *mc) {
    while (*mc->si == ' ') mc->si++;
}

// LC.INC:5
static void _print_mes(struct mc *mc, const char *mes) {
    if (mc->print_flag != 0) print_mes(mc, mes);
}

// LC.INC:13
static void lc(struct mc *mc, uint8_t lc_flag) {
    // LC.INC:34
    mc->print_flag = lc_flag;
    // LC.INC:37
    *part_chr(mc) = 'A';
    uint8_t *bp = m_buf(mc);

    // LC.INC:43
    for (; *part_chr(mc) < 'K'; *part_chr(mc) += 1, bp += 2) {
        mc->sib = m_buf(mc) + read16(bp);
        part_loop2(mc);
    }

    // LC.INC:126
    // The single loop in the original over both FM3 and PPZ extend parts has
    // been split into two loops for organizational clarity.
    for (int i = 0; i < 3; i++) {
        if (mc->fm3_adr[i] == 0) continue;
        mc->sib = m_buf(mc) + mc->fm3_adr[i];
        *part_chr(mc) = mc->fm3_partchr[i];
        part_loop2(mc);
    }
    for (int i = 0; i < 8; i++) {
        if (mc->pcm_adr[i] == 0) continue;
        mc->sib = m_buf(mc) + mc->pcm_adr[i];
        *part_chr(mc) = mc->pcm_partchr[i];
        part_loop2(mc);
    }

    // LC.INC:155
    *part_chr(mc) = 'K';
    mc->all_length = 0;
    mc->loop_length = -1;
    mc->loop_flag = 0;
    // LC.INC:163
    mc->sib = m_buf(mc) + read16(bp);
    uint8_t *r_table = m_buf(mc) + read16(bp + 2);
    // LC.INC:173
    kcom_loop(mc, r_table);
}

// LC.INC:48
// The relationship between part_loop2 and part_loop (LC.INC:43) is exactly
// the same as with cmloop and cmloop2. part_loop2 is used as a subroutine, and
// modeling it as such allows us to skip some checks that make the loop work out
// in the original.
static void part_loop2(struct mc *mc) {
    mc->all_length = 0;
    mc->loop_length = -1;
    mc->loop_flag = 0;

    // LC.INC:58
    if (*part_chr(mc) == 'A' && *mc->sib == 0xc6) {
        mc->sib++;
        for (int i = 0; i < 3; i++) {
            mc->fm3_adr[i] = lodsw(mc);
        }
    }
    // LC.INC:77
    if (*part_chr(mc) == 'J' && *mc->sib == 0xb4) {
        mc->sib++;
        for (int i = 0; i < 8; i++) {
            mc->pcm_adr[i] = lodsw(mc);
        }
    }
    // LC.INC:97
    for (;;) {
        uint8_t cmd = *mc->sib++;
        if (cmd == 0x80) {
            break;
        } else if (cmd > 0x80) {
            // LC.INC:107
            command_exec(mc, cmd);
            if (mc->loop_flag != 0) break;
        } else {
            mc->all_length += *mc->sib++;
        }
    }
    // LC.INC:116
    print_length(mc);
}

// LC.INC:173
static void kcom_loop(struct mc *mc, uint8_t *r_table) {
    for (;;) {
        uint8_t cmd = *mc->sib++;
        if (cmd == 0x80) {
            break;
        } else if (cmd > 0x80) {
            // LC.INC:195
            command_exec(mc, cmd);
            if (mc->loop_flag != 0) break;
        } else {
            uint8_t *old_sib = mc->sib;
            mc->sib = m_buf(mc) + read16(r_table + 2 * cmd);
            rcom_loop(mc);
            mc->sib = old_sib;
        }
    }
    // LC.INC:206
    print_length(mc);
}

// LC.INC:213
static void rcom_loop(struct mc *mc) {
    for (;;) {
        uint8_t cmd = *mc->sib++;
        if (cmd == 0xff) {
            break;
        } else if (cmd >= 0xc0) {
            // LC.INC:232
            command_exec(mc, cmd);
            if (mc->loop_flag != 0) break;
        } else {
            if ((cmd & 0x80) != 0) mc->sib++;
            mc->all_length += *mc->sib++;
        }
    }
}

// LC.INC:247
static void command_exec(struct mc *mc, uint8_t cmd) {
    switch (cmd) {
    // LC.INC:491
    case 0xff: jump1(mc);        break;
    case 0xfe: jump1(mc);        break;
    case 0xfd: jump1(mc);        break;
    case 0xfc: _tempo(mc);       break;
    case 0xfb: jump0(mc);        break;
    case 0xfa: jump2(mc);        break;
    case 0xf9: loop_start(mc);   break;
    case 0xf8: loop_end(mc);     break;
    case 0xf7: loop_exit(mc);    break;
    case 0xf6: loop_set(mc);     break;
    case 0xf5: jump1(mc);        break;
    case 0xf4: jump0(mc);        break;
    case 0xf3: jump0(mc);        break;
    case 0xf2: jump4(mc);        break;
    case 0xf1: jump1(mc);        break;
    case 0xf0: jump4(mc);        break;
    case 0xef: jump2(mc);        break;
    case 0xee: jump1(mc);        break;
    case 0xed: jump1(mc);        break;
    case 0xec: jump1(mc);        break;
    case 0xeb: jump1(mc);        break;
    case 0xea: jump1(mc);        break;
    case 0xe9: jump1(mc);        break;
    case 0xe8: jump1(mc);        break;
    case 0xe7: jump1(mc);        break;
    case 0xe6: jump1(mc);        break;
    case 0xe5: jump2(mc);        break;
    case 0xe4: jump1(mc);        break;
    case 0xe3: jump1(mc);        break;
    case 0xe2: jump1(mc);        break;
    case 0xe1: jump1(mc);        break;
    case 0xe0: jump1(mc);        break;
    case 0xdf: jump1(mc);        break;
    case 0xde: jump1(mc);        break;
    case 0xdd: jump1(mc);        break;
    case 0xdc: jump1(mc);        break;
    case 0xdb: jump1(mc);        break;
    case 0xda: porta(mc);        break;
    case 0xd9: jump1(mc);        break;
    case 0xd8: jump1(mc);        break;
    case 0xd7: jump1(mc);        break;
    case 0xd6: jump2(mc);        break;
    case 0xd5: jump2(mc);        break;
    case 0xd4: jump1(mc);        break;
    case 0xd3: jump1(mc);        break;
    case 0xd2: jump1(mc);        break;
    case 0xd1: jump1(mc);        break;
    case 0xd0: jump1(mc);        break;
    case 0xcf: jump1(mc);        break;
    case 0xce: jump6(mc);        break;
    case 0xcd: jump5(mc);        break;
    case 0xcc: jump1(mc);        break;
    case 0xcb: jump1(mc);        break;
    case 0xca: jump1(mc);        break;
    case 0xc9: jump1(mc);        break;
    case 0xc8: jump3(mc);        break;
    case 0xc7: jump3(mc);        break;
    case 0xc6: jump6(mc);        break;
    case 0xc5: jump1(mc);        break;
    case 0xc4: jump1(mc);        break;
    case 0xc3: jump2(mc);        break;
    case 0xc2: jump1(mc);        break;
    case 0xc1: jump0(mc);        break;
    case 0xc0: special_0c0h(mc); break;
    case 0xbf: jump4(mc);        break;
    case 0xbe: jump1(mc);        break;
    case 0xbd: jump2(mc);        break;
    case 0xbc: jump1(mc);        break;
    case 0xbb: jump1(mc);        break;
    case 0xba: jump1(mc);        break;
    case 0xb9: jump1(mc);        break;
    case 0xb8: jump2(mc);        break;
    case 0xb7: jump1(mc);        break;
    case 0xb6: jump1(mc);        break;
    case 0xb5: jump2(mc);        break;
    case 0xb4: jump16(mc);       break;
    case 0xb3: jump1(mc);        break;
    case 0xb2: jump1(mc);        break;
    case 0xb1: jump1(mc);        break;
    }
}

// LC.INC:256
static void jump16(struct mc *mc) { mc->sib += 16; }
static void jump6 (struct mc *mc) { mc->sib +=  6; }
static void jump5 (struct mc *mc) { mc->sib +=  5; }
static void jump4 (struct mc *mc) { mc->sib +=  4; }
static void jump3 (struct mc *mc) { mc->sib +=  3; }
static void jump2 (struct mc *mc) { mc->sib +=  2; }
static void jump1 (struct mc *mc) { mc->sib +=  1; }
static void jump0 (struct mc *mc) { (void)mc;      }

// LC.INC:268
static void _tempo(struct mc *mc) {
    if (*mc->sib++ >= 251) mc->sib++;
}

// LC.INC:279
static void porta(struct mc *mc) {
    mc->sib += 2;
    mc->all_length += *mc->sib++;
}

// LC.INC:291
static void loop_set(struct mc *mc) {
    mc->loop_length = mc->all_length;
}

// LC.INC:301
static void loop_start(struct mc *mc) {
    uint16_t end_offset = read16(mc->sib);
    mc->sib += 2;
    m_buf(mc)[end_offset + 1] = 0;
}

// LC.INC:311
static void loop_end(struct mc *mc) {
    uint8_t len = *mc->sib++;
    if (len == 0) {
        // LC.INC:329
        mc->loop_flag = 1;
        return;
    }
    // LC.INC:316
    *mc->sib += 1;
    if (*mc->sib++ != len) {
        // LC.INC:324
        uint16_t start_offset = read16(mc->sib);
        mc->sib = m_buf(mc) + start_offset + 2;
        return;
    }
    // LC.INC:320
    mc->sib += 2;
}

// LC.INC:336
static void loop_exit(struct mc *mc) {
    uint16_t end_offset = read16(mc->sib);
    mc->sib += 2;
    uint8_t *end = m_buf(mc) + end_offset;
    uint8_t loops = *end++ - 1;
    if (loops == *end) {
        mc->sib = end + 3;
    }
}

// LC.INC:354
static void special_0c0h(struct mc *mc) {
    uint8_t cmd = *mc->sib++;
    if (cmd < 2) return;
    switch (cmd) {
    // LC.INC:571
    case 0xff: jump1(mc); break;
    case 0xfe: jump1(mc); break;
    case 0xfd: jump1(mc); break;
    case 0xfc: jump1(mc); break;
    case 0xfb: jump1(mc); break;
    case 0xfa: jump1(mc); break;
    case 0xf9: jump1(mc); break;
    case 0xf8: jump1(mc); break;
    case 0xf7: jump1(mc); break;
    case 0xf6: jump1(mc); break;
    case 0xf5: jump1(mc); break;
    }
}

// LC.INC:371
static void print_length(struct mc *mc) {
    // LC.INC:377
    if (mc->all_length == 0) return;
    _print_mes(mc, mc->part_mes);
    // LC.INC:381
    if (mc->all_length > mc->max_all) mc->max_all = mc->all_length;
    // LC.INC:392
    print_32(mc, mc->all_length);

    // LC.INC:394
    if (mc->loop_flag != 1) {
        // LC.INC:399
        if (mc->loop_length != (uint32_t)-1) {
            // LC.INC:407
            _print_mes(mc, loop_mes);
            uint32_t loop_length = mc->all_length - mc->loop_length;
            // LC.INC:413
            if (loop_length > mc->max_loop) mc->max_loop = loop_length;
            // LC.INC:420
            print_32(mc, loop_length);
        }
        // LC.INC:422
        _print_mes(mc, _crlf_mes);
    } else {
        // LC.INC:396
        _print_mes(mc, loop_mes2);
    }
    // LC.INC:424
}

// LC.INC:432
static void print_32(struct mc *mc, uint32_t n) {
    if (mc->print_flag == 0) return;
    bool seen_digits = false;
    for (int i = 0; i < 9; i++) sub_pr(mc, &n, num_data[i], &seen_digits);
    mc->sys->putc('0' + n, mc->user_data);
}

// LC.INC:454
static void sub_pr(struct mc *mc, uint32_t *n, uint32_t div, bool *seen_digits) {
    char c = '0';
    while (*n >= div) {
        *n -= div;
        c++;
    }
    if (c != '0' || *seen_digits) {
        mc->sys->putc(c, mc->user_data);
        *seen_digits = true;
    }
}

// DISKPMD.INC:61
static int diskwrite(struct mc *mc, const char *filename, void *data, uint16_t n) {
    void *file = makhnd(mc, filename);
    if (!file) return 1;
    if (wrihnd(mc, file, data, n) != 0) return 1;
    if (clohnd(mc, file) != 0) return 1;
    return 0;
}

// DISKPMD.INC:112
static void *opnhnd(struct mc *mc, const char *filename, char oh_filename[static 128]) {
    void *file = mc->sys->open(filename, mc->user_data);
    if (file) return file;

    // DISKPMD.INC:128
    const char *search = filename;
    for (;; search++) {
        if (sjis_check(*search)) {
            search++;
            continue;
        }
        if (*search == 0) break;
        if (*search == ':' || *search == '\\') filename = search;
    }

    // DISKPMD.INC:157
    const char *pmd_path = mc->sys->getenv("PMD", mc->user_data);
    if (!pmd_path) return NULL;

    // DISKPMD.INC:179
    char *ohf_write = oh_filename;
    bool is_sjis = false;
    while (*pmd_path != 0) {
        if (sjis_check(*pmd_path)) {
            is_sjis = true;
            *ohf_write++ = *pmd_path++;
        }
        *ohf_write++ = *pmd_path++;
    }
    // DISKPMD.INC:199
    if (is_sjis || (ohf_write[-1] != '\\' && ohf_write[-1] == ':')) {
        *ohf_write++ = '\\';
    }
    // DISKPMD.INC:212
    while (*filename != 0) {
        *ohf_write++ = *filename++;
    }
    *ohf_write++ = 0;

    // DISKPMD.INC:221
    return mc->sys->open(oh_filename, mc->user_data);
}

// DISKPMD.INC:250
static bool sjis_check(char c) {
    return (c & 0x80) != 0 && ((c - 0x20) & 0x40) != 0;
}

// DISKPMD.INC:270
static int redhnd(struct mc *mc, void *file, void *dest, uint16_t n, uint16_t *read) {
    return mc->sys->read(file, dest, n, read, mc->user_data);
}

// DISKPMD.INC:280
static int clohnd(struct mc *mc, void *file) {
    return mc->sys->close(file, mc->user_data);
}

// DISKPMD.INC:291
static void *makhnd(struct mc *mc, const char *filename) {
    return mc->sys->create(filename, mc->user_data);
}

// DISKPMD.INC:303
static int wrihnd(struct mc *mc, void *file, void *data, uint16_t n) {
    return mc->sys->write(file, data, n, mc->user_data);
}
