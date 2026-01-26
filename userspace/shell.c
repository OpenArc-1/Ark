#include "syscall.h"

#define MAX_LINE 256
#define MAX_ARGS 16

/* ================= BASIC LIBC-LITE ================= */

int strlen(const char *s) {
    int i = 0;
    while (s[i]) i++;
    return i;
}

int strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a - *b;
}

void memset(char *s, char c, int n) {
    for (int i = 0; i < n; i++)
        s[i] = c;
}

/* stdout print using syscall */
int puts(const char *s) {
    return write(1, s, strlen(s));  // fd 1 = stdout
}

/* ================= INPUT ================= */

int readline(char *buf, int max_len) {
    int len = read(0, buf, max_len);   // fd 0 = stdin
    if (len <= 0) return len;

    if (len >= max_len) len = max_len - 1;
    buf[len] = '\0';   // null terminate

    return len;
}

/* ================= ARG PARSER ================= */

int parse_args(char *line, char **argv, int max_args) {
    int argc = 0;
    int in_word = 0;

    for (int i = 0; line[i] && argc < max_args - 1; i++) {
        if (line[i] == ' ' || line[i] == '\t' ||
            line[i] == '\n' || line[i] == '\r') {
            line[i] = '\0';
            in_word = 0;
        } else if (!in_word) {
            argv[argc++] = &line[i];
            in_word = 1;
        }
    }

    argv[argc] = 0;
    return argc;
}

/* ================= COMMANDS ================= */

int cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;

    puts("Ark Shell - Available commands:\n");
    puts("  help   - Show this help message\n");
    puts("  echo   - Echo arguments\n");
    puts("  clear  - Clear screen\n");
    puts("  exit   - Exit shell\n");
    return 0;
}

int cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        puts(argv[i]);
        if (i < argc - 1)
            puts(" ");
    }
    puts("\n");
    return 0;
}

int cmd_clear(int argc, char **argv) {
    (void)argc; (void)argv;
    puts("\033[2J\033[H");  // ANSI clear (serial works)
    return 0;
}

/* ================= EXECUTION ================= */

int execute_command(int argc, char **argv) {
    if (argc == 0) return 0;

    if (strcmp(argv[0], "help") == 0 || strcmp(argv[0], "?") == 0)
        return cmd_help(argc, argv);

    if (strcmp(argv[0], "echo") == 0)
        return cmd_echo(argc, argv);

    if (strcmp(argv[0], "clear") == 0)
        return cmd_clear(argc, argv);

    if (strcmp(argv[0], "exit") == 0) {
        puts("Goodbye!\n");
        exit(0);
    }

    puts("Unknown command: ");
    puts(argv[0]);
    puts("\nType 'help' for available commands.\n");
    return 1;
}

/* ================= MAIN SHELL LOOP ================= */

void shell_main(void) {
    char line[MAX_LINE];
    char *argv[MAX_ARGS];

    puts("\n========================================\n");
    puts("  Ark Shell v1.0 (userspace)\n");
    puts("========================================\n\n");

    while (1) {
        puts("ark> ");

        memset(line, 0, MAX_LINE);
        int len = readline(line, MAX_LINE - 1);
        if (len <= 0)
            continue;

        int argc = parse_args(line, argv, MAX_ARGS);
        if (argc > 0)
            execute_command(argc, argv);
    }
}
