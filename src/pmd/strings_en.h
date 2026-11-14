// :7
#define ver "4.8s"
#define vers 0x48
#define verc "s"
#define date "Jan.22nd 2020"

// :45
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
static PMDC_UNUSED const char mes_not_board[] = "YM2608+PCM not found.\n";
#else
static PMDC_UNUSED const char mes_not_board[] = "YM2608+ADPCM not found.\n";
#endif
#else
static PMDC_UNUSED const char mes_not_board[] = "The FM sound board is not installed.\n";
#endif

// :10558
static PMDC_UNUSED const char mes_cut[] = "The resident driver has been removed.\n";

// :10560
static PMDC_UNUSED const char mes_title[] =
    "Music Driver P.M.D. for PC9801/88VA Version " ver _optnam "\n"
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
static PMDC_UNUSED const char mes_int2[] = ") is used.\n";

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
static PMDC_UNUSED const char mes_ppsdrv[] = "PPSDRV(INT64H) is supported.\n";
#if ppz
static PMDC_UNUSED const char mes_ppz8[] = "PPZ8(INT7FH) is supported.\n";
#endif
static PMDC_UNUSED const char mes_wait1[] = "FM LSI REG-DATA wait loop count: ";
static PMDC_UNUSED const char mes_wait2[] = " (approx. ";
static PMDC_UNUSED const char mes_wait3[] = "ns)\n";

// :10598
static PMDC_UNUSED const char cantcut_mes[] =
    "Resident removal is currently disabled. Release PMD-related TSRs first.\n";
static PMDC_UNUSED const char pmderror_mes1[] = "The FM interrupt vector differs from the resident one.\n";
static PMDC_UNUSED const char pmderror_mes2[] = "The music data buffer must be at least 1KB.\n";
static PMDC_UNUSED const char pmderror_mes3[] = "The music+voice+effect data must total at most 40KB.\n";
static PMDC_UNUSED const char pmderror_mes4[] = "Removal failed.\n";
static PMDC_UNUSED const char pmderror_mes5[] = "PMD is not resident.\n";
static PMDC_UNUSED const char pmderror_mes6[] = "An option that cannot be changed was specified.\n";
static PMDC_UNUSED const char pmderror_mes7[] = "Unable to allocate memory.\n";

// :10607
static PMDC_UNUSED const char mes_exit[] =
    "Resident.\n"
    "PMD can be controlled via INT 60H.\n";

// :10611
static PMDC_UNUSED const char mes_key[] = "The ESC/GRPH keys stop the music and fast-forward.\n";

// :10613
static PMDC_UNUSED const char mes_kankyo[] = "The PMDOPT environment variable is set incorrectly.\n";

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
    " [/option[value]][/option[value]]..\n"
    "Option:"
    "\t  /Mn  Music data      buffer size (KB) Def.=16\n"
    "\t  /Vn  Voice data      buffer size (KB) Def.= 8\n"
    "\t  /En  FM effect data  buffer size (KB) Def.= 4\n"
#if !va
    "\t  /On  FM sound source port 0=088H 1=188H 2=288H 3=388H Def.=auto\n"
#endif
    "\t* /DFn FM sound source volume adjust (max 0 - min 255) Def.="
#if board2
    " 0\n"
#else
    "16\n"
#endif
    "\t* /DSn SSG sound source volume adjust (max 0 - min 255) Def.= 0\n"
#if board2
    "\t* /DPn PCM sound source volume adjust (max 0 - min 255) Def.= 0\n"
#if ppz
    "\t* /DZn PPZ8              volume adjust (max 0 - min 255) Def.= 0\n"
#endif
    "\t* /DRn RHYTHM sound source volume adjust (max 0 - min 255) Def.= 0\n"
    "\t* /N(-)Do not play RHYTHM together with the SSG drum (do play)\n"
#endif
    "\t* /K(-)Disable stopping/fast-forwarding with ESC/GRPH keys (enable)\n"
#if va
    "\t* /KEn Key used with MSTOP/ESC Def.=4\n"
    "\t* /KGn Key used with FF   /GRPH Def.=4\n"
    "\t* /KRn Key used with REW  /GRPH Def.=2\n"
    "\t\tValues: 1=KANA 2=SHIFT 4=CTRL (add to combine)\n"
#else
    "\t* /KEn Key used with MSTOP/ESC Def.=16\n"
    "\t* /KGn Key used with FF   /GRPH Def.=16\n"
    "\t* /KRn Key used with REW  /GRPH Def.=1\n"
    "\t\tValues: 1=SHIFT 2=CAPS 4=KANA 16=CTRL (add to combine)\n"
