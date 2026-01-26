/**
 * Ark OS Userspace Shell (init.bin)
 * 
 * This is the initial userspace program that runs after kernel initialization.
 * Provides a simple command shell for system interaction.
 */

#include "../include/ark/types.h"
#include "syscall.h"

#define INPUT_BUFFER_SIZE 256
#define ARG_MAX 16
#define STDOUT_FILENO 1

typedef struct {
    char cmd[64];
    char *args[ARG_MAX];
    int argc;
} command_t;

/**
 * Simple string utilities for userspace
 */
static int str_len(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static int str_cmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a - *b;
}

static void str_cpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/**
 * Parse command line input
 */
static void parse_command(const char *input, command_t *cmd) {
    int i = 0, j = 0;
    int in_arg = 0;
    char arg_buf[64];
    
    cmd->argc = 0;
    
    while (input[i] && cmd->argc < ARG_MAX) {
        if (input[i] == ' ' || input[i] == '\t' || input[i] == '\n') {
            if (in_arg) {
                arg_buf[j] = '\0';
                if (cmd->argc == 0) {
                    str_cpy(cmd->cmd, arg_buf, sizeof(cmd->cmd));
                }
                in_arg = 0;
                j = 0;
            }
        } else {
            arg_buf[j++] = input[i];
            in_arg = 1;
        }
        i++;
    }
    
    if (in_arg) {
        arg_buf[j] = '\0';
        if (cmd->argc == 0) {
            str_cpy(cmd->cmd, arg_buf, sizeof(cmd->cmd));
        }
    }
}

/**
 * Built-in shell commands with syscall output
 */
static void cmd_help(void) {
    uspace_puts("");
    uspace_puts("Ark Shell - Available Commands:");
    uspace_puts("================================");
    uspace_puts("  help        - Show this help message");
    uspace_puts("  clear       - Clear the screen");
    uspace_puts("  echo TEXT   - Print text");
    uspace_puts("  info        - Show system information");
    uspace_puts("  exit        - Exit the shell");
    uspace_puts("");
}

static void cmd_echo(const command_t *cmd) {
    for (int i = 1; i < cmd->argc; i++) {
        uspace_puts(cmd->args[i]);
        if (i < cmd->argc - 1) write(STDOUT_FILENO, " ", 1);
    }
    uspace_puts("");
}

static void cmd_info(void) {
    uspace_puts("");
    uspace_puts("===== Ark OS Userspace Shell (init.bin) =====");
    uspace_puts("Kernel: Ark OS x86 32-bit");
    uspace_puts("Shell: Interactive command interpreter");
    uspace_puts("Type 'help' for available commands");
    uspace_puts("");
}

static int cmd_execute(const command_t *cmd) {
    if (str_len(cmd->cmd) == 0) {
        return 0;
    }
    
    if (str_cmp(cmd->cmd, "help") == 0 || str_cmp(cmd->cmd, "?") == 0) {
        cmd_help();
        return 0;
    }
    
    if (str_cmp(cmd->cmd, "echo") == 0) {
        cmd_echo(cmd);
        return 0;
    }
    
    if (str_cmp(cmd->cmd, "info") == 0) {
        cmd_info();
        return 0;
    }
    
    if (str_cmp(cmd->cmd, "exit") == 0 || str_cmp(cmd->cmd, "quit") == 0) {
        return 1;
    }
    
    /* Unknown command */
    write(STDOUT_FILENO, "Unknown command: ", 16);
    uspace_puts(cmd->cmd);
    
    return 0;
}

/**
 * Main userspace initialization - Interactive shell loop
 * Runs continuously until user types 'exit'
 */
void init_usp(void) {
    char input_buffer[INPUT_BUFFER_SIZE];
    command_t cmd;
    int should_exit = 0;
    int iterations = 0;
    
    /* Simple marker - do a minimal write to test */
    write(STDOUT_FILENO, "S", 1);  /* Just write 'S' to show we started */
    
    /* ===== MAIN SHELL LOOP ===== */
    while (!should_exit && iterations < 5) {
        /* Demo input - cycle through commands */
        static const char *demo_commands[] = {
            "help",
            "info",
            "exit"
        };
        
        int cmd_count = sizeof(demo_commands) / sizeof(demo_commands[0]);
        const char *cmd_str = demo_commands[iterations % cmd_count];
        
        /* Copy command to buffer */
        int i = 0;
        while (cmd_str[i] && i < INPUT_BUFFER_SIZE - 1) {
            input_buffer[i] = cmd_str[i];
            i++;
        }
        input_buffer[i] = '\0';
        
        /* Parse and execute command */
        parse_command(input_buffer, &cmd);
        
        if (cmd_execute(&cmd)) {
            should_exit = 1;
        }
        
        iterations++;
        write(STDOUT_FILENO, ".", 1);  /* Progress marker */
    }
    
    write(STDOUT_FILENO, "\nE", 2);  /* End marker */
    return;
}
