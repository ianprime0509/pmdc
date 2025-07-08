// :9
#define ver "4.8s"
#define vers 0x48
#define date "2023/09/23"

// :8015
static const char errmes_1[] = " Error ";
static const char errmes_2[] = ": Part ";
static const char errmes_3[] = ": Command ";
static const char errmes_4[] = "\n---------- ";
static const char errmes_5[] = " (Macro)";
static const char incfile_mes[] = "Include file :";
static const char crlf_mes[] = "\n";

// :8023
static const char err00[] = "オプション指定が間違っています。\n";
static const char err01[] = "MML中に理解不能な文字があります。\n";
static const char err02[] = "指定された数値が異常です。\n";
static const char err03[] = "MMLファイルが読み込めません。\n";
static const char err04[] = "Mファイルが書き込めません。\n";
static const char err05[] = "FFファイルが書き込めません。\n";
static const char err06[] = "パラメータの指定が足りません。\n";
static const char err07[] = "使用出来ない文字を指定しています。\n";
static const char err08[] = "指定された音長が長すぎます。\n";
static const char err09[] = "ポルタメント終了記号 } がありません。\n";
static const char err10[] = "Lコマンド後に音長指定がありません。\n";
static const char err11[] = "効果音パートでハードＬＦＯは使用出来ません。\n";
static const char err12[] = "効果音パートでテンポ命令は使用出来ません。\n";
static const char err13[] = "ポルタメント開始記号 { がありません。\n";
static const char err14[] = "ポルタメントコマンド中の指定が間違っています。\n";
static const char err15[] = "ポルタメントコマンド中に休符があります。\n";
static const char err16[] = "音程コマンドの直後に指定して下さい。\n";
static const char err17[] = "ここではこのコマンドは使用できません。\n";
static const char err18[] = "MMLのサイズが大き過ぎます。\n";
static const char err19[] = "コンパイル後のサイズが大き過ぎます。\n";
static const char err20[] = "W/Sコマンド使用中に255stepを越える音長は指定出来ません。\n";
static const char err21[] = "使用不可能な音長を指定しています。\n";
static const char err22[] = "タイが音程命令直後に指定されていません。\n";
static const char err23[] = "ループ開始記号 [ がありません。\n";
static const char err24[] = "無限ループ中に音長を持つ命令がありません。\n";
static const char err25[] = "１ループ中に脱出記号が２ヶ所以上あります。\n";
static const char err26[] = "音程が限界を越えています。\n";
static const char err27[] = "MML変数が定義されていません。\n";
static const char err28[] = "音色ファイルか/Vオプションを指定してください。\n";
static const char err29[] = "Ｒパートが必要分定義されていません。\n";
static const char err30[] = "音色が定義されていません。\n";
static const char err31[] = "直前の音符長が圧縮または加工されています。\n";
static const char err32[] = "Rパートでタイ・スラーは使用出来ません。\n";
static const char err33[] = "可変長MML変数の定義数が256を越えました。\n";
static const char err34[] = "分散和音開始記号 {{ がありません。\n";
static const char err35[] = "分散和音コマンド中の指定が間違っています。\n";

// :8143
static const char warning_mes[] = "Warning ";
static const char not_ff_mes[] = ": 音色ファイル名が指定されていません．\n";
static const char ff_readerr_mes[] = ": 音色ファイルが読み込めません．\n";
static const char not_pmd_mes[] = ": ＰＭＤが常駐していません．\n";
static const char loop_err_mes[] = " : ループ終了記号 ] が足りません。\n";
static const char mcopt_err_mes[] = ": 環境変数 MCOPT の記述に誤りがあります。";

#if efc
// :8151
static const char usames[] =
    "Usage:  EFC [/option] filename[.EML] [filename[.FF]]\n"
    "\n"
    "Option: /V  Compile with Tonedatas\n"
    "        /VW Write Voicefile after Compile\n"
    "        /N  Compile on OPN Mode(Default)\n"
    "        /M  Compile on OPM Mode\n"
    "        /L  Compile on OPL Mode\n";
// :8158
static const char titmes[] =
    " .EML file --> .EFC file Compiler ver " ver "\n"
    "		Programmed by M.Kajihara(KAJA) " date "\n"
    "\n";
#else
// :8165
static const char usames[] =
    "Usage:  MC"
#if hyouka
    "H"
#endif
    " [/option] filename[.MML] [filename[.FF]]\n"
    "\n"
    "Option: "
#if !hyouka
    "/V  Compile with Tonedatas & Messages & Filenames\n"
    "        /VW Write Tonedata after Compile\n"
    "        "
#endif
    "/N  Compile on OPN   Mode(Default)\n"
    "        /L  Compile on OPL   Mode\n"
#if !hyouka
    "        /M  Compile on OPM   Mode\n"
    "        /T  Compile on TOWNS Mode\n"
    "        /P  Play after Compile Complete\n"
    "        /S  Not Write Compiled File & Play\n"
#endif
    "        /A  Not Set ADPCM_File before Play\n"
    "        /O  Not Put Title Messages after Play\n"
    "        /C  Calculate & Put Total Length of Parts\n";

// :8189
static const char titmes[] =
#if !hyouka
    " .MML file --> .M file Compiler"
#else
    " .MML file Compiler & Player (MC.EXE評価版)"
#endif
    " ver " ver "\n"
    "		Programmed by M.Kajihara(KAJA) " date "\n"
    "\n";
#endif

// :8199
static const char finmes[] = "Compile Completed.\n";

// LC.INC:586
static const char loop_mes[] = "\t/ Loop : ";
static const char loop_mes2[] = "\t/ Found Infinite Local Loop!\n";
static const char _crlf_mes[] = "\n";

// LC.INC:583
// NOTE: for correct behavior, this string must remain exactly the same length
// in all translations, and the 6th character (index 5) must be a placeholder
// for the part letter.
static const char default_part_mes[] = "Part  \tLength : ";
