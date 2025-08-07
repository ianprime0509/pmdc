// :7
#define ver "4.8s"
#define vers 0x48
#define date "Jan.22nd 2020"

// :10547
static const char mes_warning[] = "WARNING:";

// :10560
static const char mes_title[] = "Ｍｕｓｉｃ　Ｄｒｉｖｅｒ　Ｐ.Ｍ.Ｄ. for PC9801/88VA Version " ver
#ifdef _optnam
    _optnam
#endif
    "\n"
    "Copyright (C)1989," date " by M.Kajihara(KAJA).\n\n";

// :10590
static const char mes_ppsdrv[] = "PPSDRV(INT64H)に対応します．\n";

// :10598
static const char cantcut_mes[] = "現在常駐解除は禁止されています．PMD関連TSRを先に解放して下さい．\n";
static const char pmderror_mes1[] = "常駐時とFM割り込みベクトルが違います．\n";
static const char pmderror_mes2[] = "曲データバッファは 1KB 以上にして下さい．\n";
static const char pmderror_mes3[] = "曲+音色+効果音データの合計は 40KB 以内にして下さい．\n";
static const char pmderror_mes4[] = "解放に失敗しました．\n";
static const char pmderror_mes5[] = "ＰＭＤは常駐していません．\n";
static const char pmderror_mes6[] = "変更出来ないオプションを指定しています．\n";
static const char pmderror_mes7[] = "メモリが確保出来ません。\n";

// :10613
static const char mes_kankyo[] = "環境変数 PMDOPT の設定に誤りがあります．\n";

// :10615
static const char mes_usage[] = "Usage:\tPMD"
#if va
    "VA"
# if !board2
    "1"
# endif
#else
# if board2
#  if pcm
    "86"
#  else
#   if ppz
    "PPZ"
#   else
    "B2"
#   endif
#   if ademu
    "E"
#   endif
#  endif
# endif
#endif
    " [/option[数値]][/option[数値]]..\n"
    "Option:"
    "\t  /Mn  曲データ      バッファサイズ指定(KB単位)Def.=16\n"
    "\t  /Vn  音色データ    バッファサイズ指定(KB単位)Def.= 8\n"
    "\t  /En  FM効果音データバッファサイズ指定(KB単位)Def.= 4\n"
#if !va
    "\t  /On  FM音源 PORT指定 0=088H 1=188H 2=288H 3=388H Def.=auto\n"
#endif
    "\t* /DFn FM音源の    音量調整値設定(最大0～最小255)Def.=\n"
#if board2
    " 0"
#else
    "16"
#endif
    "\t* /DSn SSG音源の   音量調整値設定(最大0～最小255)Def.= 0\n"
#if board2
    "\t* /DPn PCM音源の   音量調整値設定(最大0～最小255)Def.= 0\n"
# if ppz
    "\t* /DZn PPZ8   の   音量調整値設定(最大0～最小255)Def.= 0\n"
# endif
    "\t* /DRn RHYTHM音源の音量調整値設定(最大0～最小255)Def.= 0\n"
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
    "\t* /Wn  FM音源REG-DATA間のWait回数の設定(1～255,数値省略時は自動判別)\n"
    "\t* /Gn  GRPHキーによる早送りのTimerB値 Def.=250\n"
#if board2 * adpcm
# if !ademu
    "\t* /An  ADPCM定義速度 0=高速 1=中速 2=低速 Def.=Auto\n"
# endif
#endif
#if board2 * pcm
    "\t* /S(-)PCMパート仕様をPMDB2に合わせる(合わせない)\n"
    "\t* /HZn PCM周波数(0～8,4.13/5.52/8.27/11.03/16.54/22.05/33.08/44.10/自動)\n"
#endif
    "\t  /R   常駐解除する\t\t/H   ヘルプの表示\n"
    "\t(* の付いたoptionは常駐後に再設定可能, - は括弧内に設定)";
