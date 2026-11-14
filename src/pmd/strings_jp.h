// :7
#define ver "4.8s"
#define vers 0x48
#define verc "s"
#define date "Jan.22nd 2020"

// :45
// The original defines these in the variant entry files. Selecting them from
// the feature flags keeps the strings shared by all variants.
#if va && board2
#define resmes "PMDVA ver." ver
#define _optnam "(VA+ADPCM)"
#elif va
#define resmes "PMDVA1 ver." ver
#define _optnam "(VA)"
#elif pcm
#define resmes "PMD86 ver." ver
#define _optnam "(86PCM)"
#elif ppz && ademu
#define resmes "PMDPPZE ver." ver
#define _optnam "(PPZ8E)"
#elif ppz
#define resmes "PMDPPZ ver." ver
#define _optnam "(PPZ8)"
#elif board2
#define resmes "PMDB2 ver." ver
#define _optnam "(ADPCM)"
#else
#define resmes "PMD ver." ver
#define _optnam ""
#endif

// :10547
static PMDC_UNUSED const char mes_warning[] = "\aWARNING:";

// :10550
#if board2
#if pcm || ppz
static PMDC_UNUSED const char mes_not_board[] = "YM2608+PCM が見つかりません．\n";
#else
static PMDC_UNUSED const char mes_not_board[] = "YM2608+ADPCM が見つかりません．\n";
#endif
#else
static PMDC_UNUSED const char mes_not_board[] = "ＦＭ音源ボードが装着されていません．\n";
#endif

// :10558
static PMDC_UNUSED const char mes_cut[] = "常駐解除しました．\n";

// :10560
static PMDC_UNUSED const char mes_title[] =
    "Ｍｕｓｉｃ　Ｄｒｉｖｅｒ　Ｐ.Ｍ.Ｄ. for PC9801/88VA Version " ver _optnam "\n"
    "Copyright (C)1989," date " by M.Kajihara(KAJA).\n\n";

// :10567
// NOTE: mes_int, int_num, port_num, and the following "(" are one resident
// string in the original. int_init writes the interrupt number into int_num
// (:8937) and fm_check writes the FM port into port_num (:8860), so struct pmd
// holds the mutable copy. This default must fit in pmd->mes_int. The
// placeholders are index 4 and, on non-VA builds, index 11.
#if va
static PMDC_UNUSED const char default_mes_int[] = "Int  ,Port 44H(";
#else
static PMDC_UNUSED const char default_mes_int[] = "Int  ,Port  88H(";
#endif
static PMDC_UNUSED const char mes_int2[] = ")を使用します．\n";

// :10577
static PMDC_UNUSED const char mes_ongen0[] = "YM2203";
// :10578
// The ADPCM RAM check overwrites the "2608" in this string to report a YMF288
// (ADRAMCHK.ASM:62), so struct pmd holds the mutable copy.
static PMDC_UNUSED const char default_mes_ongen1[] = "YM2608";

// :10582
#if board2 && adpcm
static PMDC_UNUSED const char mes_adpcm[] = "+ADPCM";
static PMDC_UNUSED const char mes_only[] = " only";
#endif

// :10587
#if board2 && pcm
static PMDC_UNUSED const char mes_pcm[] = "+PCM";
#endif

// :10590
static PMDC_UNUSED const char mes_ppsdrv[] = "PPSDRV(INT64H)に対応します．\n";
#if ppz
static PMDC_UNUSED const char mes_ppz8[] = "PPZ8(INT7FH)に対応します．\n";
#endif
static PMDC_UNUSED const char mes_wait1[] = "FM音源LSI REG-DATA間 Waitloop回数: ";
static PMDC_UNUSED const char mes_wait2[] = "回 (約 ";
static PMDC_UNUSED const char mes_wait3[] = "ns)\n";

// :10598
static PMDC_UNUSED const char cantcut_mes[] = "現在常駐解除は禁止されています．PMD関連TSRを先に解放して下さい．\n";
static PMDC_UNUSED const char pmderror_mes1[] = "常駐時とFM割り込みベクトルが違います．\n";
static PMDC_UNUSED const char pmderror_mes2[] = "曲データバッファは 1KB 以上にして下さい．\n";
static PMDC_UNUSED const char pmderror_mes3[] = "曲+音色+効果音データの合計は 40KB 以内にして下さい．\n";
static PMDC_UNUSED const char pmderror_mes4[] = "解放に失敗しました．\n";
static PMDC_UNUSED const char pmderror_mes5[] = "ＰＭＤは常駐していません．\n";
static PMDC_UNUSED const char pmderror_mes6[] = "変更出来ないオプションを指定しています．\n";
static PMDC_UNUSED const char pmderror_mes7[] = "メモリが確保出来ません。\n";

