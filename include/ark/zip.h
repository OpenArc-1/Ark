/**
 * Minimal ZIP parser for Ark initramfs
 *
 * Loads a zip file (e.g. from multiboot module) into ramfs.
 * Supports stored (uncompressed) entries only (method 0).
 */

#pragma once

#include "ark/types.h"

/**
 * Parse zip data and add each stored file into ramfs.
 * Paths are normalized to start with / (e.g. "init" -> "/init", "bin/sh" -> "/bin/sh").
 *
 * @param data Pointer to zip file data
 * @param size Size of zip data in bytes
 * @return Number of files added to ramfs, or 0 on error
 */
u32 zip_load_into_ramfs(const u8 *data, u32 size);
