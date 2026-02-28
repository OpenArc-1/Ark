/**
 * texted.c - Simple text editor for Ark kernel
 * 
 * Usage: texted filename
 * 
 * Commands:
 *   i    - Insert mode (^D to exit)
 *   l    - List lines
 *   s/old/new/  - Substitute
 *   w    - Write file
 *   q    - Quit without saving
 *   wq   - Write and quit
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define MAX_LINES 1000
#define MAX_LINE_LEN 256

typedef struct {
    char *lines[MAX_LINES];
    int line_count;
    char filename[256];
    int modified;
} editor_t;

editor_t editor = {0};
int current_line = 0;

void editor_init(const char *filename) {
    memset(&editor, 0, sizeof(editor));
    strncpy(editor.filename, filename, 255);
    editor.line_count = 0;
    editor.modified = 0;
}

int editor_read_file(void) {
    int fd = open(editor.filename, O_RDONLY);
    if (fd < 0) {
        printf("Creating new file: %s\n", editor.filename);
        return 0;
    }

    char buffer[MAX_LINE_LEN];
    int bytes_read = 0;
    
    while ((bytes_read = read(fd, buffer, MAX_LINE_LEN)) > 0) {
        if (editor.line_count >= MAX_LINES) break;
        
        char *newline = strchr(buffer, '\n');
        if (newline) *newline = '\0';
        
        size_t len = strlen(buffer) + 1;
        editor.lines[editor.line_count] = malloc(len);
        if (!editor.lines[editor.line_count]) {
            printf("Memory allocation failed\n");
            close(fd);
            return -1;
        }
        
        strcpy(editor.lines[editor.line_count], buffer);
        editor.line_count++;
    }
    
    close(fd);
    printf("Loaded %d lines from %s\n", editor.line_count, editor.filename);
    return 0;
}

int editor_write_file(void) {
    int fd = open(editor.filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("Failed to open file for writing: %s\n", editor.filename);
        return -1;
    }

    for (int i = 0; i < editor.line_count; i++) {
        if (write(fd, editor.lines[i], strlen(editor.lines[i])) < 0) {
            printf("Write error\n");
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
    printf("File saved: %s (%d lines)\n", editor.filename, editor.line_count);
    return 0;
}

void editor_insert_line(int lineno, const char *text) {
    if (editor.line_count >= MAX_LINES) {
        printf("File too large\n");
        return;
    }

    if (lineno > editor.line_count) lineno = editor.line_count;

    // Shift lines down
    for (int i = editor.line_count; i > lineno; i--) {
        editor.lines[i] = editor.lines[i-1];
    }

    size_t len = strlen(text) + 1;
    editor.lines[lineno] = malloc(len);
    if (!editor.lines[lineno]) {
        printf("Memory allocation failed\n");
        return;
    }

    strcpy(editor.lines[lineno], text);
    editor.line_count++;
    editor.modified = 1;
}

void editor_list_lines(void) {
    if (editor.line_count == 0) {
        printf("(empty file)\n");
        return;
    }

    for (int i = 0; i < editor.line_count; i++) {
        printf("%4d | %s\n", i+1, editor.lines[i]);
    }
}

void editor_delete_line(int lineno) {
    if (lineno < 0 || lineno >= editor.line_count) {
        printf("Invalid line number\n");
        return;
    }

    free(editor.lines[lineno]);

    for (int i = lineno; i < editor.line_count - 1; i++) {
        editor.lines[i] = editor.lines[i+1];
    }

    editor.line_count--;
    editor.modified = 1;
}

void editor_prompt(void) {
    char cmd[256];
    char newline_text[MAX_LINE_LEN];

    while (1) {
        printf("ed> ");
        fflush(stdout);

        if (!fgets(cmd, 255, stdin)) {
            printf("\n");
            break;
        }

        // Remove newline
        size_t len = strlen(cmd);
        if (len > 0 && cmd[len-1] == '\n') cmd[len-1] = '\0';

        if (len == 0) continue;

        switch (cmd[0]) {
            case 'i': // Insert mode
                printf("Enter lines (Ctrl+D or empty line to exit):\n");
                while (fgets(newline_text, MAX_LINE_LEN, stdin)) {
                    len = strlen(newline_text);
                    if (len > 0 && newline_text[len-1] == '\n') {
                        newline_text[len-1] = '\0';
                    }
                    if (len == 0) break;
                    editor_insert_line(editor.line_count, newline_text);
                }
                break;

            case 'l': // List lines
                editor_list_lines();
                break;

            case 'd': // Delete line
                if (sscanf(cmd, "d %d", &current_line) == 1) {
                    editor_delete_line(current_line - 1);
                } else {
                    editor_delete_line(editor.line_count - 1);
                }
                break;

            case 'w': // Write file
                if (strcmp(cmd, "wq") == 0) {
                    editor_write_file();
                    printf("Exiting...\n");
                    return;
                } else {
                    editor_write_file();
                }
                break;

            case 'q': // Quit
                if (editor.modified) {
                    printf("File modified. Really quit? (y/n) ");
                    fflush(stdout);
                    char resp;
                    scanf("%c", &resp);
                    if (resp != 'y') continue;
                }
                printf("Exiting...\n");
                return;

            case 'h': // Help
            case '?':
                printf("Commands:\n");
                printf("  i       - Insert mode\n");
                printf("  l       - List lines\n");
                printf("  d [N]   - Delete line N (or last)\n");
                printf("  w       - Write file\n");
                printf("  wq      - Write and quit\n");
                printf("  q       - Quit without saving\n");
                printf("  h, ?    - This help\n");
                break;

            default:
                printf("Unknown command: %s (type 'h' for help)\n", cmd);
                break;
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: texted <filename>\n");
        return 1;
    }

    editor_init(argv[1]);
    
    if (editor_read_file() < 0) {
        return 1;
    }

    printf("Simple Text Editor (type 'h' for help)\n");
    printf("Editing: %s\n", editor.filename);

    editor_prompt();

    // Free memory
    for (int i = 0; i < editor.line_count; i++) {
        free(editor.lines[i]);
    }

    return 0;
}
