#include "ark/types.h"
#include "ark/syscall.h"   /* for syscall numbers shared userspace/kernel */
#include "ark/dkm.h"

/* kernel syscall handlers for dynamic modules */

/* these functions are referenced from gen/syscall.c via new syscall numbers */

u32 dkm_syscall_load(const char *path) {
    return (u32)dkm_sys_load(path);
}

u32 dkm_syscall_unload(const char *name) {
    return (u32)dkm_sys_unload(name);
}

u32 dkm_syscall_list(void) {
    dkm_sys_list();
    return 0;
}
