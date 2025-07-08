// These English translations were produced by Mana, Pigu and VasteelXolotl.
// Line numbers refer to the original MC source code, not the translated version.

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
static const char err00[] = "The specified information is incorrect.\n";
static const char err01[] = "Invalid character in MML.\n";
static const char err02[] = "Specified value is wrong.\n";
static const char err03[] = "MML file could not be read.\n";
static const char err04[] = "MML file could not be written.\n";
static const char err05[] = "FF file could not be written.\n";
static const char err06[] = "Parameter not specified.\n";
static const char err07[] = "Specified character cannot be used.\n";
static const char err08[] = "Specified length is too long.\n";
static const char err09[] = "Missing portamento end symbol }.\n";
static const char err10[] = "Notes are required after L command.\n";
static const char err11[] = "Hardware LFO cannot be used in SFX part.\n";
static const char err12[] = "Tempo command cannot be used in SFX part.\n";
static const char err13[] = "Missing portamento start symbol {.\n";
static const char err14[] = "Incorrect specification of portamento command.\n";
static const char err15[] = "Rest found within portamento command.\n";
static const char err16[] = "Specify it right after pitch command.\n";
static const char err17[] = "This command cannot be used here.\n";
static const char err18[] = "MML file size is too large.\n";
static const char err19[] = "Size after compilation is too large.\n";
static const char err20[] = "Note length cannot exceed 255 ticks during W/S commands.\n";
static const char err21[] = "Specified note length is invalid.\n";
static const char err22[] = "Tie is not specified right after pitch.\n";
static const char err23[] = "Missing loop start symbol [.\n";
static const char err24[] = "There are no notes in the infinite loop.\n";
static const char err25[] = "There are two escape symbols in one loop.\n";
static const char err26[] = "Pitch exceeds the limit.\n";
static const char err27[] = "MML macro is not defined.\n";
static const char err28[] = "Please specify the /V option or sound file.\n";
static const char err29[] = "The required R pattern is undefined.\n";
static const char err30[] = "Instrument is not defined.\n";
static const char err31[] = "Previous note length compressed or altered.\n";
static const char err32[] = "Ties and slurs not allowed in R pattern.\n";
static const char err33[] = "Numer of macros defined exceeds 256.\n";
static const char err34[] = "Missing arpeggio start symbol {{.\n";
static const char err35[] = "Incorrect specification of arpeggio command.\n";

// :8143
static const char warning_mes[] = "Warning: ";
static const char not_ff_mes[] = "Instrument file name not specified.\n";
static const char ff_readerr_mes[] = "Instrument file cannot be read.\n";
static const char not_pmd_mes[] = "PMD is not resident.n";
static const char loop_err_mes[] = "Insufficient loop end symbols ].\n";
static const char mcopt_err_mes[] = "Error in description of MCOPT EnvVar.";

#if efc
// :8151
static const char usames[] =
    "Usage:  EFC [/option] filename[.EML] [filename[.FF]]\n"
    "\n"
    "Option: /V  Compile with tone data\n"
    "        /VW Write voice file after compiling\n"
    "        /N  Compile on OPN Mode (Default)\n"
    "        /M  Compile on OPM Mode\n"
    "        /L  Compile on OPL Mode\n";
// :8158
static const char titmes[] =
    " .EML file --> .EFC file Compiler ver " ver "\n"
    "		Programmed by M.Kajihara(KAJA) " date "\n"
    "		English version by Mana, Pigu and VasteelXolotl \n"
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
    "/V  Include used instruments with compiled song\n"
    "        /VW Write used instruments to separate instrument file\n"
    "        "
#endif
    "/N  Compile on OPN   Mode (Default)\n"
    "        /L  Compile on OPL   Mode\n"
#if !hyouka
    "        /M  Compile on OPM   Mode\n"
    "        /T  Compile on TOWNS Mode\n"
    "        /P  Play after compilation\n"
    "        /S  Play without saving file\n"
#endif
    "        /A  Ignore specified ADPCM\n"
    "        /O  Don't display title during playback\n"
    "        /C  Calculate and print total length of parts\n";

// :8189
static const char titmes[] =
#if !hyouka
    " .MML file --> .M file Compiler"
#else
    " .MML file Compiler & Player (MC.EXE評価版)"
#endif
    " ver " ver "\n"
    "		Programmed by M.Kajihara(KAJA) " date "\n"
    "		English version by Mana, Pigu and VasteelXolotl \n"
    "\n";
#endif

// :8199
static const char finmes[] = "Compilation completed.\n";

// LC.INC:586
static const char loop_mes[] = "\t/ Loop : ";
static const char loop_mes2[] = "\t/ Found Infinite Local Loop!\n";
static const char _crlf_mes[] = "\n";

// LC.INC:583
// NOTE: for correct behavior, this string must remain exactly the same length
// in all translations, and the 6th character (index 5) must be a placeholder
// for the part letter.
static const char default_part_mes[] = "Part  \tLength : ";
