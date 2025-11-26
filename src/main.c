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

#define USAGE "Simple Terminal\nusage: simple-terminal [-h] [-scale 2.0] [-font font.ttf] [-fontsize 14] [-fontshade 0|1|2] [-o file] [-q] [-r command ...]\n"

/* Arbitrary sizes */
#define DRAW_BUF_SIZ 20 * 1024

#define REDRAW_TIMEOUT (80 * 1000) /* 80 ms */

/* macros */
#define TIMEDIFF(t1, t2) ((t1.tv_sec - t2.tv_sec) * 1000 + (t1.tv_usec - t2.tv_usec) / 1000)

enum window_state { WIN_VISIBLE = 1, WIN_REDRAW = 2, WIN_FOCUSED = 4 };

/* Purely graphic info */
typedef struct {
    // Colormap cmap;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Surface *surface;
    int width, height;         /* window width and height */
    int ttyWidth, ttyHeight;   /* tty width and height */
    int charHeight, charWidth; /* char height and width */
    char state;                /* focus, redraw, visible */
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

static NonPrintingKeyboardKey nonPrintingKeyboardKeys[] = {
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
SDL_Surface *oskScreen;

static void draw(void);
static void drawregion(int, int, int, int);
static void mainLoop(void);
int ttythread(void *unused);

static void xdraws(char *, Glyph, int, int, int, int);
static void xclear(int, int, int, int);
static void xdrawcursor(void);
static void sdlinit(void);
static void create_ttythread();
static void initcolormap(void);
static void sdltermclear(int, int, int, int);
static void xresize(int, int);
static void scale_to_size(int, int);
static char *kmap(SDL_Keycode, Uint16);
static void kpress(SDL_Event *);
static void textinput(SDL_Event *);
static void window_event_handler(SDL_Event *);

static void update_render(void);
static Uint32 clear_popup_timer(Uint32 interval, void *param);

static void (*event_handler[SDL_LASTEVENT])(SDL_Event *) = {[SDL_KEYDOWN] = kpress, [SDL_TEXTINPUT] = textinput, [SDL_WINDOWEVENT] = window_event_handler};

/* Globals */
static DrawingContext drawingContext;
static MainWindow mainwindow;
static SDL_Joystick *joystick;

SDL_Thread *thread = NULL;

char **opt_cmd = NULL;
int opt_cmd_size = 0;
char *opt_io = NULL;

static int embedded_font_name = 1;  // 1 or 2
static volatile int thread_should_exit = 0;
static int shutdown_called = 0;

char popupMessage[256];

size_t xwrite(int fd, char *s, size_t len) {
    size_t aux = len;

    while (len > 0) {
        ssize_t r = write(fd, s, len);
        if (r < 0) return r;
        len -= r;
        s += r;
    }
    return aux;
}

void *xmalloc(size_t len) {
    void *p = malloc(len);
    if (!p) die("Out of memory\n");
    return p;
}

void *xrealloc(void *p, size_t len) {
    if ((p = realloc(p, len)) == NULL) die("Out of memory\n");
    return p;
}

void *xcalloc(size_t nmemb, size_t size) {
    void *p = calloc(nmemb, size);
    if (!p) die("Out of memory\n");
    return p;
}

void sdlloadfonts() {
    // Try to load TTF font if opt_font is set
    if (opt_font && init_ttf_font(opt_font, opt_fontsize, opt_fontshade)) {
        mainwindow.charWidth = get_ttf_char_width();
        mainwindow.charHeight = get_ttf_char_height();
    } else {
        // Fallback to bitmap font
        mainwindow.charWidth = get_embedded_font_char_width(embedded_font_name);
        mainwindow.charHeight = get_embedded_font_char_height(embedded_font_name);
        fprintf(stderr, "Using embedded bitmap font %d (%dx%d)\n", embedded_font_name, mainwindow.charWidth, mainwindow.charHeight);
    }
}

void sdlshutdown(void) {
    if (SDL_WasInit(SDL_INIT_EVERYTHING) != 0 && !shutdown_called) {
        shutdown_called = 1;
        fprintf(stderr, "SDL shutting down\n");
        if (thread) {
            printf("Signaling ttythread to exit...\n");
            thread_should_exit = 1;
            // ttywrite n key to answer y/n question if blocked on ttyread
            ttywrite("n", 1);

            ttywrite("\033[?1000l", 7);    // disable mouse tracking to unblock ttyread
            SDL_WaitThread(thread, NULL);  // Wait for thread to exit cleanly
            // SDL_KillThread(thread);
            thread = NULL;
        }

        // Cleanup TTF font
        cleanup_ttf_font();

        if (mainwindow.surface) SDL_FreeSurface(mainwindow.surface);
        if (oskScreen) SDL_FreeSurface(oskScreen);
        mainwindow.surface = NULL;
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
    mainwindow.width = width;
    mainwindow.height = height;
    printf("Set scale to size: %dx%d (x%.1f)\n", mainwindow.width, mainwindow.height, opt_scale);

    // Recreate texture for new size
    if (mainwindow.texture) {
        SDL_DestroyTexture(mainwindow.texture);
    }
    mainwindow.texture = SDL_CreateTexture(mainwindow.renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, mainwindow.width, mainwindow.height);
    if (!mainwindow.texture) {
        fprintf(stderr, "Unable to recreate texture: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    // Recreate surfaces
    if (mainwindow.surface) SDL_FreeSurface(mainwindow.surface);
    mainwindow.surface = SDL_CreateRGBSurface(0, mainwindow.width, mainwindow.height, 16, 0xF800, 0x7E0, 0x1F, 0);  // console screen
    if (oskScreen) SDL_FreeSurface(oskScreen);
    oskScreen = SDL_CreateRGBSurface(0, mainwindow.width, mainwindow.height, 16, 0xF800, 0x7E0, 0x1F, 0);  // for keyboard mix

    // Recreate screen surface for compatibility
    if (screen) SDL_FreeSurface(screen);
    screen = SDL_CreateRGBSurface(0, 640, 480, 16, 0xF800, 0x7E0, 0x1F, 0);

    // resize terminal to fit window
    int col, row;
    col = (mainwindow.width - 2 * borderpx) / mainwindow.charWidth;
    row = (mainwindow.height - 2 * borderpx) / mainwindow.charHeight;
    tresize(col, row);
    xresize(col, row);
    ttyresize();
}

void sdlinit(void) {
    fprintf(stderr, "SDL init\n");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
        fprintf(stderr, "Unable to initialize SDL: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    SDL_ShowCursor(0);
    SDL_StartTextInput();

    /* font */
    sdlloadfonts();

    /* colors */
    initcolormap();

    int displayIndex = 0;  // usually 0 unless you have multiple screens
    SDL_DisplayMode mode;
    if (SDL_GetCurrentDisplayMode(displayIndex, &mode) != 0) {
        printf("SDL_GetCurrentDisplayMode failed: %s\n", SDL_GetError());
        mainwindow.width = initial_width;
        mainwindow.height = initial_height;
    } else {
        printf("Detected screen: %dx%d @ %dHz\n", mode.w, mode.h, mode.refresh_rate);
        mainwindow.width = mode.w;
        mainwindow.height = mode.h;
#ifndef BR2
        mainwindow.width = initial_width * 2;
        mainwindow.height = initial_height * 2;
#endif
        printf("Setting resolution to: %dx%d\n", mainwindow.width, mainwindow.height);
    }

    mainwindow.window = SDL_CreateWindow("Simple Terminal", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, mainwindow.width, mainwindow.height, SDL_WINDOW_SHOWN);
    if (!mainwindow.window) {
        fprintf(stderr, "Unable to create window: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    mainwindow.renderer = SDL_CreateRenderer(mainwindow.window, -1, SDL_RENDERER_ACCELERATED);
    if (!mainwindow.renderer) {
        mainwindow.renderer = SDL_CreateRenderer(mainwindow.window, -1, SDL_RENDERER_SOFTWARE);
        if (!mainwindow.renderer) {
            fprintf(stderr, "Unable to create renderer: %s\n", SDL_GetError());
            exit(EXIT_FAILURE);
        }
    }

    // SDL_RenderSetLogicalSize(mainwindow.renderer, mainwindow.width, mainwindow.height);

    mainwindow.texture = SDL_CreateTexture(mainwindow.renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, mainwindow.width, mainwindow.height);
    if (!mainwindow.texture) {
        fprintf(stderr, "Unable to create texture: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }
    mainwindow.surface = SDL_CreateRGBSurface(0, mainwindow.width, mainwindow.height, 16, 0xF800, 0x7E0, 0x1F, 0);  // console screen
    oskScreen = SDL_CreateRGBSurface(0, mainwindow.width, mainwindow.height, 16, 0xF800, 0x7E0, 0x1F, 0);           // for keyboard mix

    // Create a temporary surface for the screen to maintain compatibility
    screen = SDL_CreateRGBSurface(0, mainwindow.width, mainwindow.height, 16, 0xF800, 0x7E0, 0x1F, 0);

    mainwindow.state |= WIN_VISIBLE | WIN_REDRAW;

    joystick = SDL_JoystickOpen(0);
}

void create_ttythread() {
    // TODO: might need to use system threads
    if (!(thread = SDL_CreateThread(ttythread, "ttythread", NULL))) {
        fprintf(stderr, "Unable to create thread: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }
}

void update_render(void) {
    if (mainwindow.surface == NULL) return;

    memcpy(oskScreen->pixels, mainwindow.surface->pixels, mainwindow.width * mainwindow.height * 2);
    if (popupMessage[0] != '\0') {
        SDL_Rect rect = {borderpx, mainwindow.height / 2 - mainwindow.charHeight / 2 - 4, mainwindow.width - borderpx * 2, mainwindow.charHeight + 6};
        SDL_Color popupBoxBg = drawingContext.colors[8];
        SDL_Color popupBoxStr = drawingContext.colors[11];
        SDL_FillRect(oskScreen, &rect, SDL_MapRGB(oskScreen->format, popupBoxBg.r, popupBoxBg.g, popupBoxBg.b));
        draw_string(oskScreen, popupMessage, rect.x + 2, rect.y + 4, SDL_MapRGB(oskScreen->format, popupBoxStr.r, popupBoxStr.g, popupBoxStr.b), embedded_font_name);
    }
    draw_keyboard(oskScreen);  // oskScreen(SW) = console + keyboard
                               // Update texture with screen pixels and render
    SDL_UpdateTexture(mainwindow.texture, NULL, oskScreen->pixels, oskScreen->pitch);
    SDL_RenderClear(mainwindow.renderer);
    SDL_RenderCopy(mainwindow.renderer, mainwindow.texture, NULL, NULL);
    SDL_RenderPresent(mainwindow.renderer);
}

void die(const char *errstr, ...) {
    va_list ap;
    va_start(ap, errstr);
    vfprintf(stderr, errstr, ap);
    va_end(ap);
    sdlshutdown();
}

void xresize(int col, int row) {
    mainwindow.ttyWidth = MAX(1, 2 * borderpx + col * mainwindow.charWidth);
    mainwindow.ttyHeight = MAX(1, 2 * borderpx + row * mainwindow.charHeight);
}

void initcolormap(void) {
    int i, r, g, b;

    // TODO: allow these to override the xterm ones somehow?
    memcpy(drawingContext.colors, colormap, sizeof(drawingContext.colors));

    /* init colors [16-255] ; same colors as xterm */
    for (i = 16, r = 0; r < 6; r++) {
        for (g = 0; g < 6; g++) {
            for (b = 0; b < 6; b++) {
                drawingContext.colors[i].r = r == 0 ? 0 : 0x3737 + 0x2828 * r;
                drawingContext.colors[i].g = g == 0 ? 0 : 0x3737 + 0x2828 * g;
                drawingContext.colors[i].b = b == 0 ? 0 : 0x3737 + 0x2828 * b;
                i++;
            }
        }
    }

    for (r = 0; r < 24; r++, i++) {
        b = 0x0808 + 0x0a0a * r;
        drawingContext.colors[i].r = b;
        drawingContext.colors[i].g = b;
        drawingContext.colors[i].b = b;
    }
}

void sdltermclear(int col1, int row1, int col2, int row2) {
    if (mainwindow.surface == NULL) return;
    SDL_Rect r = {borderpx + col1 * mainwindow.charWidth, borderpx + row1 * mainwindow.charHeight, (col2 - col1 + 1) * mainwindow.charWidth, (row2 - row1 + 1) * mainwindow.charHeight};
    SDL_Color c = drawingContext.colors[IS_SET(MODE_REVERSE) ? defaultfg : defaultbg];
    SDL_FillRect(mainwindow.surface, &r, SDL_MapRGB(mainwindow.surface->format, c.r, c.g, c.b));
}

/*
 * Absolute coordinates.
 */
void xclear(int x1, int y1, int x2, int y2) {
    if (mainwindow.surface == NULL) return;
    SDL_Rect r = {x1, y1, x2 - x1, y2 - y1};
    SDL_Color c = drawingContext.colors[IS_SET(MODE_REVERSE) ? defaultfg : defaultbg];
    SDL_FillRect(mainwindow.surface, &r, SDL_MapRGB(mainwindow.surface->format, c.r, c.g, c.b));
}

void xdraws(char *s, Glyph base, int x, int y, int charlen, int bytelen) {
    int winx = borderpx + x * mainwindow.charWidth, winy = borderpx + y * mainwindow.charHeight, width = charlen * mainwindow.charWidth;
    // TTF_Font *font = drawingContext.font;
    SDL_Color *fg = &drawingContext.colors[base.fg], *bg = &drawingContext.colors[base.bg], *temp, revfg, revbg;

    s[bytelen] = '\0';

    if (base.mode & ATTR_BOLD) {
        if (BETWEEN(base.fg, 0, 7)) {
            /* basic system colors */
            fg = &drawingContext.colors[base.fg + 8];
        } else if (BETWEEN(base.fg, 16, 195)) {
            /* 256 colors */
            fg = &drawingContext.colors[base.fg + 36];
        } else if (BETWEEN(base.fg, 232, 251)) {
            /* greyscale */
            fg = &drawingContext.colors[base.fg + 4];
        }
        /*
         * Those ranges will not be brightened:
         *	8 - 15 – bright system colors
         *	196 - 231 – highest 256 color cube
         *	252 - 255 – brightest colors in greyscale
         */
        // font = drawingContext.bfont;
    }

    /*if(base.mode & ATTR_ITALIC)
        font = drawingContext.ifont;
    if((base.mode & ATTR_ITALIC) && (base.mode & ATTR_BOLD))
        font = drawingContext.ibfont;*/

    if (IS_SET(MODE_REVERSE)) {
        if (fg == &drawingContext.colors[defaultfg]) {
            fg = &drawingContext.colors[defaultbg];
        } else {
            revfg.r = ~fg->r;
            revfg.g = ~fg->g;
            revfg.b = ~fg->b;
            fg = &revfg;
        }

        if (bg == &drawingContext.colors[defaultbg]) {
            bg = &drawingContext.colors[defaultfg];
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
        xclear(0, (y == 0) ? 0 : winy, borderpx, winy + mainwindow.charHeight + (y == term.row - 1) ? mainwindow.height : 0);
    }
    if (x + charlen >= term.col - 1) {
        xclear(winx + width, (y == 0) ? 0 : winy, mainwindow.width, (y == term.row - 1) ? mainwindow.height : (winy + mainwindow.charHeight));
    }
    if (y == 0) xclear(winx, 0, winx + width, borderpx);
    if (y == term.row - 1) xclear(winx, winy + mainwindow.charHeight, winx + width, mainwindow.height);

    // SDL_Surface *text_surface;
    SDL_Rect r = {winx, winy, width, mainwindow.charHeight};

    if (mainwindow.surface != NULL) {
        SDL_FillRect(mainwindow.surface, &r, SDL_MapRGB(mainwindow.surface->format, bg->r, bg->g, bg->b));
        // TODO: find a better way to draw cursor box y + 1
        int ys = r.y + 1;
        if (is_ttf_loaded()) {
            // Use TTF rendering
            draw_string_ttf(mainwindow.surface, s, winx, winy, *fg, *bg);
        } else {
            // Use bitmap rendering
            draw_string(mainwindow.surface, s, winx, ys, SDL_MapRGB(mainwindow.surface->format, fg->r, fg->g, fg->b), embedded_font_name);
        }
    }

    if (base.mode & ATTR_UNDERLINE) {
        // r.y += TTF_FontAscent(font) + 1;
        r.y += mainwindow.charHeight;
        r.h = 1;
        if (mainwindow.surface != NULL) SDL_FillRect(mainwindow.surface, &r, SDL_MapRGB(mainwindow.surface->format, fg->r, fg->g, fg->b));
    }
}

void xdrawcursor(void) {
    static int oldx = 0, oldy = 0;
    int sl;
    Glyph g = {{' '}, ATTR_NULL, defaultbg, defaultcs, 0};

    LIMIT(oldx, 0, term.col - 1);
    LIMIT(oldy, 0, term.row - 1);

    if (term.line[term.c.y][term.c.x].state & GLYPH_SET) memcpy(g.c, term.line[term.c.y][term.c.x].c, UTF_SIZ);

    /* remove the old cursor */
    if (term.line[oldy][oldx].state & GLYPH_SET) {
        sl = utf8size(term.line[oldy][oldx].c);
        xdraws(term.line[oldy][oldx].c, term.line[oldy][oldx], oldx, oldy, 1, sl);
    } else {
        sdltermclear(oldx, oldy, oldx, oldy);
    }

    /* draw the new one */
    if (!(term.c.state & CURSOR_HIDE)) {
        if (!(mainwindow.state & WIN_FOCUSED)) g.bg = defaultucs;

        if (IS_SET(MODE_REVERSE)) g.mode |= ATTR_REVERSE, g.fg = defaultcs, g.bg = defaultfg;

        sl = utf8size(g.c);
        xdraws(g.c, g, term.c.x, term.c.y, 1, sl);
        oldx = term.c.x, oldy = term.c.y;
    }
}

void redraw(void) {
    struct timespec tv = {0, REDRAW_TIMEOUT * 1000};

    tfulldirt();
    draw();
    nanosleep(&tv, NULL);
}

void draw(void) {
    drawregion(0, 0, term.col, term.row);
    update_render();
}

void drawregion(int x1, int y1, int x2, int y2) {
    int ic, ib, x, y, ox, sl;
    Glyph base, new;
    char buf[DRAW_BUF_SIZ];

    if (!(mainwindow.state & WIN_VISIBLE)) return;

    for (y = y1; y < y2; y++) {
        if (!term.dirty[y]) continue;

        sdltermclear(0, y, term.col, y);
        term.dirty[y] = 0;
        base = term.line[y][0];
        ic = ib = ox = 0;
        for (x = x1; x < x2; x++) {
            new = term.line[y][x];
            if (ib > 0 && (!(new.state & GLYPH_SET) || ATTRCMP(base, new) || ib >= DRAW_BUF_SIZ - UTF_SIZ)) {
                xdraws(buf, base, ox, y, ic, ib);
                ic = ib = 0;
            }
            if (new.state & GLYPH_SET) {
                if (ib == 0) {
                    ox = x;
                    base = new;
                }
                sl = utf8size(new.c);
                memcpy(buf + ib, new.c, sl);
                ib += sl;
                ++ic;
            }
        }
        if (ib > 0) xdraws(buf, base, ox, y, ic, ib);
    }
    xdrawcursor();
}

char *kmap(SDL_Keycode k, Uint16 state) {
    int i;
    SDL_Keymod mask;

    for (i = 0; i < LEN(nonPrintingKeyboardKeys); i++) {
        mask = nonPrintingKeyboardKeys[i].mask;

        if (nonPrintingKeyboardKeys[i].k == k && ((state & mask) == mask || (mask == 0 && !state))) {
            return (char *)nonPrintingKeyboardKeys[i].s;
        }
    }
    return NULL;
}

void printNonPrintingKeyForDebug(char *nonPrintingKey, SDL_KeyboardEvent *e) {
    char escaped_seq[16] = {0};
    int idx = 0;
    for (int i = 0; nonPrintingKey[i] != '\0'; i++) {
        unsigned char ch = nonPrintingKey[i];
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

void kpress(SDL_Event *ev) {
    SDL_KeyboardEvent *e = &ev->key;
    char *nonPrintingKey;
    int meta, shift, ctrl, synth;
    SDL_Keycode ksym = e->keysym.sym;

    if (IS_SET(MODE_KBDLOCK)) return;

    meta = e->keysym.mod & KMOD_ALT;
    shift = e->keysym.mod & KMOD_SHIFT;
    ctrl = e->keysym.mod & KMOD_CTRL;
    synth = e->keysym.mod & KMOD_SYNTHETIC;

    // printf("kpress: keysym=%d scancode=%d mod=%d\n", ksym, e->keysym.scancode, e->keysym.mod);

    if ((nonPrintingKey = kmap(ksym, e->keysym.mod))) { /* 1. non printing keys from vt100.h */
        // printNonPrintingKeyForDebug(nonPrintingKey, e);
        ttywrite(nonPrintingKey, strlen(nonPrintingKey));
    } else if (ctrl && !meta && !shift) { /* 2. handle ctrl key */
        switch (ksym) {
            case SDLK_a:
                ttywrite("\001", 1);
                break;
            case SDLK_b:
                ttywrite("\002", 1);
                break;
            case SDLK_c:
                ttywrite("\003", 1);
                break;
            case SDLK_d:
                ttywrite("\004", 1);
                break;
            case SDLK_e:
                ttywrite("\005", 1);
                break;
            case SDLK_f:
                ttywrite("\006", 1);
                break;
            case SDLK_g:
                ttywrite("\007", 1);
                break;
            case SDLK_h:
                ttywrite("\010", 1);
                break;
            case SDLK_i:
                ttywrite("\011", 1);
                break;
            case SDLK_j:
                ttywrite("\012", 1);
                break;
            case SDLK_k:
                ttywrite("\013", 1);
                break;
            case SDLK_l:
                ttywrite("\014", 1);
                break;
            case SDLK_m:
                ttywrite("\015", 1);
                break;
            case SDLK_n:
                ttywrite("\016", 1);
                break;
            case SDLK_o:
                ttywrite("\017", 1);
                break;
            case SDLK_p:
                ttywrite("\020", 1);
                break;
            case SDLK_q:
                ttywrite("\021", 1);
                break;
            case SDLK_r:
                ttywrite("\022", 1);
                break;
            case SDLK_s:
                ttywrite("\023", 1);
                break;
            case SDLK_t:
                ttywrite("\024", 1);
                break;
            case SDLK_u:
                ttywrite("\025", 1);
                break;
            case SDLK_v:
                ttywrite("\026", 1);
                break;
            case SDLK_w:
                ttywrite("\027", 1);
                break;
            case SDLK_x:
                ttywrite("\030", 1);
                break;
            case SDLK_y:
                ttywrite("\031", 1);
                break;
            case SDLK_z:
                ttywrite("\032", 1);
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
                    ttywrite("\033", 1);
                }
                ttywrite(&ch, 1);
            }
        }
    }
    /* For printable keys, we handle text input separately with SDL_TEXTINPUT events */
}

void textinput(SDL_Event *ev) {
    SDL_TextInputEvent *e = &ev->text;
    ttywrite(e->text, strlen(e->text));
}

int ttythread(void *unused) {
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
            ttyread();

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
    popupMessage[0] = '\0';
    return 0;  // one-shot timer
}

void take_screenshot() {
    char filename[64];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(filename, sizeof(filename), "st-%y%m%d_%H%M%S.bmp", t);
    // get home directory
    const char *homeDir = getenv("HOME");
    if (homeDir != NULL) {
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "%s/%s", homeDir, filename);
        strcpy(filename, filepath);
    }

    if (mainwindow.surface) {
        if (SDL_SaveBMP(mainwindow.surface, filename) == 0) {
            sprintf(popupMessage, "Screenshot saved to %s", filename);
        } else {
            sprintf(popupMessage, "Failed to save screenshot: %s", SDL_GetError());
        }
    }

    // Clear the popup message after 3 seconds
    SDL_AddTimer(3000, clear_popup_timer, NULL);
}

void mainLoop(void) {
    SDL_Event ev;
    int running = 1;
    int buttonUpHeld = 0, buttonDownHeld = 0, buttonLeftHeld = 0, buttonRightHeld = 0;
    Uint32 lastButtonHeldTime = 0;
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
                        buttonLeftHeld = held;
                        break;
                    case KEY_RIGHT:
                        buttonRightHeld = held;
                        break;
                    case KEY_UP:
                        buttonUpHeld = held;
                        break;
                    case KEY_DOWN:
                        buttonDownHeld = held;
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
        if (buttonDownHeld)
            key = KEY_DOWN;
        else if (buttonUpHeld)
            key = KEY_UP;
        else if (buttonLeftHeld)
            key = KEY_LEFT;
        else if (buttonRightHeld)
            key = KEY_RIGHT;

        if (key && now - lastButtonHeldTime > BUTTON_HELD_DELAY) {
            handle_narrow_keys_held(key);
            lastButtonHeldTime = now;
        }

        update_render();  // redraw the screen
        SDL_Delay(33);    // ~30 FPS
    }

    sdlshutdown();
}

int main(int argc, char *argv[]) {
    setenv("SDL_NOMOUSE", "1", 1);
    int isScaleSetByUser = 0;

    for (int i = 1; i < argc; i++) {
        // Handle multi-character options first
        if (strcmp(argv[i], "-scale") == 0) {
            if (++i < argc) {
                opt_scale = atof(argv[i]);
                if (opt_scale <= 0) {
                    fprintf(stderr, "Invalid scale: %s (must be positive)\n", argv[i]);
                    opt_scale = 2.0;
                }
                isScaleSetByUser = 1;
            } else {
                fprintf(stderr, "Missing argument for -scale\n");
                die(USAGE);
            }
            continue;
        }
        if (strcmp(argv[i], "-font") == 0) {
            if (++i < argc) {
                opt_font = argv[i];
                if (!isScaleSetByUser) {
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
                opt_useEmbeddedFontForKeyboard = atoi(argv[i]);
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

    if (atexit(sdlshutdown)) {
        fprintf(stderr, "Unable to register SDL_Quit atexit\n");
    }

    sdlinit();
    tnew((mainwindow.width - borderpx) / mainwindow.charWidth, (mainwindow.height - borderpx) / mainwindow.charHeight);
    ttynew();
    create_ttythread();
    scale_to_size((int)(mainwindow.width / opt_scale), (int)(mainwindow.height / opt_scale));
    init_keyboard(embedded_font_name, opt_useEmbeddedFontForKeyboard);
    mainLoop();
    return 0;
}
