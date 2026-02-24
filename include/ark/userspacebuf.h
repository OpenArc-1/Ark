/**
 * Userspace Output Buffer
 * Allow userspace to write output that kernel can read and display
 */

#ifndef __USERSPACEBUF_H__
#define __USERSPACEBUF_H__

#include "../include/ark/types.h"

#define USP_BUFFER_SIZE 4096

/**
 * Shared buffer structure
 * Located at fixed address accessible from both kernel and userspace
 */
typedef struct {
    char buffer[USP_BUFFER_SIZE];
    volatile u32 write_pos;
    volatile u32 read_pos;
    volatile u32 activity_flag;
} uspace_buffer_t;

/* Global buffer - place at predictable address */
extern uspace_buffer_t g_uspace_buffer;

#endif
