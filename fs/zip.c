/**
 * Minimal ZIP parser - loads stored (method 0) and deflated (method 8) entries into ramfs.
 * ZIP layout: [local headers + data] ... [central directory] [end of central dir]
 */

#include "ark/zip.h"
#include "ark/ramfs.h"
#include "ark/printk.h"
#include "ark/types.h"
#include "ark/tinf.h"

#define LOCAL_SIG   0x04034b50u
#define CENTRAL_SIG 0x02014b50u
#define EOCD_SIG    0x06054b50u

#define RAMFS_MAX_FILENAME 256
#define INITRAMFS_HEAP_SIZE (2 * 1024 * 1024)  /* 2 MiB for decompressed initramfs */

static u8 path_buf[RAMFS_MAX_FILENAME];
static u8 initramfs_heap[INITRAMFS_HEAP_SIZE];
static u32 initramfs_heap_used;

/* Allocate from initramfs heap; returns NULL if not enough space. 4-byte aligned. */
static u8 *initramfs_alloc(u32 size) {
    u32 align = (size + 3) & ~3u;
    if (initramfs_heap_used + align > INITRAMFS_HEAP_SIZE)
        return NULL;
    u8 *p = initramfs_heap + initramfs_heap_used;
    initramfs_heap_used += align;
    return p;
}

/* Normalize zip path to ramfs path: ensure leading /, no leading ./
   "init" -> "/init", "bin/sh" -> "/bin/sh" */
static void normalize_path(const char *name, u32 name_len) {
    u32 i = 0, j = 0;
    if (name_len >= RAMFS_MAX_FILENAME)
        name_len = RAMFS_MAX_FILENAME - 1;
    while (i < name_len && (name[i] == '.' && name[i + 1] == '/'))
        i += 2;
    if (j < RAMFS_MAX_FILENAME - 1)
        path_buf[j++] = '/';
    while (i < name_len && j < RAMFS_MAX_FILENAME - 1) {
        char c = name[i++];
        if (c == '\\')
            c = '/';
        if (c == '/' && j > 0 && path_buf[j - 1] == '/')
            continue;
        path_buf[j++] = c;
    }
    if (j > 1 && path_buf[j - 1] == '/')
        j--;
    path_buf[j] = '\0';
}

u32 zip_load_into_ramfs(const u8 *data, u32 size) {
    if (!data || size < 22)
        return 0;

    /* Find End of Central Directory (last occurrence in last 64K) */
    u32 search_start = size > 65557 ? size - 65557 : 0;
    u32 eocd_off = 0;
    for (u32 i = search_start; i + 22 <= size; i++) {
        if (data[i] == 0x50 && data[i + 1] == 0x4b && data[i + 2] == 0x05 && data[i + 3] == 0x06)
            eocd_off = i;
    }
    if (eocd_off == 0) {
        printk(T, "zip: EOCD not found\n");
        return 0;
    }

    u16 cd_entries = (u16)data[eocd_off + 8] | ((u16)data[eocd_off + 9] << 8);
    u32 cd_size   = data[eocd_off + 12] | (data[eocd_off + 13] << 8) | (data[eocd_off + 14] << 16) | (data[eocd_off + 15] << 24);
    u32 cd_offset = data[eocd_off + 16] | (data[eocd_off + 17] << 8) | (data[eocd_off + 18] << 16) | (data[eocd_off + 19] << 24);

    printk(T, "zip: central dir at %u, %u entries\n", cd_offset, (u32)cd_entries);

    u32 loaded = 0;
    u32 pos = cd_offset;

    for (u32 n = 0; n < cd_entries && pos + 46 <= size; n++) {
        if (data[pos] != 0x50 || data[pos + 1] != 0x4b || data[pos + 2] != 0x01 || data[pos + 3] != 0x02) {
            printk(T, "zip: bad central header at %u\n", pos);
            break;
        }

        u16 method = (u16)data[pos + 10] | ((u16)data[pos + 11] << 8);
        u32 compressed = data[pos + 20] | (data[pos + 21] << 8) | (data[pos + 22] << 16) | (data[pos + 23] << 24);
        u32 uncompressed = data[pos + 24] | (data[pos + 25] << 8) | (data[pos + 26] << 16) | (data[pos + 27] << 24);
        u16 fn_len = (u16)data[pos + 28] | ((u16)data[pos + 29] << 8);
        u16 extra_len = (u16)data[pos + 30] | ((u16)data[pos + 31] << 8);
        u16 comment_len = (u16)data[pos + 32] | ((u16)data[pos + 33] << 8);
        u32 local_off = data[pos + 42] | (data[pos + 43] << 8) | (data[pos + 44] << 16) | (data[pos + 45] << 24);

        pos += 46 + fn_len + extra_len + comment_len;

        /* Skip directories (trailing /) */
        if (fn_len == 0)
            continue;
        if (method != 0 && method != 8) {
            printk(T, "zip: skip unsupported method %u\n", (u32)method);
            continue;
        }

        /* Local file header at local_off */
        if (local_off + 30 + fn_len + extra_len + compressed > size) {
            printk(T, "zip: entry out of range\n");
            continue;
        }
        if (data[local_off] != 0x50 || data[local_off + 1] != 0x4b || data[local_off + 2] != 0x03 || data[local_off + 3] != 0x04) {
            printk(T, "zip: bad local header at %u\n", local_off);
            continue;
        }

        const char *fn = (const char *)&data[local_off + 30];
        normalize_path(fn, fn_len);

        u32 payload_off = local_off + 30 + fn_len + extra_len;
        const u8 *payload = (const u8 *)data + payload_off;
        if (payload_off + compressed > size)
            continue;

        if (method == 0) {
            /* Stored: use payload directly */
            if (ramfs_add_file((const char *)path_buf, (u8 *)payload, compressed)) {
                loaded++;
                printk(T, "zip: added %s (%u bytes)\n", path_buf, compressed);
            }
        } else {
            /* Method 8 (deflate): decompress into heap then add */
            u8 *out = initramfs_alloc(uncompressed);
            if (!out) {
                printk(T, "zip: no heap for %s (%u bytes uncompressed)\n", path_buf, uncompressed);
                continue;
            }
            {
                unsigned int out_len = uncompressed;
                int ret = tinf_uncompress(out, &out_len, payload, compressed);
                if (ret != TINF_OK) {
                    printk(T, "zip: inflate failed for %s (err %d)\n", path_buf, ret);
                    continue;
                }
                if (ramfs_add_file((const char *)path_buf, out, (u32)out_len)) {
                    loaded++;
                    printk(T, "zip: extracted %s (%u -> %u bytes)\n", path_buf, compressed, (u32)out_len);
                }
            }
        }
    }

    printk(T, "zip: loaded %u files into ramfs\n", loaded);
    return loaded;
}
