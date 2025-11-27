#ifndef VT100_H
#define VT100_H

#include <stdbool.h>
#include <stddef.h>

/* VT100/Terminal related constants */
#define ESC_BUF_SIZ 256
#define ESC_ARG_SIZ 16
#define STR_BUF_SIZ 256
#define STR_ARG_SIZ 16
#define UTF_SIZ 4
#define VT102ID "\033[?6c"

/* VT100/Terminal macros */
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) < (b) ? (b) : (a))
#define LEN(a) (sizeof(a) / sizeof(a[0]))
#define DEFAULT(a, b) (a) = (a) ? (a) : (b)
#define BETWEEN(x, a, b) ((a) <= (x) && (x) <= (b))
#define LIMIT(x, a, b) (x) = (x)<(a) ? (a) : (x)>(b) ? (b) : (x)
#define ATTRCMP(a, b) ((a).mode != (b).mode || (a).fg != (b).fg || (a).bg != (b).bg)
#define IS_SET(flag) (term.mode & (flag))

/* Type definitions */
typedef unsigned char uchar;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef unsigned short ushort;

/* Glyph attributes */
enum glyph_attribute {
    ATTR_NULL = 0,
    ATTR_REVERSE = 1,
    ATTR_UNDERLINE = 2,
    ATTR_BOLD = 4,
    ATTR_GFX = 8,
    ATTR_ITALIC = 16,
    ATTR_BLINK = 32,
};

/* Cursor movements */
enum cursor_movement { CURSOR_UP, CURSOR_DOWN, CURSOR_LEFT, CURSOR_RIGHT, CURSOR_SAVE, CURSOR_LOAD };

/* Cursor states */
enum cursor_state { CURSOR_DEFAULT = 0, CURSOR_HIDE = 1, CURSOR_WRAPNEXT = 2 };

/* Glyph states */
enum glyph_state { GLYPH_SET = 1, GLYPH_DIRTY = 2 };

/* Terminal modes */
enum term_mode { MODE_WRAP = 1, MODE_INSERT = 2, MODE_APPKEYPAD = 4, MODE_ALTSCREEN = 8, MODE_CRLF = 16, MODE_MOUSEBTN = 32, MODE_MOUSEMOTION = 64, MODE_MOUSE = 32 | 64, MODE_REVERSE = 128, MODE_KBDLOCK = 256 };

/* Escape states */
enum escape_state {
    ESC_START = 1,
    ESC_CSI = 2,
    ESC_STR = 4, /* DSC, OSC, PM, APC */
    ESC_ALTCHARSET = 8,
    ESC_STR_END = 16, /* a final string was encountered */
    ESC_TEST = 32,    /* Enter in test mode */
};

/* Bit macros */
#undef B0
enum { B0 = 1, B1 = 2, B2 = 4, B3 = 8, B4 = 16, B5 = 32, B6 = 64, B7 = 128 };

/* Glyph structure */
typedef struct {
    char c[UTF_SIZ]; /* character code */
    uchar mode;      /* attribute flags */
    ushort fg;       /* foreground  */
    ushort bg;       /* background  */
    uchar state;     /* state flags    */
} Glyph;

typedef Glyph *Line;

/* Terminal cursor */
typedef struct {
    Glyph attr; /* current char attributes */
    int x;
    int y;
    char state;
} TCursor;

/* CSI Escape sequence structs */
/* ESC '[' [[ [<priv>] <arg> [;]] <mode>] */
typedef struct {
    char buf[ESC_BUF_SIZ]; /* raw string */
    int len;               /* raw string length */
    char priv;
    int arg[ESC_ARG_SIZ];
    int narg; /* nb of args */
    char mode;
} CSIEscape;

/* STR Escape sequence structs */
/* ESC type [[ [<priv>] <arg> [;]] <mode>] ESC '\' */
typedef struct {
    char type;             /* ESC type ... */
    char buf[STR_BUF_SIZ]; /* raw string */
    int len;               /* raw string length */
    char *args[STR_ARG_SIZ];
    int narg; /* nb of args */
} STREscape;

/* Internal representation of the screen */
typedef struct {
    int row;     /* nb row */
    int col;     /* nb col */
    Line *line;  /* screen */
    Line *alt;   /* alternate screen */
    bool *dirty; /* dirtyness of lines */
    TCursor c;   /* cursor */
    int top;     /* top    scroll limit */
    int bot;     /* bottom scroll limit */
    int mode;    /* terminal mode flags */
    int esc;     /* escape state flags */
    bool *tabs;
} Term;

/* Global terminal state - extern declarations */
extern Term term;
extern CSIEscape csiescseq;
extern STREscape strescseq;
extern int cmdfd;

/* TTY functions */
void tty_new(void);
void tty_read(void);
void tty_write(const char *s, size_t n);
void tty_resize(void);

/* Terminal functions */
void t_clear_region(int x1, int y1, int x2, int y2);
void t_cursor(int mode);
void t_delete_char(int n);
void t_delete_line(int n);
void t_insert_blank(int n);
void t_insert_blank_line(int n);
void t_move_to(int x, int y);
void t_new(int col, int row);
void t_newline(int first_col);
void t_put_tab(bool forward);
void t_putc(char *c, int len);
void t_reset(void);
int t_resize(int col, int row);
void t_scroll_up(int orig, int n);
void t_scroll_down(int orig, int n);
void t_set_attr(int *attr, int l);
void t_set_char(char *c, Glyph *attr, int x, int y);
void t_set_scroll(int t, int b);
void t_swap_screen(void);
void t_set_dirt(int top, int bot);
void t_set_mode(bool priv, bool set, int *args, int narg);
void t_full_dirt(void);

/* CSI/Escape sequence functions */
void csi_dump(void);
void csi_handle(void);
void csi_parse(void);
void csi_reset(void);
void str_reset(void);

/* UTF-8 functions */
int utf8_decode(char *c, long *u);
int utf8_encode(long *u, char *c);
int utf8_size(char *s);
int is_full_utf8(char *c, int len);

/* External dependencies from main.c */
void die(const char *, ...);
void *x_malloc(size_t);
void *x_realloc(void *, size_t);
void *x_calloc(size_t nmemb, size_t size);
size_t x_write(int fd, char *s, size_t len);
void redraw(void);

#endif /* VT100_H */