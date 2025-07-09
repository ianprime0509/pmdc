#include "mc.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>

void mc_sys_putc(char c, void *user_data) {
    (void)user_data;
    fputc(c, stdout);
}

void mc_sys_print(const char *mes, void *user_data) {
    (void)user_data;
    fprintf(stdout, "%s", mes);
}

void *mc_sys_create(const char *filename, void *user_data) {
    (void)user_data;
    return fopen(filename, "wb");
}

void *mc_sys_open(const char *filename, void *user_data) {
    (void)user_data;
    return fopen(filename, "rb");
}

int mc_sys_close(void *file, void *user_data) {
    (void)user_data;
    return fclose(file);
}

int mc_sys_read(void *file, void *dest, uint16_t n, uint16_t *read, void *user_data) {
    (void)user_data;
    *read = fread(dest, 1, n, file);
    return ferror(file);
}

int mc_sys_write(void *file, void *data, uint16_t n, void *user_data) {
    (void)user_data;
    return fwrite(data, 1, n, file) < n;
}

noreturn void mc_sys_exit(int status, void *user_data) {
    (void)user_data;
    exit(status);
}

char *mc_sys_getenv(const char *name, void *user_data) {
    (void)user_data;
    return getenv(name);
}
