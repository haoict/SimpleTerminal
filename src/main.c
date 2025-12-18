#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>
#include <SDL2/SDL_ttf.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <locale.h>
#include <pty.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
#include "font.h"
#include "keyboard.h"
#include "vt100.h"

#define USAGE "Simple Terminal\nusage: simple-terminal [-h] [-scale 2.0] [-font font.ttf] [-fontsize 14] [-fontshade 0|1|2] [-rotate 0|90|180|270] [-o file] [-q] [-r command ...]\n"

/* Arbitrary sizes */
#define DRAW_BUF_SIZ 20 * 1024

#define REDRAW_TIMEOUT (80 * 1000) /* 80 ms */

/* macros */
#define TIMEDIFF(t1, t2) ((t1.tv_sec - t2.tv_sec) * 1000 + (t1.tv_usec - t2.tv_usec) / 1000)

enum WindowState { WIN_VISIBLE = 1, WIN_REDRAW = 2, WIN_FOCUSED = 4 };

/* Purely graphic info */
typedef struct {
    // Colormap cmap;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Surface *surface;
    int width, height;           /* window width and height */
    int tty_width, tty_height;   /* tty width and height */
    int char_height, char_width; /* char height and width */
    char state;                  /* focus, redraw, visible */
} MainWindow;

/* Drawing Context */
typedef struct {
    SDL_Color colors[LEN(colormap) < 256 ? 256 : LEN(colormap)];
    // TTF_Font *font, *ifont, *bfont, *ibfont;
} DrawingContext;

/*
 * Special keys (change & recompile accordingly)
 * Keep in mind that kpress() in main.c hardcodes some keys.
 * Mask value:
 * * Use XK_NO_MOD to match the key alone (no modifiers)
 */
#define XK_ANY_MOD 0

typedef struct {
    SDL_Keycode k;        // key
    Uint16 mask;          // modifier mask: KMOD_ALT, KMOD_CTRL, KMOD_SHIFT, KMOD_CAPS, ...
    char s[ESC_BUF_SIZ];  // output string
} NonPrintingKeyboardKey;

static NonPrintingKeyboardKey non_printing_keyboard_keys[] = {
    {SDLK_ESCAPE, XK_ANY_MOD, "\033"},       // Escape
    {SDLK_TAB, KMOD_LSHIFT, "\033[Z"},       // Shift+Tab
    {SDLK_TAB, KMOD_RSHIFT, "\033[Z"},       // Shift+Tab
    {SDLK_TAB, XK_ANY_MOD, "\t"},            // Tab
    {SDLK_RETURN, KMOD_LALT, "\033\r"},      // Alt+Enter
    {SDLK_RETURN, KMOD_RALT, "\033\r"},      // Alt+Enter
    {SDLK_RETURN, XK_ANY_MOD, "\r"},         // Enter
    {SDLK_LEFT, XK_ANY_MOD, "\033[D"},       // Left
    {SDLK_RIGHT, XK_ANY_MOD, "\033[C"},      // Right
    {SDLK_UP, XK_ANY_MOD, "\033[A"},         // Up
    {SDLK_DOWN, XK_ANY_MOD, "\033[B"},       // Down
    {SDLK_BACKSPACE, XK_ANY_MOD, "\177"},    // Backspace
    {SDLK_HOME, XK_ANY_MOD, "\033[1~"},      // Home
    {SDLK_INSERT, XK_ANY_MOD, "\033[2~"},    // Insert
    {SDLK_DELETE, XK_ANY_MOD, "\033[3~"},    // Delete
    {SDLK_END, XK_ANY_MOD, "\033[4~"},       // End
    {SDLK_PAGEUP, XK_ANY_MOD, "\033[5~"},    // Page Up
    {SDLK_PAGEDOWN, XK_ANY_MOD, "\033[6~"},  // Page Down
    {SDLK_F1, XK_ANY_MOD, "\033OP"},         // F1
    {SDLK_F2, XK_ANY_MOD, "\033OQ"},         // F2
    {SDLK_F3, XK_ANY_MOD, "\033OR"},         // F3
    {SDLK_F4, XK_ANY_MOD, "\033OS"},         // F4
    {SDLK_F5, XK_ANY_MOD, "\033[15~"},       // F5
    {SDLK_F6, XK_ANY_MOD, "\033[17~"},       // F6
    {SDLK_F7, XK_ANY_MOD, "\033[18~"},       // F7
    {SDLK_F8, XK_ANY_MOD, "\033[19~"},       // F8
    {SDLK_F9, XK_ANY_MOD, "\033[20~"},       // F9
    {SDLK_F10, XK_ANY_MOD, "\033[21~"},      // F10
    {SDLK_F11, XK_ANY_MOD, "\033[23~"},      // F11
    {SDLK_F12, XK_ANY_MOD, "\033[24~"},      // F12
};

/* SDL Surfaces */
SDL_Surface *screen;
SDL_Surface *osk_screen;
SDL_Surface *rotated_screen;   // final frame matching window size

static void draw(void);
static void draw_region(int, int, int, int);
static void main_loop(void);
int tty_thread(void *unused);