#endif
    "\t* /F(-)Do not stop the music after fadeout (do stop)\n"
    "\t* /I(-)Disable interrupts during FM/INT60 interrupts (do not)\n"
    "\t* /P(-)Do not support PPSDRV (support it if resident)\n"
#if ppz
    "\t* /Z(-)Do not support PPZ8 (support it if resident)\n"
#endif
    "\t* /Wn  FM REG-DATA wait count (1-255, auto when omitted)\n"
    "\t* /Gn  TimerB value for GRPH fast-forward Def.=250\n"
#if board2 && adpcm && !ademu
    "\t* /An  ADPCM speed 0=fast 1=medium 2=slow Def.=Auto\n"
#endif
#if board2 && pcm
    "\t* /S(-)Match the PCM part spec to PMDB2 (do not match)\n"
    "\t* /HZn PCM frequency (0-8,4.13/5.52/8.27/11.03/16.54/22.05/33.08/44.10/auto)\n"
#endif
    "\t  /R   Remove the resident driver\t\t/H   Show help\n"
    "\t(* options can be changed after residency, - is the value in parentheses)";

// :10692
static PMDC_UNUSED const char changemes_0[] = "Settings changed.\n";
static PMDC_UNUSED const char crlf[] = "\n";
static PMDC_UNUSED const char changemes_01[] =
    "  ----- Current settings -----\n"
    "・Music       data buffer size:";
static PMDC_UNUSED const char changemes_02[] = "KB\n・Voice       data buffer size:";
static PMDC_UNUSED const char changemes_03[] = "KB\n・Effect      data buffer size:";
static PMDC_UNUSED const char changemes_1[] = "KB\n・FM sound     volume adjust:";
static PMDC_UNUSED const char changemes_2[] = "・SSG sound    volume adjust:";
#if board2
static PMDC_UNUSED const char changemes_3[] = "・PCM sound    volume adjust:";
#if ppz
static PMDC_UNUSED const char changemes_3z[] = "・PPZ8         volume adjust:";
#endif
static PMDC_UNUSED const char changemes_4[] = "・RHYTHM sound volume adjust:";
static PMDC_UNUSED const char changemes_5[] = "・Mute RHYTHM while the SSG drum plays: ";
static PMDC_UNUSED const char changemes_5a[] = "off\n";
static PMDC_UNUSED const char changemes_5b[] = "on\n";
#endif
static PMDC_UNUSED const char changemes_6[] = "・Fast-forward/music stop with GRPH/ESC keys: ";
static PMDC_UNUSED const char changemes_6a[] = "enabled\n";
static PMDC_UNUSED const char changemes_6b[] = "disabled\n";
static PMDC_UNUSED const char changemes_7[] = "・Key used with MSTOP/ESC: ";
static PMDC_UNUSED const char changemes_8[] = "・Key used with FF   /GRPH: ";
static PMDC_UNUSED const char changemes_8b[] = "・Key used with REW  /GRPH: ";
static PMDC_UNUSED const char changemes_9[] = "・Stop the music after a fadeout: ";
static PMDC_UNUSED const char changemes_10[] = "・Disable interrupts during FM/INT60: ";
static PMDC_UNUSED const char changemes_11[] = "・Use PPSDRV: ";
static PMDC_UNUSED const char changemes_11_[] = "・Use PPZ8: ";
static PMDC_UNUSED const char changemes_12[] = "・FM LSI REG-DATA wait loop count: ";
static PMDC_UNUSED const char changemes_12a[] = " (approx. ";
static PMDC_UNUSED const char changemes_12b[] = "ns)\n";
static PMDC_UNUSED const char changemes_13[] = "・TimerB value for GRPH fast-forward: ";
#if board2 && adpcm && !ademu
static PMDC_UNUSED const char changemes_14[] = "・ADPCM speed: ";
static PMDC_UNUSED const char changemes_14a[] = "fast\n";
static PMDC_UNUSED const char changemes_14b[] = "medium\n";
static PMDC_UNUSED const char changemes_14c[] = "slow\n";
#endif
#if board2 && pcm
static PMDC_UNUSED const char changemes_15[] = "・Match the PCM part spec to PMDB2: ";
static PMDC_UNUSED const char changemes_15a[] = "no\n";
static PMDC_UNUSED const char changemes_15b[] = "yes\n";
static PMDC_UNUSED const char changemes_16[] = "・PCM playback frequency: ";
static PMDC_UNUSED const char *const changemes_16a[] = {
    "4.13438", "5.51250", "8.26875", "11.0250", "16.5375",
    "22.0500", "33.0750", "44.1000", "auto\n",
};
static PMDC_UNUSED const char changemes_16b[] = "KHz fixed\n";
#endif
