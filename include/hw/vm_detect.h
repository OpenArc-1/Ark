/**
 * hw/vm_detect.h — Virtual machine detection for Ark
 *
 * Call vm_detect() once early in arch entry (after serial_init).
 * Use the returned vm_info_t to decide graphics configuration:
 *
 *   vm_info_t vm;
 *   vm_detect(&vm);
 *   if (vm.is_vm) {
 *       // use vm.fb_w, vm.fb_h — guaranteed safe BGA values
 *   }
 */

#pragma once
#include "ark/types.h"

typedef enum {
    VM_NONE     = 0,   /* Real hardware                         */
    VM_QEMU     = 1,   /* QEMU (with or without KVM)            */
    VM_VBOX     = 2,   /* VirtualBox                            */
    VM_VMWARE   = 3,   /* VMware                                */
    VM_HYPERV   = 4,   /* Hyper-V / Microsoft                   */
    VM_KVM      = 5,   /* KVM bare (no QEMU userspace detected) */
    VM_UNKNOWN  = 6,   /* Hypervisor bit set, vendor unknown    */
} vm_type_t;

typedef struct {
    bool       is_vm;        /* True if any VM detected               */
    vm_type_t  type;         /* Which hypervisor                      */
    const char *name;        /* Human-readable name string            */

    /* Recommended framebuffer resolution for this VM.
     * Only valid when is_vm == true.
     * These are known-good BGA values for each hypervisor. */
    u32        fb_w;
    u32        fb_h;
    u32        fb_bpp;

    /* True if a Bochs VBE (BGA) adapter is present.
     * QEMU and VirtualBox both expose BGA; VMware/Hyper-V do not. */
    bool       has_bga;
} vm_info_t;

/**
 * vm_detect() — detect hypervisor via CPUID.
 *
 * Uses CPUID leaf 0x40000000 (hypervisor vendor string) and leaf 1
 * (hypervisor present bit). Safe to call before framebuffer setup.
 * Writes results into *out.
 */
void vm_detect(vm_info_t *out);

/**
 * vm_type_name() — return a constant string for a vm_type_t value.
 */
const char *vm_type_name(vm_type_t t);
