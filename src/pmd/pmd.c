#include "pmd.h"

// :218
static void _wait(struct pmd *pmd);
// :223
static void _waitP(struct pmd *pmd);
// :230
static void _rwait(struct pmd *pmd);

// :218
static void _wait(struct pmd *pmd) {
    (void)pmd;
    // NOTE: wait_clock delay intentionally not implemented for now
}

// :223
static void _waitP(struct pmd *pmd) {
    (void)pmd;
    // NOTE: wait_clock delay intentionally not implemented for now
}

// :230
static void _rwait(struct pmd *pmd) {
    (void)pmd;
    // NOTE: wait_clock delay intentionally not implemented for now
}