// :10607
static PMDC_UNUSED const char mes_exit[] =
    "常駐しました．\n"
    "INT 60HでPMDを操作可能です．\n";

// :10611
static PMDC_UNUSED const char mes_key[] = "ESC/GRPHキーで曲の停止、早送りが可能です．\n";

// :10613
static PMDC_UNUSED const char mes_kankyo[] = "環境変数 PMDOPT の設定に誤りがあります．\n";

// :10615
static PMDC_UNUSED const char mes_usage[] =
    "Usage:\tPMD"
#if va
    "VA"
#if !board2
    "1"
#endif
#elif board2
#if pcm
    "86"
#else
#if ppz
    "PPZ"
#else
    "B2"
#endif
#if ademu
    "E"
#endif
#endif
#endif
    " [/option[数値]][/option[数値]]..\n"
    "Option:"
    "\t  /Mn  曲データ      バッファサイズ指定(KB単位)Def.=16\n"
    "\t  /Vn  音色データ    バッファサイズ指定(KB単位)Def.= 8\n"
    "\t  /En  FM効果音データバッファサイズ指定(KB単位)Def.= 4\n"
#if !va
    "\t  /On  FM音源 PORT指定 0=088H 1=188H 2=288H 3=388H Def.=auto\n"
#endif
    "\t* /DFn FM音源の    音量調整値設定(最大0〜最小255)Def.="
#if board2
    " 0\n"
#else
    "16\n"
#endif
    "\t* /DSn SSG音源の   音量調整値設定(最大0〜最小255)Def.= 0\n"
#if board2
    "\t* /DPn PCM音源の   音量調整値設定(最大0〜最小255)Def.= 0\n"
#if ppz
    "\t* /DZn PPZ8   の   音量調整値設定(最大0〜最小255)Def.= 0\n"
#endif
    "\t* /DRn RHYTHM音源の音量調整値設定(最大0〜最小255)Def.= 0\n"
    "\t* /N(-)SSGドラムと同時にリズム音源を鳴らさない(鳴らす)\n"
#endif
    "\t* /K(-)ESC/GRPHキーで曲の停止/早送りをしない(する)\n"
#if va
    "\t* /KEn MSTOP/ESC と同時に使用するキーの設定 Def.=4\n"
    "\t* /KGn FF   /GRPHと同時に使用するキーの設定 Def.=4\n"
    "\t* /KRn REW  /GRPHと同時に使用するキーの設定 Def.=2\n"
    "\t\t設定値: 1=ｶﾅ 2=SHIFT 4=CTRL (加算して複数指定可)\n"
#else
    "\t* /KEn MSTOP/ESC と同時に使用するキーの設定 Def.=16\n"
    "\t* /KGn FF   /GRPHと同時に使用するキーの設定 Def.=16\n"
    "\t* /KRn REW  /GRPHと同時に使用するキーの設定 Def.=1\n"
    "\t\t設定値: 1=SHIFT 2=CAPS 4=ｶﾅ 16=CTRL (加算して複数指定可)\n"
#endif
    "\t* /F(-)フェードアウト後に曲を停止しない(する)\n"
    "\t* /I(-)FM/INT60割り込み中は割り込みを禁止する(しない)\n"
    "\t* /P(-)PPSDRVに対応しない(常駐していたら対応する)\n"
#if ppz
    "\t* /Z(-)PPZ8に対応しない(常駐していたら対応する)\n"
#endif
    "\t* /Wn  FM音源REG-DATA間のWait回数の設定(1〜255,数値省略時は自動判別)\n"
    "\t* /Gn  GRPHキーによる早送りのTimerB値 Def.=250\n"
#if board2 && adpcm && !ademu
    "\t* /An  ADPCM定義速度 0=高速 1=中速 2=低速 Def.=Auto\n"
