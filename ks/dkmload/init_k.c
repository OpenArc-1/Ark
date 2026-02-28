#include "ark/dkm.h"

/* kernel initialization for the dynamic module loader */
void dkm_init(void); /* forward */

/* called from gen/init.c */
void dkm_init_kernel(void) {
    dkm_init();
}
