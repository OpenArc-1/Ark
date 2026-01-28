/**
 * Display Server Compositor
 * Manages windows, z-order, and rendering to 640x480 framebuffer
 * Kernel acts as middleman for syscalls
 */

#include "../include/ark/syscall.h"
#include "../include/ark/types.h"
#include "../include/ark/fb.h"

/* Access kernel's framebuffer info directly */
extern ark_fb_info_t g_fb_info;

#define SCREEN_WIDTH    640
#define SCREEN_HEIGHT   480
#define MAX_WINDOWS     10

/* Color definitions (32-bit ARGB) */
#define COLOR_BLACK     0xFF000000
#define COLOR_WHITE     0xFFFFFFFF
#define COLOR_RED       0xFFFF0000
#define COLOR_GREEN     0xFF00FF00
#define COLOR_BLUE      0xFF0000FF
#define COLOR_GRAY      0xFF808080
#define COLOR_DARK_GRAY 0xFF404040

typedef struct {
    int x, y;           /* Position */
    int width, height;  /* Dimensions */
    u32 bg_color;       /* Background color */
    int z_order;        /* Depth (higher = on top) */
    int active;         /* Is window valid? */
    int pid;            /* Process ID (for future use) */
} window_t;

typedef struct {
    window_t windows[MAX_WINDOWS];
    int window_count;
    int next_z_order;
    u32 *framebuffer;      /* Pointer to kernel framebuffer */
} compositor_state_t;

/* Global compositor state */
static compositor_state_t comp = {0};

/**
 * Helper: Draw pixel to framebuffer directly
 */
static void draw_pixel(int x, int y, u32 color) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
        return;
    }
    if (comp.framebuffer == NULL) return;
    
    comp.framebuffer[y * SCREEN_WIDTH + x] = color;
}

/**
 * Helper: Draw filled rectangle
 */
static void fill_rect(int x1, int y1, int w, int h, u32 color) {
    int x2 = x1 + w;
    int y2 = y1 + h;
    
    if (x2 < 0 || y2 < 0 || x1 >= SCREEN_WIDTH || y1 >= SCREEN_HEIGHT) {
        return;  /* Completely off-screen */
    }
    
    /* Clip to screen bounds */
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > SCREEN_WIDTH) x2 = SCREEN_WIDTH;
    if (y2 > SCREEN_HEIGHT) y2 = SCREEN_HEIGHT;
    
    for (int y = y1; y < y2; y++) {
        for (int x = x1; x < x2; x++) {
            draw_pixel(x, y, color);
        }
    }
}

/**
 * Helper: Draw rectangle outline
 */
static void draw_rect_outline(int x, int y, int w, int h, u32 color) {
    /* Top and bottom edges */
    fill_rect(x, y, w, 1, color);
    fill_rect(x, y + h - 1, w, 1, color);
    
    /* Left and right edges */
    fill_rect(x, y, 1, h, color);
    fill_rect(x + w - 1, y, 1, h, color);
}

/**
 * Render all windows in z-order
 */
static void composite_frame(void) {
    if (comp.framebuffer == NULL) return;
    
    /* Clear framebuffer to black */
    int pixels = SCREEN_WIDTH * SCREEN_HEIGHT;
    for (int i = 0; i < pixels; i++) {
        comp.framebuffer[i] = COLOR_BLACK;
    }
    
    /* Render each window in z-order */
    for (int i = 0; i < MAX_WINDOWS; i++) {
        window_t *w = &comp.windows[i];
        if (!w->active) continue;
        
        /* Draw window background */
        fill_rect(w->x, w->y, w->width, w->height, w->bg_color);
        
        /* Draw window border */
        draw_rect_outline(w->x, w->y, w->width, w->height, COLOR_WHITE);
    }
}

/**
 * Syscall: Create window
 * Returns: window ID (0-9), or -1 on error
 */
