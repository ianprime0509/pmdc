#include "pmd.h"
#include "pmd_stdio.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    (void)argc;

    char cmdline[128];
    size_t cmdline_len = 0;
    while (*++argv != NULL) {
        size_t arglen = strlen(*argv);
        if (cmdline_len + arglen + 1 > sizeof(cmdline)) {
            fprintf(stderr, "command line too long\n");
            return 1;
        }
        memcpy(cmdline + cmdline_len, *argv, arglen);
        cmdline_len += arglen;
    }
    cmdline[cmdline_len] = 0;

    static struct pmd pmd;
    pmd.sys = &pmd_sys_stdio;
    pmd.hw = NULL;
    pmd.pps = NULL;
    pmd.ppz8 = NULL;
    pmd.p86 = NULL;
    pmd_init(&pmd);
    pmd_resident_main(&pmd, cmdline);

    return 0;
}