static void x_draws(char *, Glyph, int, int, int, int);
static void x_clear(int, int, int, int);
static void x_draw_cursor(void);
static void sdl_init(void);
static void create_tty_thread();
static void init_color_map(void);
static void sdl_term_clear(int, int, int, int);
static void x_resize(int, int);
static void scale_to_size(int, int);
static char *k_map(SDL_Keycode, Uint16);
static void k_press(SDL_Event *);
static void text_input(SDL_Event *);
static void window_event_handler(SDL_Event *);

static void update_render(void);
static Uint32 clear_popup_timer(Uint32 interval, void *param);

static void (*event_handler[SDL_LASTEVENT])(SDL_Event *) = {[SDL_KEYDOWN] = k_press, [SDL_TEXTINPUT] = text_input, [SDL_WINDOWEVENT] = window_event_handler};

/* Globals */
static DrawingContext drawing_ctx;
static MainWindow main_window;
static SDL_Joystick *joystick;

SDL_Thread *thread = NULL;

char **opt_cmd = NULL;
int opt_cmd_size = 0;
char *opt_io = NULL;

static int embedded_font_name = 1;  // 1 or 2
static volatile int thread_should_exit = 0;
static int shutdown_called = 0;

char popup_message[256];

size_t x_write(int fd, char *s, size_t len) {
    size_t aux = len;

    while (len > 0) {
        ssize_t r = write(fd, s, len);
        if (r < 0) return r;
        len -= r;
        s += r;
    }
    return aux;
}

void *x_malloc(size_t len) {
    void *p = malloc(len);
    if (!p) die("Out of memory\n");
    return p;
}

void *x_realloc(void *p, size_t len) {
    if ((p = realloc(p, len)) == NULL) die("Out of memory\n");
    return p;
}

void *x_calloc(size_t nmemb, size_t size) {
    void *p = calloc(nmemb, size);
    if (!p) die("Out of memory\n");
    return p;
}

void sdl_load_fonts() {
    // Try to load TTF font if opt_font is set
    if (opt_font && init_ttf_font(opt_font, opt_fontsize, opt_fontshade)) {
        main_window.char_width = get_ttf_char_width();
        main_window.char_height = get_ttf_char_height();
    } else {
        // Fallback to bitmap font
        main_window.char_width = get_embedded_font_char_width(embedded_font_name);
        main_window.char_height = get_embedded_font_char_height(embedded_font_name);
        fprintf(stderr, "Using embedded bitmap font %d (%dx%d)\n", embedded_font_name, main_window.char_width, main_window.char_height);
    }
}

void sdl_shutdown(void) {
    if (SDL_WasInit(SDL_INIT_EVERYTHING) != 0 && !shutdown_called) {
        shutdown_called = 1;
        fprintf(stderr, "SDL shutting down\n");
        if (thread) {
            printf("Signaling ttythread to exit...\n");
            thread_should_exit = 1;
            // tty_write n key to answer y/n question if blocked on ttyread
            tty_write("n", 1);

            tty_write("\033[?1000l", 7);   // disable mouse tracking to unblock ttyread
            SDL_WaitThread(thread, NULL);  // Wait for thread to exit cleanly
            // SDL_KillThread(thread);
            thread = NULL;
        }

        // Cleanup TTF font
        cleanup_ttf_font();

        if (main_window.surface) SDL_FreeSurface(main_window.surface);
        if (osk_screen) SDL_FreeSurface(osk_screen);
        if (rotated_screen) SDL_FreeSurface(rotated_screen);
        main_window.surface = NULL;
        SDL_JoystickClose(joystick);
        SDL_Quit();
    }
}

void window_event_handler(SDL_Event *event) {
#ifdef BR2
    return;  // no resize for BR2 handheld devices builds because of kms video driver
#endif
    switch (event->window.event) {
        case SDL_WINDOWEVENT_RESIZED:
            scale_to_size(event->window.data1, event->window.data2);
            break;
        default:
            break;
    }
}