static int syscall_gfx_create_window(int x, int y, int width, int height) {
    if (comp.window_count >= MAX_WINDOWS) {
        return -1;  /* Too many windows */
    }
    
    int win_id = comp.window_count;
    window_t *w = &comp.windows[win_id];
    
    w->x = x;
    w->y = y;
    w->width = width;
    w->height = height;
    w->bg_color = COLOR_DARK_GRAY;
    w->z_order = comp.next_z_order++;
    w->active = 1;
    w->pid = 0;
    
    comp.window_count++;
    return win_id;
}

/**
 * Syscall: Destroy window
 */
static int syscall_gfx_destroy_window(int win_id) {
    if (win_id < 0 || win_id >= MAX_WINDOWS) {
        return -1;
    }
    
    comp.windows[win_id].active = 0;
    return 0;
}

/**
 * Syscall: Draw filled rectangle in window
 */
static int syscall_gfx_draw_rect(int win_id, int x, int y, int w, int h, u32 color) {
    if (win_id < 0 || win_id >= MAX_WINDOWS || !comp.windows[win_id].active) {
        return -1;
    }
    
    window_t *win = &comp.windows[win_id];
    
    /* Map window-relative coordinates to screen coordinates */
    int screen_x = win->x + x;
    int screen_y = win->y + y;
    
    fill_rect(screen_x, screen_y, w, h, color);
    return 0;
}

/**
 * Syscall: Clear window to color
 */
static int syscall_gfx_clear(int win_id, u32 color) {
    if (win_id < 0 || win_id >= MAX_WINDOWS || !comp.windows[win_id].active) {
        return -1;
    }
    
    window_t *win = &comp.windows[win_id];
    win->bg_color = color;
    return 0;
}

/**
 * Syscall: Present (composite and sync to hardware)
 */
static int syscall_gfx_present(void) {
    composite_frame();
    return 0;
}

/**
 * Welcome screen: Draw on the default window
 */
static void draw_welcome_screen(void) {
    /* Create a window for demo */
    int win = syscall_gfx_create_window(50, 50, 280, 180);
    
    if (win >= 0) {
        /* Draw some rectangles */
        syscall_gfx_draw_rect(win, 10, 10, 100, 80, COLOR_BLUE);
        syscall_gfx_draw_rect(win, 150, 10, 100, 80, COLOR_RED);
        
        /* Present frame */
        syscall_gfx_present();
    }
}

/**
 * Main compositor loop
 */
void compositor_main(void) {
    /* Use kernel's framebuffer info directly */
    if (g_fb_info.addr == NULL) {
        /* Framebuffer not initialized by kernel */
        return;
    }
    
    comp.framebuffer = (u32 *)g_fb_info.addr;
    
    /* Draw test rectangles */
    if (comp.framebuffer != NULL) {
        /* Clear to black */
        int pixels = SCREEN_WIDTH * SCREEN_HEIGHT;
        for (int i = 0; i < pixels; i++) {
            comp.framebuffer[i] = COLOR_BLACK;
        }
        
        /* Write blue rectangle from (60,50) to (260,250) */
        for (int y = 50; y < 250; y++) {
            for (int x = 60; x < 260; x++) {
                comp.framebuffer[y * SCREEN_WIDTH + x] = COLOR_BLUE;
            }
        }
        
        /* Write green rectangle from (380,100) to (580,300) */
        for (int y = 100; y < 300; y++) {
            for (int x = 380; x < 580; x++) {
                comp.framebuffer[y * SCREEN_WIDTH + x] = COLOR_GREEN;
            }
        }
        
        /* Write red rectangle from (200,200) to (440,400) */
        for (int y = 200; y < 400; y++) {
            for (int x = 200; x < 440; x++) {
                comp.framebuffer[y * SCREEN_WIDTH + x] = COLOR_RED;
            }
        }
    }
    
    /* Keep display server running */
    while (1) {
        /* In real implementation: wait for events/commands */
        __asm__ __volatile__("pause");  /* Yield CPU without halting */
    }
}
