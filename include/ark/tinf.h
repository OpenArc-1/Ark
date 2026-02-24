/**
 * Minimal DEFLATE inflate for ZIP method 8 (tinf)
 * Used by fs/zip.c to extract compressed initramfs entries.
 */
#pragma once

#define TINF_OK          0
#define TINF_DATA_ERROR -1
#define TINF_BUF_ERROR  -2

/**
 * Decompress raw DEFLATE data (no zlib header) into dest.
 * @param dest       Output buffer
 * @param destLen    In: size of dest; Out: number of bytes written
 * @param source     Compressed data
 * @param sourceLen  Length of compressed data
 * @return TINF_OK on success, TINF_DATA_ERROR or TINF_BUF_ERROR on failure
 */
int tinf_uncompress(void *dest, unsigned int *destLen,
                    const void *source, unsigned int sourceLen);