void scale_to_size(int width, int height) {
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) return;
    main_window.width = width;
    main_window.height = height;
    printf("Set scale to size: %dx%d (x%.1f)\n", main_window.width, main_window.height, opt_scale);

    // Recreate texture for new size
    if (main_window.texture) {
        SDL_DestroyTexture(main_window.texture);
    }
    main_window.texture = SDL_CreateTexture(main_window.renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, main_window.width, main_window.height);
    if (!main_window.texture) {
        fprintf(stderr, "Unable to recreate texture: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    // Recreate surfaces
    if (main_window.surface) SDL_FreeSurface(main_window.surface);
    int compose_w = (opt_rotate == 90 || opt_rotate == 270) ? main_window.height : main_window.width;
    int compose_h = (opt_rotate == 90 || opt_rotate == 270) ? main_window.width : main_window.height;
    main_window.surface = SDL_CreateRGBSurface(0, compose_w, compose_h, 16, 0xF800, 0x7E0, 0x1F, 0);  // compose buffer
    if (osk_screen) SDL_FreeSurface(osk_screen);
    osk_screen = SDL_CreateRGBSurface(0, compose_w, compose_h, 16, 0xF800, 0x7E0, 0x1F, 0);          // compose + keyboard
    if (rotated_screen) SDL_FreeSurface(rotated_screen);
    rotated_screen = SDL_CreateRGBSurface(0, main_window.width, main_window.height, 16, 0xF800, 0x7E0, 0x1F, 0);      // final frame

    // Recreate screen surface for compatibility
    if (screen) SDL_FreeSurface(screen);
    screen = SDL_CreateRGBSurface(0, 640, 480, 16, 0xF800, 0x7E0, 0x1F, 0);

    // resize terminal to fit content buffer (which may be swapped for 90/270)
    int col, row;
    int content_w = main_window.surface ? main_window.surface->w : main_window.width;
    int content_h = main_window.surface ? main_window.surface->h : main_window.height;
    col = (content_w - 2 * borderpx) / main_window.char_width;
    row = (content_h - 2 * borderpx) / main_window.char_height;
    t_resize(col, row);
    x_resize(col, row);
    tty_resize();
}

void sdl_init(void) {
    fprintf(stderr, "SDL init\n");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
        fprintf(stderr, "Unable to initialize SDL: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    SDL_ShowCursor(0);
    SDL_StartTextInput();

    /* font */
    sdl_load_fonts();

    /* colors */
    init_color_map();

    int display_index = 0;  // usually 0 unless you have multiple screens
    SDL_DisplayMode mode;
    if (SDL_GetCurrentDisplayMode(display_index, &mode) != 0) {
        printf("SDL_GetCurrentDisplayMode failed: %s\n", SDL_GetError());
        main_window.width = initial_width;
        main_window.height = initial_height;
    } else {
        printf("Detected screen: %dx%d @ %dHz\n", mode.w, mode.h, mode.refresh_rate);
        main_window.width = mode.w;
        main_window.height = mode.h;
#ifndef BR2
        main_window.width = initial_width * 2;
        main_window.height = initial_height * 2;
#endif
        printf("Setting resolution to: %dx%d\n", main_window.width, main_window.height);
    }

    main_window.window = SDL_CreateWindow("Simple Terminal", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, main_window.width, main_window.height, SDL_WINDOW_SHOWN);
    if (!main_window.window) {
        fprintf(stderr, "Unable to create window: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    main_window.renderer = SDL_CreateRenderer(main_window.window, -1, SDL_RENDERER_ACCELERATED);
    if (!main_window.renderer) {
        main_window.renderer = SDL_CreateRenderer(main_window.window, -1, SDL_RENDERER_SOFTWARE);
        if (!main_window.renderer) {
            fprintf(stderr, "Unable to create renderer: %s\n", SDL_GetError());
            exit(EXIT_FAILURE);
        }
    }

    // SDL_RenderSetLogicalSize(main_window.renderer, main_window.width, main_window.height);

    main_window.texture = SDL_CreateTexture(main_window.renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, main_window.width, main_window.height);
    if (!main_window.texture) {
        fprintf(stderr, "Unable to create texture: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }
    int compose_w = (opt_rotate == 90 || opt_rotate == 270) ? main_window.height : main_window.width;
    int compose_h = (opt_rotate == 90 || opt_rotate == 270) ? main_window.width : main_window.height;
    main_window.surface = SDL_CreateRGBSurface(0, compose_w, compose_h, 16, 0xF800, 0x7E0, 0x1F, 0);  // console screen
    osk_screen = SDL_CreateRGBSurface(0, compose_w, compose_h, 16, 0xF800, 0x7E0, 0x1F, 0);           // for keyboard mix
    rotated_screen = SDL_CreateRGBSurface(0, main_window.width, main_window.height, 16, 0xF800, 0x7E0, 0x1F, 0);        // final frame after rotation

    // Create a temporary surface for the screen to maintain compatibility
    screen = SDL_CreateRGBSurface(0, main_window.width, main_window.height, 16, 0xF800, 0x7E0, 0x1F, 0);

    main_window.state |= WIN_VISIBLE | WIN_REDRAW;

    joystick = SDL_JoystickOpen(0);
}

void create_tty_thread() {
    // TODO: might need to use system threads
    if (!(thread = SDL_CreateThread(tty_thread, "ttythread", NULL))) {
        fprintf(stderr, "Unable to create thread: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }
}

void update_render(void) {
    if (main_window.surface == NULL) return;

    memcpy(osk_screen->pixels, main_window.surface->pixels, main_window.surface->w * main_window.surface->h * 2);
    if (popup_message[0] != '\0') {
        SDL_Rect rect = {borderpx, main_window.height / 2 - main_window.char_height / 2 - 4, main_window.width - borderpx * 2, main_window.char_height + 6};
        SDL_Color popup_box_bg = drawing_ctx.colors[8];
        SDL_Color popup_box_str = drawing_ctx.colors[11];
        SDL_FillRect(osk_screen, &rect, SDL_MapRGB(osk_screen->format, popup_box_bg.r, popup_box_bg.g, popup_box_bg.b));
        draw_string(osk_screen, popup_message, rect.x + 2, rect.y + 4, SDL_MapRGB(osk_screen->format, popup_box_str.r, popup_box_str.g, popup_box_str.b), embedded_font_name);
    }
    draw_keyboard(osk_screen);  // osk_screen(SW) = console + keyboard
    // Update texture with screen pixels and render
    SDL_RenderClear(main_window.renderer);
    if (opt_rotate == 90 || opt_rotate == 270) {
        // Ensure rotated_screen matches window size
        if (!rotated_screen || rotated_screen->w != main_window.width || rotated_screen->h != main_window.height) {
            if (rotated_screen) SDL_FreeSurface(rotated_screen);
            rotated_screen = SDL_CreateRGBSurface(0, main_window.width, main_window.height, 16, 0xF800, 0x7E0, 0x1F, 0);
        }

        // Rotate osk_screen into rotated_screen
        SDL_LockSurface(osk_screen);
        SDL_LockSurface(rotated_screen);
        int sw = osk_screen->w, sh = osk_screen->h;
        int dpw = rotated_screen->w, dph = rotated_screen->h; // dpw=window width, dph=window height
        Uint16 *sdata = (Uint16 *)osk_screen->pixels;
        Uint16 *ddata = (Uint16 *)rotated_screen->pixels;
        int spitch = osk_screen->pitch / 2;
        int dpitch = rotated_screen->pitch / 2;
        if (opt_rotate == 90) {
            // source (sw=H, sh=W) -> dest (dpw=W, dph=H)
            for (int y = 0; y < sh; y++) {
                for (int x = 0; x < sw; x++) {
                    int dx = dpw - 1 - y;
                    int dy = x;
                    ddata[dy * dpitch + dx] = sdata[y * spitch + x];
                }
            }
        } else { // 270 degrees
            for (int y = 0; y < sh; y++) {
                for (int x = 0; x < sw; x++) {
                    int dx = y;
                    int dy = dph - 1 - x;
                    ddata[dy * dpitch + dx] = sdata[y * spitch + x];
                }
            }
        }
        SDL_UnlockSurface(rotated_screen);
        SDL_UnlockSurface(osk_screen);
        SDL_UpdateTexture(main_window.texture, NULL, rotated_screen->pixels, rotated_screen->pitch);
        SDL_RenderCopy(main_window.renderer, main_window.texture, NULL, NULL);
    } else {
        // 0 or 180 degrees: upload and render; 180 uses renderer rotation for speed
        SDL_UpdateTexture(main_window.texture, NULL, osk_screen->pixels, osk_screen->pitch);
        if (opt_rotate == 0) {
            SDL_RenderCopy(main_window.renderer, main_window.texture, NULL, NULL);
        } else { // 180
            SDL_RenderCopyEx(main_window.renderer, main_window.texture, NULL, NULL, 180.0, NULL, SDL_FLIP_NONE);
        }
    }
    SDL_RenderPresent(main_window.renderer);
}

void die(const char *errstr, ...) {
    va_list ap;
    va_start(ap, errstr);
    vfprintf(stderr, errstr, ap);
    va_end(ap);
    sdl_shutdown();
}

void x_resize(int col, int row) {
    main_window.tty_width = MAX(1, 2 * borderpx + col * main_window.char_width);
    main_window.tty_height = MAX(1, 2 * borderpx + row * main_window.char_height);
}

void init_color_map(void) {
    int i, r, g, b;

    // TODO: allow these to override the xterm ones somehow?
    memcpy(drawing_ctx.colors, colormap, sizeof(drawing_ctx.colors));

    /* init colors [16-255] ; same colors as xterm */
    for (i = 16, r = 0; r < 6; r++) {
        for (g = 0; g < 6; g++) {
            for (b = 0; b < 6; b++) {
                drawing_ctx.colors[i].r = r == 0 ? 0 : 0x3737 + 0x2828 * r;
                drawing_ctx.colors[i].g = g == 0 ? 0 : 0x3737 + 0x2828 * g;
                drawing_ctx.colors[i].b = b == 0 ? 0 : 0x3737 + 0x2828 * b;
                i++;
            }
        }
    }

    for (r = 0; r < 24; r++, i++) {
        b = 0x0808 + 0x0a0a * r;
        drawing_ctx.colors[i].r = b;
        drawing_ctx.colors[i].g = b;
        drawing_ctx.colors[i].b = b;
    }
}

void sdl_term_clear(int col1, int row1, int col2, int row2) {
    if (main_window.surface == NULL) return;
    SDL_Rect r = {borderpx + col1 * main_window.char_width, borderpx + row1 * main_window.char_height, (col2 - col1 + 1) * main_window.char_width, (row2 - row1 + 1) * main_window.char_height};
    SDL_Color c = drawing_ctx.colors[IS_SET(MODE_REVERSE) ? defaultfg : defaultbg];
    SDL_FillRect(main_window.surface, &r, SDL_MapRGB(main_window.surface->format, c.r, c.g, c.b));
}

/*
 * Absolute coordinates.
 */
void x_clear(int x1, int y1, int x2, int y2) {
    if (main_window.surface == NULL) return;
    SDL_Rect r = {x1, y1, x2 - x1, y2 - y1};
    SDL_Color c = drawing_ctx.colors[IS_SET(MODE_REVERSE) ? defaultfg : defaultbg];
    SDL_FillRect(main_window.surface, &r, SDL_MapRGB(main_window.surface->format, c.r, c.g, c.b));
}

void x_draws(char *s, Glyph base, int x, int y, int charlen, int bytelen) {
    int winx = borderpx + x * main_window.char_width, winy = borderpx + y * main_window.char_height, width = charlen * main_window.char_width;
    // TTF_Font *font = drawing_ctx.font;
    SDL_Color *fg = &drawing_ctx.colors[base.fg], *bg = &drawing_ctx.colors[base.bg], *temp, revfg, revbg;

    s[bytelen] = '\0';

    if (base.mode & ATTR_BOLD) {
        if (BETWEEN(base.fg, 0, 7)) {
            /* basic system colors */
            fg = &drawing_ctx.colors[base.fg + 8];
        } else if (BETWEEN(base.fg, 16, 195)) {
            /* 256 colors */
            fg = &drawing_ctx.colors[base.fg + 36];
        } else if (BETWEEN(base.fg, 232, 251)) {
            /* greyscale */
            fg = &drawing_ctx.colors[base.fg + 4];
        }
        /*
         * Those ranges will not be brightened:
         *	8 - 15 – bright system colors
         *	196 - 231 – highest 256 color cube
         *	252 - 255 – brightest colors in greyscale
         */
        // font = drawing_ctx.bfont;
    }

    /*if(base.mode & ATTR_ITALIC)
        font = drawing_ctx.ifont;
    if((base.mode & ATTR_ITALIC) && (base.mode & ATTR_BOLD))
        font = drawing_ctx.ibfont;*/

    if (IS_SET(MODE_REVERSE)) {
        if (fg == &drawing_ctx.colors[defaultfg]) {
            fg = &drawing_ctx.colors[defaultbg];
        } else {
            revfg.r = ~fg->r;
            revfg.g = ~fg->g;
            revfg.b = ~fg->b;
            fg = &revfg;
        }

        if (bg == &drawing_ctx.colors[defaultbg]) {
            bg = &drawing_ctx.colors[defaultfg];
        } else {
            revbg.r = ~bg->r;
            revbg.g = ~bg->g;
            revbg.b = ~bg->b;
            bg = &revbg;
        }
    }

    if (base.mode & ATTR_REVERSE) temp = fg, fg = bg, bg = temp;

    /* Intelligent cleaning up of the borders. */
    if (x == 0) {
        x_clear(0, (y == 0) ? 0 : winy, borderpx, winy + main_window.char_height + (y == term.row - 1) ? main_window.height : 0);
    }
    if (x + charlen >= term.col - 1) {
        x_clear(winx + width, (y == 0) ? 0 : winy, main_window.width, (y == term.row - 1) ? main_window.height : (winy + main_window.char_height));
    }
    if (y == 0) x_clear(winx, 0, winx + width, borderpx);
    if (y == term.row - 1) x_clear(winx, winy + main_window.char_height, winx + width, main_window.height);

    // SDL_Surface *text_surface;
    SDL_Rect r = {winx, winy, width, main_window.char_height};

    if (main_window.surface != NULL) {
        SDL_FillRect(main_window.surface, &r, SDL_MapRGB(main_window.surface->format, bg->r, bg->g, bg->b));
        // TODO: find a better way to draw cursor box y + 1
        int ys = r.y + 1;
        if (is_ttf_loaded()) {
            // Use TTF rendering
            draw_string_ttf(main_window.surface, s, winx, winy, *fg, *bg);
        } else {
            // Use bitmap rendering
            draw_string(main_window.surface, s, winx, ys, SDL_MapRGB(main_window.surface->format, fg->r, fg->g, fg->b), embedded_font_name);
        }
    }

    if (base.mode & ATTR_UNDERLINE) {
        // r.y += TTF_FontAscent(font) + 1;
        r.y += main_window.char_height;
        r.h = 1;
        if (main_window.surface != NULL) SDL_FillRect(main_window.surface, &r, SDL_MapRGB(main_window.surface->format, fg->r, fg->g, fg->b));
    }
}

void x_draw_cursor(void) {
    static int oldx = 0, oldy = 0;
    int sl;
    Glyph g = {{' '}, ATTR_NULL, defaultbg, defaultcs, 0};

    LIMIT(oldx, 0, term.col - 1);
    LIMIT(oldy, 0, term.row - 1);

    if (term.line[term.c.y][term.c.x].state & GLYPH_SET) memcpy(g.c, term.line[term.c.y][term.c.x].c, UTF_SIZ);

    /* remove the old cursor */
    if (term.line[oldy][oldx].state & GLYPH_SET) {
        sl = utf8_size(term.line[oldy][oldx].c);
        x_draws(term.line[oldy][oldx].c, term.line[oldy][oldx], oldx, oldy, 1, sl);
    } else {
        sdl_term_clear(oldx, oldy, oldx, oldy);
    }

    /* draw the new one */
    if (!(term.c.state & CURSOR_HIDE)) {
        if (!(main_window.state & WIN_FOCUSED)) g.bg = defaultucs;

        if (IS_SET(MODE_REVERSE)) g.mode |= ATTR_REVERSE, g.fg = defaultcs, g.bg = defaultfg;

        sl = utf8_size(g.c);
        x_draws(g.c, g, term.c.x, term.c.y, 1, sl);
        oldx = term.c.x, oldy = term.c.y;
    }
}

void redraw(void) {
    struct timespec tv = {0, REDRAW_TIMEOUT * 1000};

    t_full_dirt();
    draw();
    nanosleep(&tv, NULL);
}

void draw(void) {
    draw_region(0, 0, term.col, term.row);
    update_render();
}

void draw_region(int x1, int y1, int x2, int y2) {
    int ic, ib, x, y, ox, sl;
    Glyph base, new;
    char buf[DRAW_BUF_SIZ];

    if (!(main_window.state & WIN_VISIBLE)) return;

    for (y = y1; y < y2; y++) {
        if (!term.dirty[y]) continue;

        sdl_term_clear(0, y, term.col, y);
        term.dirty[y] = 0;
        base = term.line[y][0];
        ic = ib = ox = 0;
        for (x = x1; x < x2; x++) {
            new = term.line[y][x];
            if (ib > 0 && (!(new.state & GLYPH_SET) || ATTRCMP(base, new) || ib >= DRAW_BUF_SIZ - UTF_SIZ)) {
                x_draws(buf, base, ox, y, ic, ib);
                ic = ib = 0;
            }
            if (new.state & GLYPH_SET) {
                if (ib == 0) {
                    ox = x;
                    base = new;
                }
                sl = utf8_size(new.c);
                memcpy(buf + ib, new.c, sl);
                ib += sl;
                ++ic;
            }
        }
        if (ib > 0) x_draws(buf, base, ox, y, ic, ib);
    }
    x_draw_cursor();
}

char *k_map(SDL_Keycode k, Uint16 state) {
    int i;
    SDL_Keymod mask;

    for (i = 0; i < LEN(non_printing_keyboard_keys); i++) {
        mask = non_printing_keyboard_keys[i].mask;

        if (non_printing_keyboard_keys[i].k == k && ((state & mask) == mask || (mask == 0 && !state))) {
            return (char *)non_printing_keyboard_keys[i].s;
        }
    }
    return NULL;
}

void print_non_printing_key_for_debug(char *non_printing_key, SDL_KeyboardEvent *e) {
    char escaped_seq[16] = {0};
    int idx = 0;
    for (int i = 0; non_printing_key[i] != '\0'; i++) {
        unsigned char ch = non_printing_key[i];
        if (ch == 27) {  // '\033'
            strcpy(&escaped_seq[idx], "\\033");
            idx += 4;
        } else if (ch >= 32 && ch <= 126) {
            escaped_seq[idx++] = ch;
        } else {
            sprintf(&escaped_seq[idx], "\\x%02X", ch);
            idx += 4;
        }
    }
    escaped_seq[idx] = '\0';
    printf("Custom key mapped: %s - ksym=%d, scancode=%d, mod=%d\n", escaped_seq, e->keysym.sym, e->keysym.scancode, e->keysym.mod);
}

void k_press(SDL_Event *ev) {
    SDL_KeyboardEvent *e = &ev->key;
    char *non_printing_key;
    int meta, shift, ctrl, synth;
    SDL_Keycode ksym = e->keysym.sym;

    if (IS_SET(MODE_KBDLOCK)) return;

    meta = e->keysym.mod & KMOD_ALT;
    shift = e->keysym.mod & KMOD_SHIFT;
    ctrl = e->keysym.mod & KMOD_CTRL;
    synth = e->keysym.mod & KMOD_SYNTHETIC;

    // printf("kpress: keysym=%d scancode=%d mod=%d\n", ksym, e->keysym.scancode, e->keysym.mod);

    if ((non_printing_key = k_map(ksym, e->keysym.mod))) { /* 1. non printing keys from vt100.h */
        // print_non_printing_key_for_debug(non_printing_key, e);
        tty_write(non_printing_key, strlen(non_printing_key));
    } else if (ctrl && !meta && !shift) { /* 2. handle ctrl key */
        switch (ksym) {
            case SDLK_a:
                tty_write("\001", 1);
                break;
            case SDLK_b:
                tty_write("\002", 1);
                break;
            case SDLK_c:
                tty_write("\003", 1);
                break;
            case SDLK_d:
                tty_write("\004", 1);
                break;
            case SDLK_e:
                tty_write("\005", 1);
                break;
            case SDLK_f:
                tty_write("\006", 1);
                break;
            case SDLK_g:
                tty_write("\007", 1);
                break;
            case SDLK_h:
                tty_write("\010", 1);
                break;
            case SDLK_i:
                tty_write("\011", 1);
                break;
            case SDLK_j:
                tty_write("\012", 1);
                break;
            case SDLK_k:
                tty_write("\013", 1);
                break;
            case SDLK_l:
                tty_write("\014", 1);
                break;
            case SDLK_m:
                tty_write("\015", 1);
                break;
            case SDLK_n:
                tty_write("\016", 1);
                break;
            case SDLK_o:
                tty_write("\017", 1);
                break;
            case SDLK_p:
                tty_write("\020", 1);
                break;
            case SDLK_q:
                tty_write("\021", 1);
                break;
            case SDLK_r:
                tty_write("\022", 1);
                break;
            case SDLK_s:
                tty_write("\023", 1);
                break;
            case SDLK_t:
                tty_write("\024", 1);
                break;
            case SDLK_u:
                tty_write("\025", 1);
                break;
            case SDLK_v:
                tty_write("\026", 1);
                break;
            case SDLK_w:
                tty_write("\027", 1);
                break;
            case SDLK_x:
                tty_write("\030", 1);
                break;
            case SDLK_y:
                tty_write("\031", 1);
                break;
            case SDLK_z:
                tty_write("\032", 1);
                break;
            default:
                break;
        }
    } else {
        // special volumeup/down/powerkey handling
        if (e->keysym.scancode == 128) {
            printf("Volume Up key pressed\n");
        } else if (e->keysym.scancode == 129) {
            printf("Volume Down key pressed\n");
        } else if (e->keysym.scancode == 102) {
            printf("Power key pressed\n");
        }

        // keys pressed by on-screen keyboard
        if (synth) {
            // printf("Synthetic key event: %s\n", SDL_GetKeyName(e->keysym.sym));
            if (e->keysym.sym <= 128) {
                char ch = (char)e->keysym.sym;
                if (meta) {
                    tty_write("\033", 1);
                }
                tty_write(&ch, 1);
            }
        }
    }
    /* For printable keys, we handle text input separately with SDL_TEXTINPUT events */
}

void text_input(SDL_Event *ev) {
    SDL_TextInputEvent *e = &ev->text;
    tty_write(e->text, strlen(e->text));
}

int tty_thread(void *unused) {
    int i;
    fd_set rfd;
    struct timeval drawtimeout, *tv = NULL;
    SDL_Event event;
    (void)unused;

    event.type = SDL_USEREVENT;
    event.user.code = 0;
    event.user.data1 = NULL;
    event.user.data2 = NULL;

    for (i = 0;; i++) {
        if (thread_should_exit) break;
        FD_ZERO(&rfd);
        FD_SET(cmdfd, &rfd);
        if (select(cmdfd + 1, &rfd, NULL, NULL, tv) < 0) {
            if (errno == EINTR) continue;
            die("select failed: %s\n", strerror(errno));
        }

        /*
         * Stop after a certain number of reads so the user does not
         * feel like the system is stuttering.
         */
        if (i < 1000 && FD_ISSET(cmdfd, &rfd)) {
            tty_read();

            /*
             * Just wait a bit so it isn't disturbing the
             * user and the system is able to write something.
             */
            drawtimeout.tv_sec = 0;
            drawtimeout.tv_usec = 5;
            tv = &drawtimeout;
            continue;
        }
        i = 0;
        tv = NULL;

        SDL_PushEvent(&event);
    }

    return 0;
}

static Uint32 clear_popup_timer(Uint32 interval, void *param) {
    popup_message[0] = '\0';
    return 0;  // one-shot timer
}

void take_screenshot() {
    char filename[64];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(filename, sizeof(filename), "st-%y%m%d_%H%M%S.bmp", t);
    // get home directory
    const char *home_dir = getenv("HOME");
    if (home_dir != NULL) {
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "%s/%s", home_dir, filename);
        strcpy(filename, filepath);
    }

    if (main_window.surface) {
        if (SDL_SaveBMP(main_window.surface, filename) == 0) {
            sprintf(popup_message, "Screenshot saved to %s", filename);
        } else {
            sprintf(popup_message, "Failed to save screenshot: %s", SDL_GetError());
        }
    }

    // Clear the popup message after 3 seconds
    SDL_AddTimer(3000, clear_popup_timer, NULL);
}

void main_loop(void) {
    SDL_Event ev;
    int running = 1;
    int button_up_held = 0, button_down_held = 0, button_left_held = 0, button_right_held = 0;
    Uint32 last_button_held_time = 0;
    while (running) {
        while (SDL_PollEvent(&ev))
        // while (SDL_WaitEvent(&ev))
        {
            if (ev.type == SDL_QUIT) {
                running = 0;
                break;
            }

            if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
                // printf("Keyboard event received - key: %d (%s), state: %s\n", ev.key.keysym.sym, SDL_GetKeyName(ev.key.keysym.sym), (ev.type == SDL_KEYDOWN) ? "DOWN" : "UP");
                int keyboard_event = handle_keyboard_event(&ev);
                if (keyboard_event == 1) {
                    // printf("OSK handled the event.\n");
                } else {
                    // printf("OSK passing event to default handler.\n");
                    if (event_handler[ev.type]) (event_handler[ev.type])(&ev);
                }

                int held = (ev.type == SDL_KEYDOWN);
                switch (ev.key.keysym.sym) {
                    case KEY_LEFT:
                        button_left_held = held;
                        break;
                    case KEY_RIGHT:
                        button_right_held = held;
                        break;
                    case KEY_UP:
                        button_up_held = held;
                        break;
                    case KEY_DOWN:
                        button_down_held = held;
                        break;
                    default:
                        break;
                }
            } else if (ev.type == SDL_JOYBUTTONDOWN || ev.type == SDL_JOYBUTTONUP) {
                // printf("Joystick event received type: %s - %d\n", (ev.jbutton.state == SDL_PRESSED) ? "down" : "up", ev.jbutton.button);
                SDL_Event sdl_event = {.key = {.type = (ev.jbutton.state == SDL_PRESSED) ? SDL_KEYDOWN : SDL_KEYUP,
                                               .state = (ev.jbutton.state == SDL_PRESSED) ? SDL_PRESSED : SDL_RELEASED,
                                               .keysym = {
                                                   .scancode = -ev.jbutton.button,
                                                   .sym = -ev.jbutton.button,
                                                   .mod = 0,
                                               }}};

                SDL_PushEvent(&sdl_event);
            } else {
                if (event_handler[ev.type]) (event_handler[ev.type])(&ev);
            }

            switch (ev.type) {
                case SDL_USEREVENT:
                    if (ev.user.code == 0) {  // redraw terminal
                        draw();
                    } else if (ev.user.code == 1) {  // Take a screenshot
                        take_screenshot();
                    }
            }
        }

        Uint32 now = SDL_GetTicks();
        int key = 0;
        if (button_down_held)
            key = KEY_DOWN;
        else if (button_up_held)
            key = KEY_UP;
        else if (button_left_held)
            key = KEY_LEFT;
        else if (button_right_held)
            key = KEY_RIGHT;

        if (key && now - last_button_held_time > BUTTON_HELD_DELAY) {
            handle_narrow_keys_held(key);
            last_button_held_time = now;
        }

        update_render();  // redraw the screen
        SDL_Delay(33);    // ~30 FPS
    }

    sdl_shutdown();
}

int main(int argc, char *argv[]) {
    setenv("SDL_NOMOUSE", "1", 1);
    int is_scale_set_by_user = 0;

    for (int i = 1; i < argc; i++) {
        // Handle multi-character options first
        if (strcmp(argv[i], "-scale") == 0) {
            if (++i < argc) {
                opt_scale = atof(argv[i]);
                if (opt_scale <= 0) {
                    fprintf(stderr, "Invalid scale: %s (must be positive)\n", argv[i]);
                    opt_scale = 2.0;
                }
                is_scale_set_by_user = 1;
            } else {
                fprintf(stderr, "Missing argument for -scale\n");
                die(USAGE);
            }
            continue;
        }
        if (strcmp(argv[i], "-rotate") == 0) {
            if (++i < argc) {
                int val = atoi(argv[i]);
                if (val == 0 || val == 90 || val == 180 || val == 270) {
                    opt_rotate = val;
                } else {
                    fprintf(stderr, "Invalid rotate: %s (allowed: 0,90,180,270)\n", argv[i]);
                    die(USAGE);
                }
            } else {
                fprintf(stderr, "Missing argument for -rotate\n");
                die(USAGE);
            }
            continue;
        }
        if (strcmp(argv[i], "-font") == 0) {
            if (++i < argc) {
                opt_font = argv[i];
                if (!is_scale_set_by_user) {
                    opt_scale = 1.0;  // if custom font is set, default scale to 1.0
                }

                if (strcmp(opt_font, "1") == 0) {
                    opt_font = NULL;
                    embedded_font_name = 1;
                    opt_scale = 2.0;
                } else if (strcmp(opt_font, "2") == 0) {
                    opt_font = NULL;
                    embedded_font_name = 2;
                    opt_scale = 2.0;
                } else if (strcmp(opt_font, "3") == 0) {
                    opt_font = NULL;
                    embedded_font_name = 3;
                    opt_scale = 2.0;
                } else if (strcmp(opt_font, "4") == 0) {
                    opt_font = NULL;
                    embedded_font_name = 4;
                    opt_scale = 1.0;
                } else if (strcmp(opt_font, "5") == 0) {
                    opt_font = NULL;
                    embedded_font_name = 5;
                    opt_scale = 1.0;
                }
            } else {
                fprintf(stderr, "Missing argument for -font\n");
                die(USAGE);
            }
            continue;
        }
        if (strcmp(argv[i], "-fontsize") == 0) {
            if (++i < argc) {
                opt_fontsize = atoi(argv[i]);
                if (opt_fontsize <= 0) {
                    fprintf(stderr, "Invalid fontsize: %s (must be positive)\n", argv[i]);
                    opt_fontsize = 0;
                }
            } else {
                fprintf(stderr, "Missing argument for -fontsize\n");
                die(USAGE);
            }
            continue;
        }
        if (strcmp(argv[i], "-fontshade") == 0) {
            if (++i < argc) {
                opt_fontshade = atoi(argv[i]);
            } else {
                fprintf(stderr, "Missing argument for -fontshade\n");
                die(USAGE);
            }
            continue;
        }
        if (strcmp(argv[i], "-useEmbeddedFontForKeyboard") == 0) {
            if (++i < argc) {
                opt_use_embedded_font_for_keyboard = atoi(argv[i]);
            } else {
                fprintf(stderr, "Missing argument for -useEmbeddedFontForKeyboard\n");
                die(USAGE);
            }
            continue;
        }

        switch (argv[i][0] != '-' || argv[i][2] ? -1 : argv[i][1]) {
            case 'r':  // run commands from arguments, must be at the end of argv
                if (++i < argc) {
                    opt_cmd = &argv[i];
                    opt_cmd_size = argc - i;
                    for (int j = 0; j < opt_cmd_size; j++) {
                        printf("Command to execute: %s\n", opt_cmd[j]);
                    }
                    show_help = 0;
                }
                break;
            case 'o':  // save output commands to file
                if (++i < argc) opt_io = argv[i];
                break;
            case 'q':  // quiet mode
                active = show_help = 0;
                break;
            case 'h':  // print help
            default:
                die(USAGE);
        }
    }

    if (atexit(sdl_shutdown)) {
        fprintf(stderr, "Unable to register SDL_Quit atexit\n");
    }

    sdl_init();
    {
        int content_w = main_window.surface ? main_window.surface->w : main_window.width;
        int content_h = main_window.surface ? main_window.surface->h : main_window.height;
        t_new((content_w - borderpx) / main_window.char_width, (content_h - borderpx) / main_window.char_height);
    }
    tty_new();
    create_tty_thread();
    scale_to_size((int)(main_window.width / opt_scale), (int)(main_window.height / opt_scale));
    init_keyboard(embedded_font_name, opt_use_embedded_font_for_keyboard);
    main_loop();
    return 0;
}
