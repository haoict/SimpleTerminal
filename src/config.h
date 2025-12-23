/* See LICENSE file for copyright and license details. */

static int borderpx = 2;
char default_shell[] = "/bin/bash";

/* Scrollback configuration */
int scrollback_lines = 256;  /* Number of lines to keep in scrollback buffer */

static int initial_width = 320;
static int initial_height = 240;
static float opt_scale = 2.0;
static int opt_rotate = 0;   // rotation angle: 0, 90, 180, 270
static char *opt_font = NULL;  // "1" or "2" for embedded fonts, or path to TTF font file
static int opt_fontsize = 12;  // only used if opt_font is set to a TTF font
static int opt_fontshade = 0;  // 0=solid, 1=blended, 2=shaded, only used if opt_font is set to a TTF font
static int opt_use_embedded_font_for_keyboard = 0;

static const Uint32 BUTTON_HELD_DELAY = 150;  // milliseconds between button triggers when held

/* TERM value */
char termname[] = "xterm";

unsigned int tabspaces = 4;

/* Terminal colors (16 first used in escape sequence) */
SDL_Color colormap[] = {
    /* 8 normal colors */
    {0, 0, 0, 0},        // 0 "black" #000000
    {128, 0, 0, 0},      // 1 "red3" #800000
    {0, 128, 0, 0},      // 2 "green3" #008000
    {128, 128, 0, 0},    // 3 "yellow3" #808000
    {0, 0, 128, 0},      // 4 "blue2" #000080
    {128, 0, 128, 0},    // 5 "magenta3" #800080
    {0, 128, 128, 0},    // 6 "cyan3" #008080
    {192, 192, 192, 0},  // 7 "gray90" #C0C0C0

    /* 8 bright colors */
    {128, 128, 128, 0},  // 8 "gray50" #808080
    {255, 0, 0, 0},      // 9 "red" #FF0000
    {0, 255, 0, 0},      // 10 "green" #00FF00
    {255, 255, 0, 0},    // 11 "yellow" #FFFF00
    {0, 0, 255, 0},      // 12 "blue" #0000FF
    {255, 0, 255, 0},    // 13 "magenta" #FF00FF
    {0, 255, 255, 0},    // 14 "cyan" #00FFFF
    {255, 255, 255, 0},  // 15 "white" #FFFFFF

    [255] = {0, 0, 0, 0},

    /* more colors can be added after 255 to use with DefaultXX */
    {204, 204, 204, 0},  // 256 "gray80" #CCCCCC,
    {51, 51, 51, 0},     // 257 "gray20" #333333
    {16, 16, 16, 0},     // 258 "gray6" #101010
};

/*
 * Default colors (colorname index)
 * foreground, background, cursor, unfocused cursor
 */
unsigned int defaultfg = 7;
unsigned int defaultbg = 0;
unsigned int defaultcs = 256;
unsigned int defaultucs = 257;
