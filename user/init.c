/**
 * init.c - Ark userspace init system
 * 
 * Provides interactive shell with file operations and text editor
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <ark/printk.h>

/* ═══════════════════════════════════════════════════════════════
 * Text Editor Implementation
 * ═══════════════════════════════════════════════════════════════ */

#define MAX_LINES 1000
#define MAX_LINE_LEN 256

typedef struct {
    char *lines[MAX_LINES];
    int line_count;
    char filename[256];
    int modified;
} editor_t;

editor_t editor = {0};

void editor_init(const char *filename) {
    memset(&editor, 0, sizeof(editor));
    strncpy(editor.filename, filename, 255);
}

int editor_read_file(void) {
    int fd = open(editor.filename, O_RDONLY);
    if (fd < 0) {
        return 0;  /* New file */
    }

    char buffer[MAX_LINE_LEN];
    ssize_t bytes;
    
    while ((bytes = read(fd, buffer, MAX_LINE_LEN - 1)) > 0) {
        if (editor.line_count >= MAX_LINES) break;
        
        buffer[bytes] = '\0';
        char *nl = strchr(buffer, '\n');
        if (nl) *nl = '\0';
        
        size_t len = strlen(buffer) + 1;
        editor.lines[editor.line_count] = malloc(len);
        if (!editor.lines[editor.line_count]) {
            close(fd);
            return -1;
        }
        strcpy(editor.lines[editor.line_count], buffer);
        editor.line_count++;
    }
    
    close(fd);
    return 0;
}

int editor_write_file(void) {
    int fd = open(editor.filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("Error: cannot write file\n");
        return -1;
    }

    for (int i = 0; i < editor.line_count; i++) {
        if (write(fd, editor.lines[i], strlen(editor.lines[i])) < 0) {
            close(fd);
            return -1;
        }
        if (write(fd, "\n", 1) < 0) {
            close(fd);
            return -1;
        }
    }

    close(fd);
    editor.modified = 0;
    return 0;
}

void editor_list_lines(void) {
    if (editor.line_count == 0) {
        printf("[empty file]\n");
        return;
    }
    for (int i = 0; i < editor.line_count; i++) {
        printf("%4d | %s\n", i+1, editor.lines[i]);
    }
}

void editor_insert_line(const char *text) {
    if (editor.line_count >= MAX_LINES) {
        printf("Error: too many lines\n");
        return;
    }
    
    size_t len = strlen(text) + 1;
    editor.lines[editor.line_count] = malloc(len);
    if (!editor.lines[editor.line_count]) {
        printf("Error: memory allocation failed\n");
        return;
    }
    
    strcpy(editor.lines[editor.line_count], text);
    editor.line_count++;
    editor.modified = 1;
}

void editor_run(const char *filename) {
    editor_init(filename);
    
    if (editor_read_file() < 0) {
        printf("Error: failed to read file\n");
        return;
    }

    char cmd[256];
    char line[MAX_LINE_LEN];

    printf("=== Text Editor ===\n");
    printf("Commands: i (insert), l (list), w (write), wq (save+exit), q (exit), h (help)\n\n");

    while (1) {
        printf(">> ");
        fflush(stdout);

        if (!fgets(cmd, 255, stdin)) break;
        
        /* Update cursor blink state */
        printk_cursor_auto_update();

        int len = strlen(cmd);
        if (len > 0 && cmd[len-1] == '\n') cmd[len-1] = '\0';

        if (len == 0) continue;

        switch (cmd[0]) {
            case 'i':
                printf("Enter lines (empty line to exit):\n");
                while (fgets(line, MAX_LINE_LEN, stdin)) {
                    len = strlen(line);
                    if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
                    if (len == 0) break;
                    editor_insert_line(line);
                }
                break;

            case 'l':
                editor_list_lines();
                break;

            case 'w':
                if (strcmp(cmd, "wq") == 0) {
                    if (editor_write_file() == 0) {
                        printf("File saved. Exiting editor.\n");
                        goto editor_exit;
                    }
                } else {
                    if (editor_write_file() == 0) {
                        printf("File saved.\n");
                    }
                }
                break;

            case 'q':
                if (editor.modified) {
                    printf("File modified. Save? (y/n): ");
                    fflush(stdout);
                    if (fgets(cmd, 10, stdin) && cmd[0] == 'y') {
                        editor_write_file();
                    }
                }
                goto editor_exit;

            case 'h':
            case '?':
                printf("i - Insert lines\nl - List lines\nw - Write file\nwq - Write and exit\nq - Quit\nh - Help\n");
                break;

            default:
                printf("Unknown command. Type 'h' for help.\n");
        }
    }

editor_exit:
    for (int i = 0; i < editor.line_count; i++) {
        free(editor.lines[i]);
    }
}

/* ═══════════════════════════════════════════════════════════════
 * Main Shell
 * ═══════════════════════════════════════════════════════════════ */

void print_help(void) {
    printf("\n=== Ark Init Shell ===\n");
    printf("Commands:\n");
    printf("  edit <file>  - Open text editor\n");
    printf("  cat <file>   - Display file contents\n");
    printf("  ls           - List files\n");
    printf("  rm <file>    - Delete file\n");
    printf("  help         - Show this help\n");
    printf("  exit         - Exit shell\n");
    printf("\n");
}

void cmd_cat(const char *filename) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        printf("Error: cannot open file\n");
        return;
    }

    char buffer[256];
    ssize_t bytes;
    while ((bytes = read(fd, buffer, 255)) > 0) {
        buffer[bytes] = '\0';
        printf("%s", buffer);
    }
    close(fd);
    printf("\n");
}

void cmd_ls(void) {
    printf("File listing not implemented in this init\n");
}

void cmd_rm(const char *filename) {
    if (unlink(filename) < 0) {
        printf("Error: cannot delete file\n");
    } else {
        printf("File deleted: %s\n", filename);
    }
}

int main(int argc, char *argv[]) {
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║  Ark Kernel - Userspace Init System   ║\n");
    printf("║  Type 'help' for available commands   ║\n");
    printf("╚════════════════════════════════════════╝\n\n");

    print_help();

    char cmd_line[512];
    char cmd[64];
    char arg1[256];
    char arg2[256];

    while (1) {
        printf("init> ");
        fflush(stdout);

        if (!fgets(cmd_line, 511, stdin)) {
            break;
        }
        
        /* Update cursor blink state */
        printk_cursor_auto_update();

        int len = strlen(cmd_line);
        if (len > 0 && cmd_line[len-1] == '\n') cmd_line[len-1] = '\0';

        if (len == 0) continue;

        /* Parse command */
        memset(cmd, 0, 64);
        memset(arg1, 0, 256);
        
        int n = sscanf(cmd_line, "%s %s %s", cmd, arg1, arg2);

        if (strcmp(cmd, "edit") == 0 && n >= 2) {
            editor_run(arg1);
        }
        else if (strcmp(cmd, "cat") == 0 && n >= 2) {
            cmd_cat(arg1);
        }
        else if (strcmp(cmd, "ls") == 0) {
            cmd_ls();
        }
        else if (strcmp(cmd, "rm") == 0 && n >= 2) {
            cmd_rm(arg1);
        }
        else if (strcmp(cmd, "help") == 0) {
            print_help();
        }
        else if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
            printf("Shutting down init system...\n");
            break;
        }
        else {
            printf("Unknown command: %s (type 'help' for available commands)\n", cmd);
        }
    }

    printf("Init exiting.\n");
    return 0;
}

