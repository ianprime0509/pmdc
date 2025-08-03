#include "mc.h"
#include "mc_stdio.h"

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

    struct mc mc;
    mc.sys = &mc_sys_stdio;
    mc_init(&mc);
    mc_main(&mc, cmdline);

    return 0;
}
