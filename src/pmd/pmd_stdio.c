#include "pmd.h"

#include <stdio.h>
#include <stdlib.h>

static void pmd_stdio_putc(char c, void *user_data) {
    (void)user_data;
    fputc(c, stdout);
}

static void pmd_stdio_print(const char *mes, void *user_data) {
    (void)user_data;
    fprintf(stdout, "%s", mes);
}

PMDC_NORETURN static void pmd_stdio_exit(int status, void *user_data) {
    (void)user_data;
    exit(status);
}

static char *pmd_stdio_getenv(const char *name, void *user_data) {
    (void)user_data;
    return getenv(name);
}

const struct pmd_sys pmd_sys_stdio = (struct pmd_sys){
    .putc = pmd_stdio_putc,
    .print = pmd_stdio_print,
    .exit = pmd_stdio_exit,
    .getenv = pmd_stdio_getenv,
};
