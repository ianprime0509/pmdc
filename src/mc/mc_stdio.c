#include "mc.h"

#include <stdio.h>
#include <stdlib.h>

static void mc_stdio_putc(char c, void *user_data) {
    (void)user_data;
    fputc(c, stdout);
}

static void mc_stdio_print(const char *mes, void *user_data) {
    (void)user_data;
    fprintf(stdout, "%s", mes);
}

static void *mc_stdio_create(const char *filename, void *user_data) {
    (void)user_data;
    return fopen(filename, "wb");
}

static void *mc_stdio_open(const char *filename, void *user_data) {
    (void)user_data;
    return fopen(filename, "rb");
}

static int mc_stdio_close(void *file, void *user_data) {
    (void)user_data;
    return fclose(file);
}

static int mc_stdio_read(void *file, void *dest, uint16_t n, uint16_t *read, void *user_data) {
    (void)user_data;
    *read = fread(dest, 1, n, file);
    return ferror(file);
}

static int mc_stdio_write(void *file, void *data, uint16_t n, void *user_data) {
    (void)user_data;
    return fwrite(data, 1, n, file) < n;
}

PMDC_NORETURN static void mc_stdio_exit(int status, void *user_data) {
    (void)user_data;
    exit(status);
}

static char *mc_stdio_getenv(const char *name, void *user_data) {
    (void)user_data;
    return getenv(name);
}

const struct mc_sys mc_sys_stdio = (struct mc_sys){
    .putc = mc_stdio_putc,
    .print = mc_stdio_print,
    .create = mc_stdio_create,
    .open = mc_stdio_open,
    .close = mc_stdio_close,
    .read = mc_stdio_read,
    .write = mc_stdio_write,
    .exit = mc_stdio_exit,
    .getenv = mc_stdio_getenv,
};
