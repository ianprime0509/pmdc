#include "pmd.h"
#include "pmd_stdio.h"
#include "pmd_ymfm.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    (void)argc;

    char cmdline[128];
    size_t cmdline_len = 0;
    while (*++argv != NULL) {
        size_t arglen = strlen(*argv);
        size_t separator_len = cmdline_len != 0 ? 1 : 0;
        if (cmdline_len + separator_len + arglen + 1 > sizeof(cmdline)) {
            fprintf(stderr, "command line too long\n");
            return 1;
        }
        if (separator_len != 0) cmdline[cmdline_len++] = ' ';
        memcpy(cmdline + cmdline_len, *argv, arglen);
        cmdline_len += arglen;
    }
    cmdline[cmdline_len] = 0;

    static struct pmd pmd;
    pmd.sys = &pmd_sys_stdio;
    pmd.pps = NULL;
    pmd.ppz8 = NULL;
    pmd.p86 = NULL;

    struct pmd_ymfm_config config;
    pmd_ymfm_default_config(&config, &pmd);
    struct pmd_ymfm *ymfm = pmd_ymfm_create(&config);
    if (ymfm == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    pmd.hw = &pmd_hw_ymfm;
    pmd.user_data = ymfm;

    // The rhythm samples are board ROM contents, not part of the chip, so the
    // host supplies them from the working directory.
    pmd_ymfm_load_adpcm_a_file(ymfm, "ym2608_adpcm_rom.bin");

    pmd_init(&pmd);
    pmd_resident_main(&pmd, cmdline);

    pmd_ymfm_destroy(ymfm);
    return 0;
}
