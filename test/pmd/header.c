#include "pmd.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Compiling this file for each variant's flag set checks that pmd.h is
// self-contained and consistent under those flags.
static struct pmd header_check_pmd;

int pmd_header_check_probe(void) {
    struct pmd_qq *part = &header_check_pmd.parts[0];
    header_check_pmd.mmlbuf = header_check_pmd.dataarea + 1;
    part->pat = 1;
    part->eenv_ar = 2;
    part->address = header_check_pmd.mmlbuf + 1;
    header_check_pmd.part_data_table[0] = part;
    return (int)(part->address - header_check_pmd.mmlbuf) +
           (int)part->penv + (int)header_check_pmd.part_data_table[0]->leng +
           (int)sizeof(header_check_pmd.dataarea) + (int)sizeof(struct pmd_qq) +
           (int)sizeof(union pmd_pcm_table *);
}
