#include "pmd.h"

#include <assert.h>
#include <string.h>

// :21
#ifndef va
#define va 0
#endif
#ifndef board2
#define board2 0
#endif
#ifndef adpcm
#define adpcm 0
#endif
#ifndef ademu
#define ademu 0
#endif
#ifndef pcm
#define pcm 0
#endif
#ifndef ppz
#define ppz 0
#endif
#ifndef sync
#define sync 0
#endif
#ifndef vsync
#define vsync 0
#endif

#if lang == 256 * 'e' + 'n'
#include "strings_en.h"
#else
#include "strings_jp.h"
#endif

// The delay loops are calibrated CPU loops (WAIT.INC) that are intentionally
// not implemented; they are no-delay points until the call sites land.
// :218
static PMDC_UNUSED void _wait(struct pmd *pmd);
// :223
static PMDC_UNUSED void _waitP(struct pmd *pmd);
// :230
static PMDC_UNUSED void _rwait(struct pmd *pmd);

// :218
static void _wait(struct pmd *pmd) {
    (void)pmd;
    // NOTE: WAIT.INC wait_clock calibration intentionally not implemented.
}

// :223
static void _waitP(struct pmd *pmd) {
    (void)pmd;
    // NOTE: WAIT.INC wait_clock calibration intentionally not implemented.
}

// :230
static void _rwait(struct pmd *pmd) {
    (void)pmd;
    // NOTE: WAIT.INC wait_clock calibration intentionally not implemented.
}

void pmd_init(struct pmd *pmd) {
#define lengthof(arr) (sizeof((arr)) / sizeof((arr)[0]))

    // :10567
    static_assert(lengthof(default_mes_int) <= lengthof(pmd->mes_int),
                  "mes_int is too small for default_mes_int");
    memcpy(pmd->mes_int, default_mes_int, sizeof(default_mes_int));
    // :10578
    static_assert(lengthof(default_mes_ongen1) <= lengthof(pmd->mes_ongen1),
                  "mes_ongen1 is too small for default_mes_ongen1");
    memcpy(pmd->mes_ongen1, default_mes_ongen1, sizeof(default_mes_ongen1));

#undef lengthof

    // The rest of the global state is filled in when the global fields are
    // defined (phase 2).
}

// :8471
void pmd_resident_main(struct pmd *pmd, char *cmdline) {
    (void)pmd;
    (void)cmdline;
    // Filled in when option parsing and the hardware probe are translated
    // (phase 3).
}

// :9490
void pmd_change_main(struct pmd *pmd, char *cmdline) {
    (void)pmd;
    (void)cmdline;
    // Filled in when the status option machinery is translated (phase 3).
}
