#include "ark/types.h"
#include "ark/syscall.h"

/* Simple string compare - no libc */
static int streq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

/* Simple string length */
static u32 strlen_simple(const char *s) {
    u32 len = 0;
    while (s[len]) len++;
    return len;
}

/* Simple print using write syscall */
static void puts_simple(const char *s) {
    syscall(SYS_WRITE, 1, (int)s, strlen_simple(s));
}

static void usage(void) {
    puts_simple("strapper: usage:\n");
    puts_simple("  strapper load <path>\n");
    puts_simple("  strapper unload <name>\n");
    puts_simple("  strapper list\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 1;
    }

    if (streq(argv[1], "load")) {
        if (argc < 3) {
            usage();
            return 1;
        }
        int rc = syscall(SYS_DKM_LOAD, (int)argv[2], 0, 0);
        if (rc != 0) {
            puts_simple("strapper: load failed\n");
            return rc;
        }
        puts_simple("strapper: loaded\n");
        return 0;
    } else if (streq(argv[1], "unload")) {
        if (argc < 3) {
            usage();
            return 1;
        }
        int rc = syscall(SYS_DKM_UNLOAD, (int)argv[2], 0, 0);
        if (rc != 0) {
            puts_simple("strapper: unload failed\n");
            return rc;
        }
        puts_simple("strapper: unloaded\n");
        return 0;
    } else if (streq(argv[1], "list")) {
        syscall(SYS_DKM_LIST, 0, 0, 0);
        return 0;
    }

    usage();
    return 1;
}
