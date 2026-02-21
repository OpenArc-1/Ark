/**
 * script.c - Kernel Script Scanner for Ark
 *
 * Scans ramfs for scripts tagged with #! and executes them.
 * Supports #!init tags and file:/ directives for binary loading.
 */

#include "ark/types.h"
#include "ark/printk.h"
#include "ark/ramfs.h"
#include "ark/elf_loader.h"
#include "ark/input.h"
#include "ark/init_api.h"

/* Forward declarations from keyboard driver (hid/kbd100.c) */
void kbd_poll(void);
bool kbd_is_initialized(void);

#define SCRIPT_MAX_LINE 512
#define SCRIPT_SHEBANG_MAX 64

/**
 * Check if a file starts with shebang (#!)
 */
static u8 is_script_file(const u8 *data, u32 size) {
    if (size < 2)
        return 0;
    return (data[0] == '#' && data[1] == '!');
}

/**
 * Read a line from script data
 * Returns length of line (excluding newline), or 0 if no more lines
 */
static u32 read_script_line(const u8 *data, u32 size, u32 *pos, char *line, u32 max_len) {
    u32 start = *pos;
    u32 len = 0;
    
    /* Skip to start of line if we're past data */
    if (start >= size)
        return 0;
    
    /* Read until newline or end of data */
    while (start + len < size && len < max_len - 1) {
        char c = (char)data[start + len];
        if (c == '\n' || c == '\r') {
            /* Found end of line */
            if (c == '\r' && start + len + 1 < size && data[start + len + 1] == '\n') {
                len++; /* Skip \r\n */
            }
            len++; /* Include the newline */
            break;
        }
        len++;
    }
    
    /* Copy line to buffer */
    u32 i;
    for (i = 0; i < len && i < max_len - 1; i++) {
        line[i] = (char)data[start + i];
        if (line[i] == '\n' || line[i] == '\r')
            break;
    }
    line[i] = '\0';
    
    *pos = start + len;
    return i; /* Return length without newline */
}

/**
 * Parse shebang line (e.g., "#!init" or "#!/bin/init")
 * Returns 1 if it's an init script, 0 otherwise
 */
static u8 parse_shebang(const char *line) {
    if (!line || line[0] != '#' || line[1] != '!')
        return 0;
    
    /* Skip "#!" */
    const char *tag = line + 2;
    
    /* Skip whitespace */
    while (*tag == ' ' || *tag == '\t')
        tag++;
    
    /* Check for "init" tag */
    if (tag[0] == 'i' && tag[1] == 'n' && tag[2] == 'i' && tag[3] == 't') {
        /* Check if it's exactly "init" or followed by whitespace/end */
        if (tag[4] == '\0' || tag[4] == ' ' || tag[4] == '\t' || tag[4] == '\n' || tag[4] == '\r')
            return 1;
    }
    
    return 0;
}

/**
 * Parse file directive (e.g., "file:/init.bin")
 * Extracts the file path and stores it in out_path
 * Returns 1 if valid directive found, 0 otherwise
 */
static u8 parse_file_directive(const char *line, char *out_path, u32 max_path_len) {
    if (!line || !out_path)
        return 0;
    
    /* Look for "file:" prefix */
    const char *file_prefix = "file:";
    u32 i = 0;
    
    /* Skip whitespace at start */
    while (line[i] == ' ' || line[i] == '\t')
        i++;
    
    /* Check for "file:" */
    u32 j = 0;
    while (file_prefix[j] && line[i + j] == file_prefix[j])
        j++;
    
    if (j != 5) /* "file:" is 5 chars */
        return 0;
    
    i += j; /* Skip "file:" */
    
    /* Skip whitespace after "file:" */
    while (line[i] == ' ' || line[i] == '\t')
        i++;
    
    /* Check for "/" */
    if (line[i] != '/')
        return 0;
    
    /* Copy path */
    u32 path_idx = 0;
    while (line[i] && line[i] != '\n' && line[i] != '\r' && 
           line[i] != ' ' && line[i] != '\t' && path_idx < max_path_len - 1) {
        out_path[path_idx++] = line[i++];
    }
    out_path[path_idx] = '\0';
    
    return (path_idx > 0) ? 1 : 0;
}