#endif
#if board2 && pcm
    "\t* /S(-)PCMパート仕様をPMDB2に合わせる(合わせない)\n"
    "\t* /HZn PCM周波数(0〜8,4.13/5.52/8.27/11.03/16.54/22.05/33.08/44.10/自動)\n"
#endif
    "\t  /R   常駐解除する\t\t/H   ヘルプの表示\n"
    "\t(* の付いたoptionは常駐後に再設定可能, - は括弧内に設定)";

// :10692
static PMDC_UNUSED const char changemes_0[] = "設定を変更しました．\n";
static PMDC_UNUSED const char crlf[] = "\n";
static PMDC_UNUSED const char changemes_01[] =
    "　----- 現在の設定 -----\n"
    "・曲　　データバッファサイズ:";
static PMDC_UNUSED const char changemes_02[] = "KB\n・音色　データバッファサイズ:";
static PMDC_UNUSED const char changemes_03[] = "KB\n・効果音データバッファサイズ:";
static PMDC_UNUSED const char changemes_1[] = "KB\n・FM音源    音量調整値:";
static PMDC_UNUSED const char changemes_2[] = "・SSG音源   音量調整値:";
#if board2
static PMDC_UNUSED const char changemes_3[] = "・PCM音源   音量調整値:";
#if ppz
static PMDC_UNUSED const char changemes_3z[] = "・PPZ8      音量調整値:";
#endif
static PMDC_UNUSED const char changemes_4[] = "・RHYTHM音源音量調整値:";
static PMDC_UNUSED const char changemes_5[] = "・SSGドラム発音時に同時にRHYTHM音源を鳴ら";
static PMDC_UNUSED const char changemes_5a[] = "す\n";
static PMDC_UNUSED const char changemes_5b[] = "さない\n";
#endif
static PMDC_UNUSED const char changemes_6[] = "・GRPH/ESCキーで早送り/曲の停止を制御";
static PMDC_UNUSED const char changemes_6a[] = "する\n";
static PMDC_UNUSED const char changemes_6b[] = "しない\n";
static PMDC_UNUSED const char changemes_7[] = "・MSTOP/ESC と同時に使用するキー設定値:";
static PMDC_UNUSED const char changemes_8[] = "・FF   /GRPHと同時に使用するキー設定値:";
static PMDC_UNUSED const char changemes_8b[] = "・REW  /GRPHと同時に使用するキー設定値:";
static PMDC_UNUSED const char changemes_9[] = "・フェードアウト後に曲の演奏を停止";
static PMDC_UNUSED const char changemes_10[] = "・FM/INT60割り込み中に割り込みを禁止";
static PMDC_UNUSED const char changemes_11[] = "・PPSDRVを使用";
static PMDC_UNUSED const char changemes_11_[] = "・PPZ8  を使用";
static PMDC_UNUSED const char changemes_12[] = "・FM音源LSI REG-DATA間 Waitloop回数: ";
static PMDC_UNUSED const char changemes_12a[] = "回(約 ";
static PMDC_UNUSED const char changemes_12b[] = "ns)\n";
static PMDC_UNUSED const char changemes_13[] = "・GRPHによる早送り時のTimerB値: ";
#if board2 && adpcm && !ademu
static PMDC_UNUSED const char changemes_14[] = "・ADPCM定義速度: ";
static PMDC_UNUSED const char changemes_14a[] = "高速\n";
static PMDC_UNUSED const char changemes_14b[] = "中速\n";
static PMDC_UNUSED const char changemes_14c[] = "低速\n";
#endif
#if board2 && pcm
static PMDC_UNUSED const char changemes_15[] = "・PCMパート仕様をPMDB2に合わせ";
static PMDC_UNUSED const char changemes_15a[] = "ない\n";
static PMDC_UNUSED const char changemes_15b[] = "る\n";
static PMDC_UNUSED const char changemes_16[] = "・PCM再生周波数: ";
// The original stores these consecutively after changemes_16a and selects
// one by adding its offset, so the entries are an array here.
static PMDC_UNUSED const char *const changemes_16a[] = {
    "4.13438", "5.51250", "8.26875", "11.0250", "16.5375",
    "22.0500", "33.0750", "44.1000", "自動選択\n",
};
static PMDC_UNUSED const char changemes_16b[] = "KHz 固定\n";
#endif
