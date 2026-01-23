/**
 * Ark kernel basic type definitions.
 *
 * These are intentionally minimal and self-contained so the kernel
 * does not depend on a hosted C library's stdint.h.
 */

#pragma once

typedef signed char         i8;
typedef unsigned char       u8;

typedef short               i16;
typedef unsigned short      u16;

typedef int                 i32;
typedef unsigned int        u32;

typedef long long           i64;
typedef unsigned long long  u64;

typedef u64                 usize;
typedef i64                 isize;

typedef u8                  bool;

#ifndef true
#define true  ((bool)1)
#endif

#ifndef false
#define false ((bool)0)
#endif

#define NULL ((void *)0)