/**
 * Scan a script file for #!init tag and file:/ directives
 * Returns the path to the binary to execute, or NULL if not found
 */
static const char *scan_script(const u8 *data, u32 size, char *out_path, u32 max_path_len) {
    if (!data || size == 0 || !out_path)
        return NULL;
    
    /* Check if it's a script */
    if (!is_script_file(data, size)) {
        return NULL;
    }
    
    u32 pos = 0;
    char line[SCRIPT_MAX_LINE];
    u8 found_init_tag = 0;
    
    /* Read first line (shebang) */
    if (read_script_line(data, size, &pos, line, SCRIPT_MAX_LINE) > 0) {
        if (parse_shebang(line)) {
            found_init_tag = 1;
            printk(T,"Found #!init tag\n");
        }
    }
    
    /* If not an init script, skip */
    if (!found_init_tag)
        return NULL;
    
    /* Read remaining lines looking for file:/ directive */
    while (pos < size) {
        u32 line_len = read_script_line(data, size, &pos, line, SCRIPT_MAX_LINE);
        if (line_len == 0)
            break;
        
        /* Try to parse file:/ directive */
        if (parse_file_directive(line, out_path, max_path_len)) {
            printk(T,"Found file:/ directive: %s\n", out_path);
            return out_path;
        }
    }
    
    return NULL;
}

/**
 * Scan all files in ramfs for init scripts and execute them
 * Uses ark/printk, input, and hid/kbd100 as requested
 */
u8 script_scan_and_execute(void) {
    printk(T,"Scanning ramfs for #!init scripts...\n");
    
    /* List files for debugging */
    ramfs_list_files();
    
    /* Get file count and iterate through all files */
    u32 file_count = ramfs_get_file_count();
    printk(T,"Scanning %u files in ramfs...\n", file_count);
    
    u8 found_script = 0;
    char binary_path[256];
    
    /* Iterate through all files in ramfs */
    for (u32 i = 0; i < file_count; i++) {
        char filename[RAMFS_MAX_FILENAME];
        u8 *file_data = NULL;
        u32 file_size = 0;
        
        if (ramfs_get_file_by_index(i, filename, &file_data, &file_size)) {
            if (file_data && file_size > 0) {
                printk(T,"Checking file: %s (%u bytes)\n", filename, file_size);
                
                /* Scan the file for #!init script */
                if (scan_script(file_data, file_size, binary_path, sizeof(binary_path))) {
                    printk(T,"Found #!init script in %s\n", filename);
                    printk(T,"Binary to execute: %s\n", binary_path);
                    
                    /* Get the binary file */
                    u32 binary_size = 0;
                    u8 *binary_data = ramfs_get_file(binary_path, &binary_size);
                    
                    if (binary_data && binary_size > 0) {
                        printk(T,"Loading binary: %s (%u bytes)\n", binary_path, binary_size);
                        
                        /* Poll keyboard to ensure it's ready (input already initialized in kernel_main) */
                        kbd_poll();
                        if (kbd_is_initialized()) {
                            printk(T,"Keyboard ready\n");
                        }
                        
                        /* Execute the binary using the bin system loader */
                        printk(T,"Executing binary via elf_execute...\n");
                        int exit_code = elf_execute(binary_data, binary_size, ark_kernel_api());
                        
                        printk(T,"Binary execution completed with exit code: %d\n", exit_code);
                        found_script = 1;
                        
                        return 1; /* Success - found and executed script */
                    } else {
                        printk(T,"ERROR: Binary file not found: %s\n", binary_path);
                    }
                }
            }
        }
    }
    
    if (!found_script) {
        printk(T,"No #!init scripts found in ramfs\n");
    }
    
    return found_script;
}
