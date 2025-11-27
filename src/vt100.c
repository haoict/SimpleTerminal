#include "vt100.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

/* External variables from config.h */
extern unsigned int defaultfg;
extern unsigned int defaultbg;
extern unsigned int tabspaces;
extern char default_shell[];
extern char termname[];

/* External variables from main.c */
extern char *opt_io;
extern char **opt_cmd;
extern int opt_cmd_size;
extern int show_help;

/* VT100/Terminal global variables */
Term term;
CSIEscape csiescseq;
STREscape strescseq;
int cmdfd;
int iofd = -1;
static pid_t pid;

/* UTF-8 functions */
int utf8_decode(char *s, long *u) {
    uchar c;
    int i, n, rtn;

    rtn = 1;
    c = *s;
    if (~c & B7) { /* 0xxxxxxx */
        *u = c;
        return rtn;
    } else if ((c & (B7 | B6 | B5)) == (B7 | B6)) { /* 110xxxxx */
        *u = c & (B4 | B3 | B2 | B1 | B0);
        n = 1;
    } else if ((c & (B7 | B6 | B5 | B4)) == (B7 | B6 | B5)) { /* 1110xxxx */
        *u = c & (B3 | B2 | B1 | B0);
        n = 2;
    } else if ((c & (B7 | B6 | B5 | B4 | B3)) == (B7 | B6 | B5 | B4)) { /* 11110xxx */
        *u = c & (B2 | B1 | B0);
        n = 3;
    } else {
        goto invalid;
    }

    for (i = n, ++s; i > 0; --i, ++rtn, ++s) {
        c = *s;
        if ((c & (B7 | B6)) != B7) /* 10xxxxxx */
            goto invalid;
        *u <<= 6;
        *u |= c & (B5 | B4 | B3 | B2 | B1 | B0);
    }

    if ((n == 1 && *u < 0x80) || (n == 2 && *u < 0x800) || (n == 3 && *u < 0x10000) || (*u >= 0xD800 && *u <= 0xDFFF)) {
        goto invalid;
    }

    return rtn;
invalid:
    *u = 0xFFFD;

    return rtn;
}

int utf8_encode(long *u, char *s) {
    uchar *sp;
    ulong uc;
    int i, n;

    sp = (uchar *)s;
    uc = *u;
    if (uc < 0x80) {
        *sp = uc; /* 0xxxxxxx */
        return 1;
    } else if (*u < 0x800) {
        *sp = (uc >> 6) | (B7 | B6); /* 110xxxxx */
        n = 1;
    } else if (uc < 0x10000) {
        *sp = (uc >> 12) | (B7 | B6 | B5); /* 1110xxxx */
        n = 2;
    } else if (uc <= 0x10FFFF) {
        *sp = (uc >> 18) | (B7 | B6 | B5 | B4); /* 11110xxx */
        n = 3;
    } else {
        goto invalid;
    }

    for (i = n, ++sp; i > 0; --i, ++sp) *sp = ((uc >> 6 * (i - 1)) & (B5 | B4 | B3 | B2 | B1 | B0)) | B7; /* 10xxxxxx */

    return n + 1;
invalid:
    /* U+FFFD */
    *s++ = '\xEF';
    *s++ = '\xBF';
    *s = '\xBD';

    return 3;
}

/* use this if your buffer is less than UTF_SIZ, it returns 1 if you can decode
   UTF-8 otherwise return 0 */
int is_full_utf8(char *s, int b) {
    uchar *c1, *c2, *c3;

    c1 = (uchar *)s;
    c2 = (uchar *)++s;
    c3 = (uchar *)++s;
    if (b < 1) {
        return 0;
    } else if ((*c1 & (B7 | B6 | B5)) == (B7 | B6) && b == 1) {
        return 0;
    } else if ((*c1 & (B7 | B6 | B5 | B4)) == (B7 | B6 | B5) && ((b == 1) || ((b == 2) && (*c2 & (B7 | B6)) == B7))) {
        return 0;
    } else if ((*c1 & (B7 | B6 | B5 | B4 | B3)) == (B7 | B6 | B5 | B4) && ((b == 1) || ((b == 2) && (*c2 & (B7 | B6)) == B7) || ((b == 3) && (*c2 & (B7 | B6)) == B7 && (*c3 & (B7 | B6)) == B7))) {
        return 0;
    } else {
        return 1;
    }
}

int utf8_size(char *s) {
    uchar c = *s;

    if (~c & B7) {
        return 1;
    } else if ((c & (B7 | B6 | B5)) == (B7 | B6)) {
        return 2;
    } else if ((c & (B7 | B6 | B5 | B4)) == (B7 | B6 | B5)) {
        return 3;
    } else {
        return 4;
    }
}

/* External functions needed by VT100 */
void exec_sh(void) {
    char **args;
    char *envshell = getenv("SHELL");

    unsetenv("COLUMNS");
    unsetenv("LINES");
    unsetenv("TERMCAP");
    chdir(getenv("HOME"));

    if (show_help != 0) {
        system("uname -a");
        system("echo '\n'");
    }

    signal(SIGCHLD, SIG_DFL);
    signal(SIGHUP, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGALRM, SIG_DFL);

    DEFAULT(envshell, default_shell);
    setenv("TERM", termname, 1);
    args = (char *[]){envshell, "-i", NULL};

    // executing opt_cmd
    for (int i = 0; i < opt_cmd_size; i++) {
        char echo_cmd[255];
        sprintf(echo_cmd, "echo '\n$ %s\n'", opt_cmd[i]);
        system(echo_cmd);
        system(opt_cmd[i]);
    }

    execvp(args[0], args);
    exit(EXIT_FAILURE);
}

void sig_chld(int a) {
    int stat = 0;
    (void)a;

    if (waitpid(pid, &stat, 0) < 0) die("Waiting for pid %hd failed: %s\n", pid, strerror(errno));

    if (WIFEXITED(stat)) {
        exit(WEXITSTATUS(stat));
    } else {
        exit(EXIT_FAILURE);
    }
}

void tty_new(void) {
    int m, s;
    struct winsize w = {term.row, term.col, 0, 0};

    /* seems to work fine on linux, openbsd and freebsd */
    if (openpty(&m, &s, NULL, NULL, &w) < 0) die("openpty failed: %s\n", strerror(errno));

    switch (pid = fork()) {
        case -1:
            die("fork failed\n");
            break;
        case 0:
            setsid(); /* create a new process group */
            dup2(s, STDIN_FILENO);
            dup2(s, STDOUT_FILENO);
            dup2(s, STDERR_FILENO);
            if (ioctl(s, TIOCSCTTY, NULL) < 0) die("ioctl TIOCSCTTY failed: %s\n", strerror(errno));
            close(s);
            close(m);
            exec_sh();
            break;
        default:
            close(s);
            cmdfd = m;
            signal(SIGCHLD, sig_chld);
            if (opt_io) {
                iofd = (!strcmp(opt_io, "-")) ? STDOUT_FILENO : open(opt_io, O_WRONLY | O_CREAT, 0666);
                if (iofd < 0) {
                    fprintf(stderr, "Error opening %s:%s\n", opt_io, strerror(errno));
                }
            }
    }
}

void dump(char c) {
    static int col;

    fprintf(stderr, " %02x '%c' ", c, isprint(c) ? c : '.');
    if (++col % 10 == 0) fprintf(stderr, "\n");
}

void tty_read(void) {
    static char buf[BUFSIZ];
    static int buflen = 0;
    char *ptr;
    char s[UTF_SIZ];
    int charsize; /* size of utf8 char in bytes */
    long utf8c;
    int ret;

    /* append read bytes to unprocessed bytes */
    if ((ret = read(cmdfd, buf + buflen, LEN(buf) - buflen)) < 0) die("Couldn't read from shell: %s\n", strerror(errno));

    /* process every complete utf8 char */
    buflen += ret;
    ptr = buf;
    while (buflen >= UTF_SIZ || is_full_utf8(ptr, buflen)) {
        charsize = utf8_decode(ptr, &utf8c);
        utf8_encode(&utf8c, s);
        t_putc(s, charsize);
        ptr += charsize;
        buflen -= charsize;
    }

    /* keep any uncomplete utf8 char for the next call */
    memmove(buf, ptr, buflen);
}

void tty_write(const char *s, size_t n) {
    if (write(cmdfd, s, n) == -1) die("write error on tty: %s\n", strerror(errno));
}

void tty_resize(void) {
    struct winsize w;

    w.ws_row = term.row;
    w.ws_col = term.col;
    w.ws_xpixel = 0; /* mainwindow.tw */
    w.ws_ypixel = 0; /* mainwindow.th */
    if (ioctl(cmdfd, TIOCSWINSZ, &w) < 0) fprintf(stderr, "Couldn't set window size: %s\n", strerror(errno));
}

void t_set_dirt(int top, int bot) {
    int i;

    LIMIT(top, 0, term.row - 1);
    LIMIT(bot, 0, term.row - 1);

    for (i = top; i <= bot; i++) term.dirty[i] = 1;
}

void t_full_dirt(void) { t_set_dirt(0, term.row - 1); }

void t_cursor(int mode) {
    static TCursor c[2];  // Separate cursor save for primary[0] and alt[1] screens

    if (mode == CURSOR_SAVE) {
        int screen_idx = IS_SET(MODE_ALTSCREEN) ? 1 : 0;
        c[screen_idx] = term.c;
    } else if (mode == CURSOR_LOAD) {
        int screen_idx = IS_SET(MODE_ALTSCREEN) ? 1 : 0;
        term.c = c[screen_idx];
        t_move_to(c[screen_idx].x, c[screen_idx].y);
    }
}

void t_reset(void) {
    uint i;

    term.c = (TCursor){{.mode = ATTR_NULL, .fg = defaultfg, .bg = defaultbg}, .x = 0, .y = 0, .state = CURSOR_DEFAULT};

    memset(term.tabs, 0, term.col * sizeof(*term.tabs));
    for (i = tabspaces; i < term.col; i += tabspaces) term.tabs[i] = 1;
    term.top = 0;
    term.bot = term.row - 1;
    term.mode = MODE_WRAP;

    t_clear_region(0, 0, term.col - 1, term.row - 1);
}

void t_new(int col, int row) {
    /* set screen size */
    term.row = row;
    term.col = col;
    term.line = x_malloc(term.row * sizeof(Line));
    term.alt = x_malloc(term.row * sizeof(Line));
    term.dirty = x_malloc(term.row * sizeof(*term.dirty));
    term.tabs = x_malloc(term.col * sizeof(*term.tabs));

    for (row = 0; row < term.row; row++) {
        term.line[row] = x_malloc(term.col * sizeof(Glyph));
        term.alt[row] = x_malloc(term.col * sizeof(Glyph));
        term.dirty[row] = 0;
    }
    memset(term.tabs, 0, term.col * sizeof(*term.tabs));
    /* setup screen */
    t_reset();
}

void t_swap_screen(void) {
    Line *tmp = term.line;

    term.line = term.alt;
    term.alt = tmp;
    term.mode ^= MODE_ALTSCREEN;
    t_full_dirt();

    // Ensure cursor is within bounds after swap
    LIMIT(term.c.x, 0, term.col - 1);
    LIMIT(term.c.y, 0, term.row - 1);

    redraw();  // Force immediate redraw after screen swap
}

void t_scroll_down(int orig, int n) {
    int i;
    Line temp;

    LIMIT(n, 0, term.bot - orig + 1);

    t_clear_region(0, term.bot - n + 1, term.col - 1, term.bot);

    for (i = term.bot; i >= orig + n; i--) {
        temp = term.line[i];
        term.line[i] = term.line[i - n];
        term.line[i - n] = temp;

        term.dirty[i] = 1;
        term.dirty[i - n] = 1;
    }
}

void t_scroll_up(int orig, int n) {
    int i;
    Line temp;
    LIMIT(n, 0, term.bot - orig + 1);

    t_clear_region(0, orig, term.col - 1, orig + n - 1);

    for (i = orig; i <= term.bot - n; i++) {
        temp = term.line[i];
        term.line[i] = term.line[i + n];
        term.line[i + n] = temp;

        term.dirty[i] = 1;
        term.dirty[i + n] = 1;
    }
}

void t_newline(int first_col) {
    int y = term.c.y;

    if (y == term.bot) {
        t_scroll_up(term.top, 1);
    } else {
        y++;
    }
    t_move_to(first_col ? 0 : term.c.x, y);
}

void csi_parse(void) {
    /* int noarg = 1; */
    char *p = csiescseq.buf;

    csiescseq.narg = 0;
    if (*p == '?') csiescseq.priv = 1, p++;

    while (p < csiescseq.buf + csiescseq.len) {
        while (isdigit(*p)) {
            csiescseq.arg[csiescseq.narg] *= 10;
            csiescseq.arg[csiescseq.narg] += *p++ - '0' /*, noarg = 0 */;
        }
        if (*p == ';' && csiescseq.narg + 1 < ESC_ARG_SIZ) {
            csiescseq.narg++, p++;
        } else {
            csiescseq.mode = *p;
            csiescseq.narg++;

            return;
        }
    }
}

void t_move_to(int x, int y) {
    LIMIT(x, 0, term.col - 1);
    LIMIT(y, 0, term.row - 1);
    term.c.state &= ~CURSOR_WRAPNEXT;
    term.c.x = x;
    term.c.y = y;
}

void t_set_char(char *c, Glyph *attr, int x, int y) {
    static char *vt100_0[62] = {
        /* 0x41 - 0x7e */
        "↑", "↓", "→", "←", "█", "▚", "☃",      /* A - G */
        0,   0,   0,   0,   0,   0,   0,   0,   /* H - O */
        0,   0,   0,   0,   0,   0,   0,   0,   /* P - W */
        0,   0,   0,   0,   0,   0,   0,   " ", /* X - _ */
        "◆", "▒", "␉", "␌", "␍", "␊", "°", "±", /* ` - g */
        "␤", "␋", "┘", "┐", "┌", "└", "┼", "⎺", /* h - o */
        "⎻", "─", "⎼", "⎽", "├", "┤", "┴", "┬", /* p - w */
        "│", "≤", "≥", "π", "≠", "£", "·",      /* x - ~ */
    };

    /*
     * The table is proudly stolen from rxvt.
     */
    if (attr->mode & ATTR_GFX) {
        if (c[0] >= 0x41 && c[0] <= 0x7e && vt100_0[c[0] - 0x41]) {
            c = vt100_0[c[0] - 0x41];
        }
    }

    term.dirty[y] = 1;
    term.line[y][x] = *attr;
    memcpy(term.line[y][x].c, c, UTF_SIZ);
    term.line[y][x].state |= GLYPH_SET;
}

void t_clear_region(int x1, int y1, int x2, int y2) {
    int x, y, temp;

    if (x1 > x2) temp = x1, x1 = x2, x2 = temp;
    if (y1 > y2) temp = y1, y1 = y2, y2 = temp;

    LIMIT(x1, 0, term.col - 1);
    LIMIT(x2, 0, term.col - 1);
    LIMIT(y1, 0, term.row - 1);
    LIMIT(y2, 0, term.row - 1);

    for (y = y1; y <= y2; y++) {
        term.dirty[y] = 1;
        for (x = x1; x <= x2; x++) term.line[y][x].state = 0;
    }
}

void t_delete_char(int n) {
    int src = term.c.x + n;
    int dst = term.c.x;
    int size = term.col - src;

    term.dirty[term.c.y] = 1;

    if (src >= term.col) {
        t_clear_region(term.c.x, term.c.y, term.col - 1, term.c.y);
        return;
    }

    memmove(&term.line[term.c.y][dst], &term.line[term.c.y][src], size * sizeof(Glyph));
    t_clear_region(term.col - n, term.c.y, term.col - 1, term.c.y);
}

void t_insert_blank(int n) {
    int src = term.c.x;
    int dst = src + n;
    int size = term.col - dst;

    term.dirty[term.c.y] = 1;

    if (dst >= term.col) {
        t_clear_region(term.c.x, term.c.y, term.col - 1, term.c.y);
        return;
    }

    memmove(&term.line[term.c.y][dst], &term.line[term.c.y][src], size * sizeof(Glyph));
    t_clear_region(src, term.c.y, dst - 1, term.c.y);
}

void t_insert_blank_line(int n) {
    if (term.c.y < term.top || term.c.y > term.bot) return;

    t_scroll_down(term.c.y, n);
}

void t_delete_line(int n) {
    if (term.c.y < term.top || term.c.y > term.bot) return;

    t_scroll_up(term.c.y, n);
}

void t_set_attr(int *attr, int l) {
    int i;

    for (i = 0; i < l; i++) {
        switch (attr[i]) {
            case 0:
                term.c.attr.mode &= ~(ATTR_REVERSE | ATTR_UNDERLINE | ATTR_BOLD | ATTR_ITALIC | ATTR_BLINK);
                term.c.attr.fg = defaultfg;
                term.c.attr.bg = defaultbg;
                break;
            case 1:
                term.c.attr.mode |= ATTR_BOLD;
                break;
            case 3: /* enter standout (highlight) */
                term.c.attr.mode |= ATTR_ITALIC;
                break;
            case 4:
                term.c.attr.mode |= ATTR_UNDERLINE;
                break;
            case 5:
                term.c.attr.mode |= ATTR_BLINK;
                break;
            case 7:
                term.c.attr.mode |= ATTR_REVERSE;
                break;
            case 21:
            case 22:
                term.c.attr.mode &= ~ATTR_BOLD;
                break;
            case 23: /* leave standout (highlight) mode */
                term.c.attr.mode &= ~ATTR_ITALIC;
                break;
            case 24:
                term.c.attr.mode &= ~ATTR_UNDERLINE;
                break;
            case 25:
                term.c.attr.mode &= ~ATTR_BLINK;
                break;
            case 27:
                term.c.attr.mode &= ~ATTR_REVERSE;
                break;
            case 29: /* not crossed out (most terminals don't support crossed out anyway) */
                /* ignore - we don't have a crossed out attribute to unset */
                break;
            case 38:
                if (i + 2 < l && attr[i + 1] == 5) {
                    i += 2;
                    if (BETWEEN(attr[i], 0, 255)) {
                        term.c.attr.fg = attr[i];
                    } else {
                        fprintf(stderr, "erresc: bad fgcolor %d\n", attr[i]);
                    }
                } else {
                    fprintf(stderr, "erresc(38): gfx attr %d unknown\n", attr[i]);
                }
                break;
            case 39:
                term.c.attr.fg = defaultfg;
                break;
            case 48:
                if (i + 2 < l && attr[i + 1] == 5) {
                    i += 2;
                    if (BETWEEN(attr[i], 0, 255)) {
                        term.c.attr.bg = attr[i];
                    } else {
                        fprintf(stderr, "erresc: bad bgcolor %d\n", attr[i]);
                    }
                } else {
                    fprintf(stderr, "erresc(48): gfx attr %d unknown\n", attr[i]);
                }
                break;
            case 49:
                term.c.attr.bg = defaultbg;
                break;
            default:
                if (BETWEEN(attr[i], 30, 37)) {
                    term.c.attr.fg = attr[i] - 30;
                } else if (BETWEEN(attr[i], 40, 47)) {
                    term.c.attr.bg = attr[i] - 40;
                } else if (BETWEEN(attr[i], 90, 97)) {
                    term.c.attr.fg = attr[i] - 90 + 8;
                } else if (BETWEEN(attr[i], 100, 107)) {
                    term.c.attr.bg = attr[i] - 100 + 8;
                } else {
                    fprintf(stderr, "erresc(default): gfx attr %d unknown\n", attr[i]), csi_dump();
                }
                break;
        }
    }
}

void t_set_scroll(int t, int b) {
    int temp;

    LIMIT(t, 0, term.row - 1);
    LIMIT(b, 0, term.row - 1);
    if (t > b) {
        temp = t;
        t = b;
        b = temp;
    }
    term.top = t;
    term.bot = b;
}

#define MODBIT(x, set, bit) ((set) ? ((x) |= (bit)) : ((x) &= ~(bit)))

void t_set_mode(bool priv, bool set, int *args, int narg) {
    int *lim, mode;

    for (lim = args + narg; args < lim; ++args) {
        if (priv) {
            switch (*args) {
                break;
                case 1: /* DECCKM -- Cursor key */
                    MODBIT(term.mode, set, MODE_APPKEYPAD);
                    break;
                case 5: /* DECSCNM -- Reverse video */
                    mode = term.mode;
                    MODBIT(term.mode, set, MODE_REVERSE);
                    if (mode != term.mode) redraw();
                    break;
                case 6: /* XXX: DECOM -- Origin */
                    break;
                case 7: /* DECAWM -- Auto wrap */
                    MODBIT(term.mode, set, MODE_WRAP);
                    break;
                case 8: /* XXX: DECARM -- Auto repeat */
                    break;
                case 0:  /* Error (IGNORED) */
                case 12: /* att610 -- Start blinking cursor (IGNORED) */
                    break;
                case 25:
                    MODBIT(term.c.state, !set, CURSOR_HIDE);
                    break;
                case 1000: /* 1000,1002: enable xterm mouse report */
                    MODBIT(term.mode, set, MODE_MOUSEBTN);
                    break;
                case 1002:
                    MODBIT(term.mode, set, MODE_MOUSEMOTION);
                    break;
                case 1049: /* = 1047 and 1048 */
                    // Mode 1049 combines screen switching AND cursor save/restore
                    if (set) {
                        // Save cursor position on primary screen, then switch to alternate screen
                        if (!IS_SET(MODE_ALTSCREEN)) {
                            t_cursor(CURSOR_SAVE);  // Save on primary screen
                            t_swap_screen();        // Switch to alternate
                            t_move_to(0, 0);        // Move to top-left on alternate screen
                        }
                    } else {
                        // Switch back to primary screen, then restore cursor position
                        if (IS_SET(MODE_ALTSCREEN)) {
                            t_swap_screen();        // Switch back to primary
                            t_cursor(CURSOR_LOAD);  // Restore on primary screen
                        }
                    }
                    break;
                case 47:
                case 1047:
                    // Alternate screen buffer only (no cursor save/restore)
                    if (set) {
                        // Switch to alternate screen
                        if (!IS_SET(MODE_ALTSCREEN)) {
                            t_swap_screen();
                        }
                    } else {
                        // Switch back to primary screen
                        if (IS_SET(MODE_ALTSCREEN)) {
                            t_swap_screen();
                        }
                    }
                    break;
                case 1048:
                    // Cursor save/restore only
                    t_cursor((set) ? CURSOR_SAVE : CURSOR_LOAD);
                    break;
                case 2004: /* bracketed paste mode */
                    // MODBIT(term.mode, set, MODE_BRACKETPASTE);
                    break;
                default:
                    /* case 2:  DECANM -- ANSI/VT52 (NOT SUPPOURTED) */
                    /* case 3:  DECCOLM -- Column  (NOT SUPPORTED) */
                    /* case 4:  DECSCLM -- Scroll (NOT SUPPORTED) */
                    /* case 18: DECPFF -- Printer feed (NOT SUPPORTED) */
                    /* case 19: DECPEX -- Printer extent (NOT SUPPORTED) */
                    /* case 42: DECNRCM -- National characters (NOT SUPPORTED) */
                    fprintf(stderr, "erresc: unknown private set/reset mode %d\n", *args);
                    break;
            }
        } else {
            switch (*args) {
                case 0: /* Error (IGNORED) */
                    break;
                case 2: /* KAM -- keyboard action */
                    MODBIT(term.mode, set, MODE_KBDLOCK);
                    break;
                case 4: /* IRM -- Insertion-replacement */
                    MODBIT(term.mode, set, MODE_INSERT);
                    break;
                case 12: /* XXX: SRM -- Send/Receive */
                    break;
                case 20: /* LNM -- Linefeed/new line */
                    MODBIT(term.mode, set, MODE_CRLF);
                    break;
                default:
                    fprintf(stderr, "erresc: unknown set/reset mode %d\n", *args);
                    break;
            }
        }
    }
}
#undef MODBIT

void csi_handle(void) {
    switch (csiescseq.mode) {
        case 't': /* Window manipulation */
            // See: https://invisible-island.net/xterm/ctlseqs/ctlseqs.html#h2-Window-manipulation
            if (csiescseq.narg > 0) {
                int op = csiescseq.arg[0];
                char buf[64];
                switch (op) {
                    case 18:  // Report window size in pixels
                        // Response: ESC [ 4 ; height ; width t
                        snprintf(buf, sizeof(buf), "\033[4;%d;%dt", term.row * 16, term.col * 8);
                        tty_write(buf, strlen(buf));
                        break;
                    case 19:  // Report window size in characters
                        // Response: ESC [ 8 ; height ; width t
                        snprintf(buf, sizeof(buf), "\033[8;%d;%dt", term.row, term.col);
                        tty_write(buf, strlen(buf));
                        break;
                    case 22:  // Push window title to stack (ignore for now)
                    case 23:  // Pop window title from stack (ignore for now)
                    default:
                        // For other window ops, ignore silently
                        break;
                }
            }
            break;
        default:
        unknown:
            fprintf(stderr, "erresc: unknown csi ");
            csi_dump();
            /* die(""); */
            break;
        case '@': /* ICH -- Insert <n> blank char */
            DEFAULT(csiescseq.arg[0], 1);
            t_insert_blank(csiescseq.arg[0]);
            break;
        case 'A': /* CUU -- Cursor <n> Up */
        case 'e':
            DEFAULT(csiescseq.arg[0], 1);
            t_move_to(term.c.x, term.c.y - csiescseq.arg[0]);
            break;
        case 'B': /* CUD -- Cursor <n> Down */
            DEFAULT(csiescseq.arg[0], 1);
            t_move_to(term.c.x, term.c.y + csiescseq.arg[0]);
            break;
        case 'c': /* DA -- Device Attributes */
            if (csiescseq.arg[0] == 0) tty_write(VT102ID, sizeof(VT102ID) - 1);
            break;
        case 'C': /* CUF -- Cursor <n> Forward */
        case 'a':
            DEFAULT(csiescseq.arg[0], 1);
            t_move_to(term.c.x + csiescseq.arg[0], term.c.y);
            break;
        case 'D': /* CUB -- Cursor <n> Backward */
            DEFAULT(csiescseq.arg[0], 1);
            t_move_to(term.c.x - csiescseq.arg[0], term.c.y);
            break;
        case 'E': /* CNL -- Cursor <n> Down and first col */
            DEFAULT(csiescseq.arg[0], 1);
            t_move_to(0, term.c.y + csiescseq.arg[0]);
            break;
        case 'F': /* CPL -- Cursor <n> Up and first col */
            DEFAULT(csiescseq.arg[0], 1);
            t_move_to(0, term.c.y - csiescseq.arg[0]);
            break;
        case 'g': /* TBC -- Tabulation clear */
            switch (csiescseq.arg[0]) {
                case 0: /* clear current tab stop */
                    term.tabs[term.c.x] = 0;
                    break;
                case 3: /* clear all the tabs */
                    memset(term.tabs, 0, term.col * sizeof(*term.tabs));
                    break;
                default:
                    goto unknown;
            }
            break;
        case 'G': /* CHA -- Move to <col> */
        case '`': /* HPA */
            DEFAULT(csiescseq.arg[0], 1);
            t_move_to(csiescseq.arg[0] - 1, term.c.y);
            break;
        case 'H': /* CUP -- Move to <row> <col> */
        case 'f': /* HVP */
            DEFAULT(csiescseq.arg[0], 1);
            DEFAULT(csiescseq.arg[1], 1);
            t_move_to(csiescseq.arg[1] - 1, csiescseq.arg[0] - 1);
            break;
        case 'I': /* CHT -- Cursor Forward Tabulation <n> tab stops */
            DEFAULT(csiescseq.arg[0], 1);
            while (csiescseq.arg[0]--) t_put_tab(1);
            break;
        case 'J': /* ED -- Clear screen */
            switch (csiescseq.arg[0]) {
                case 0: /* below */
                    t_clear_region(term.c.x, term.c.y, term.col - 1, term.c.y);
                    if (term.c.y < term.row - 1) t_clear_region(0, term.c.y + 1, term.col - 1, term.row - 1);
                    break;
                case 1: /* above */
                    if (term.c.y > 1) t_clear_region(0, 0, term.col - 1, term.c.y - 1);
                    t_clear_region(0, term.c.y, term.c.x, term.c.y);
                    break;
                case 2: /* all */
                    t_clear_region(0, 0, term.col - 1, term.row - 1);
                    break;
                default:
                    goto unknown;
            }
            break;
        case 'K': /* EL -- Clear line */
            switch (csiescseq.arg[0]) {
                case 0: /* right */
                    t_clear_region(term.c.x, term.c.y, term.col - 1, term.c.y);
                    break;
                case 1: /* left */
                    t_clear_region(0, term.c.y, term.c.x, term.c.y);
                    break;
                case 2: /* all */
                    t_clear_region(0, term.c.y, term.col - 1, term.c.y);
                    break;
            }
            break;
        case 'S': /* SU -- Scroll <n> line up */
            DEFAULT(csiescseq.arg[0], 1);
            t_scroll_up(term.top, csiescseq.arg[0]);
            break;
        case 'T': /* SD -- Scroll <n> line down */
            DEFAULT(csiescseq.arg[0], 1);
            t_scroll_down(term.top, csiescseq.arg[0]);
            break;
        case 'L': /* IL -- Insert <n> blank lines */
            DEFAULT(csiescseq.arg[0], 1);
            t_insert_blank_line(csiescseq.arg[0]);
            break;
        case 'l': /* RM -- Reset Mode */
            t_set_mode(csiescseq.priv, 0, csiescseq.arg, csiescseq.narg);
            break;
        case 'M': /* DL -- Delete <n> lines */
            DEFAULT(csiescseq.arg[0], 1);
            t_delete_line(csiescseq.arg[0]);
            break;
        case 'X': /* ECH -- Erase <n> char */
            DEFAULT(csiescseq.arg[0], 1);
            t_clear_region(term.c.x, term.c.y, term.c.x + csiescseq.arg[0], term.c.y);
            break;
        case 'P': /* DCH -- Delete <n> char */
            DEFAULT(csiescseq.arg[0], 1);
            t_delete_char(csiescseq.arg[0]);
            break;
        case 'Z': /* CBT -- Cursor Backward Tabulation <n> tab stops */
            DEFAULT(csiescseq.arg[0], 1);
            while (csiescseq.arg[0]--) t_put_tab(0);
            break;
        case 'd': /* VPA -- Move to <row> */
            DEFAULT(csiescseq.arg[0], 1);
            t_move_to(term.c.x, csiescseq.arg[0] - 1);
            break;
        case 'h': /* SM -- Set terminal mode */
            t_set_mode(csiescseq.priv, 1, csiescseq.arg, csiescseq.narg);
            break;
        case 'm': /* SGR -- Terminal attribute (color) */
            if (csiescseq.buf[0] == '>') {
                // Handle private SGR sequences like ESC[>4;2m (bracketed paste mode queries)
                // These are usually capability queries that we can safely ignore
                break;
            }
            t_set_attr(csiescseq.arg, csiescseq.narg);
            break;
        case 'r': /* DECSTBM -- Set Scrolling Region */
            if (csiescseq.priv) {
                goto unknown;
            } else {
                DEFAULT(csiescseq.arg[0], 1);
                DEFAULT(csiescseq.arg[1], term.row);
                t_set_scroll(csiescseq.arg[0] - 1, csiescseq.arg[1] - 1);
                t_move_to(0, 0);
            }
            break;
        case 's': /* DECSC -- Save cursor position (ANSI.SYS) */
            t_cursor(CURSOR_SAVE);
            break;
        case 'u': /* DECRC -- Restore cursor position (ANSI.SYS) */
            t_cursor(CURSOR_LOAD);
            break;
    }
}

void csi_dump(void) {
    int i;
    uint c;

    printf("ESC[");
    for (i = 0; i < csiescseq.len; i++) {
        c = csiescseq.buf[i] & 0xff;
        if (isprint(c)) {
            putchar(c);
        } else if (c == '\n') {
            printf("(\\n)");
        } else if (c == '\r') {
            printf("(\\r)");
        } else if (c == 0x1b) {
            printf("(\\e)");
        } else {
            printf("(%02x)", c);
        }
    }
    putchar('\n');
}

void csi_reset(void) { memset(&csiescseq, 0, sizeof(csiescseq)); }

void str_reset(void) { memset(&strescseq, 0, sizeof(strescseq)); }

void t_put_tab(bool forward) {
    uint x = term.c.x;

    if (forward) {
        if (x == term.col) return;
        for (++x; x < term.col && !term.tabs[x]; ++x) /* nothing */
            ;
    } else {
        if (x == 0) return;
        for (--x; x > 0 && !term.tabs[x]; --x) /* nothing */
            ;
    }
    t_move_to(x, term.c.y);
}

void t_putc(char *c, int len) {
    uchar ascii = *c;
    bool control = ascii < '\x20' || ascii == 0177;

    if (iofd != -1) {
        if (x_write(iofd, c, len) < 0) {
            fprintf(stderr, "Error writting in %s:%s\n", opt_io, strerror(errno));
            close(iofd);
            iofd = -1;
        }
    }
    /*
     * STR sequences must be checked before of anything
     * because it can use some control codes as part of the sequence
     */
    if (term.esc & ESC_STR) {
        switch (ascii) {
            case '\033':
                term.esc = ESC_START | ESC_STR_END;
                break;
            case '\a': /* backwards compatibility to xterm */
                term.esc = 0;
                break;
            default:
                strescseq.buf[strescseq.len++] = ascii;
                if (strescseq.len + 1 >= STR_BUF_SIZ) {
                    term.esc = 0;
                }
        }
        return;
    }
    /*
     * Actions of control codes must be performed as soon they arrive
     * because they can be embedded inside a control sequence, and
     * they must not cause conflicts with sequences.
     */
    if (control) {
        switch (ascii) {
            case '\t': /* HT */
                t_put_tab(1);
                return;
            case '\b': /* BS */
                t_move_to(term.c.x - 1, term.c.y);
                return;
            case '\r': /* CR */
                t_move_to(0, term.c.y);
                return;
            case '\f': /* LF */
            case '\v': /* VT */
            case '\n': /* LF */
                /* go to first col if the mode is set */
                t_newline(IS_SET(MODE_CRLF));
                return;
            case '\a': /* BEL */
                return;
            case '\033': /* ESC */
                csi_reset();
                term.esc = ESC_START;
                return;
            case '\016': /* SO */
                term.c.attr.mode |= ATTR_GFX;
                return;
            case '\017': /* SI */
                term.c.attr.mode &= ~ATTR_GFX;
                return;
            case '\032': /* SUB */
            case '\030': /* CAN */
                csi_reset();
                return;
            case '\005': /* ENQ (IGNORED) */
            case '\000': /* NUL (IGNORED) */
            case '\021': /* XON (IGNORED) */
            case '\023': /* XOFF (IGNORED) */
            case 0177:   /* DEL (IGNORED) */
                return;
        }
    } else if (term.esc & ESC_START) {
        if (term.esc & ESC_CSI) {
            csiescseq.buf[csiescseq.len++] = ascii;
            if (BETWEEN(ascii, 0x40, 0x7E) || csiescseq.len >= ESC_BUF_SIZ) {
                term.esc = 0;
                csi_parse(), csi_handle();
            }
        } else if (term.esc & ESC_STR_END) {
            term.esc = 0;
        } else if (term.esc & ESC_ALTCHARSET) {
            switch (ascii) {
                case '0': /* Line drawing set */
                    term.c.attr.mode |= ATTR_GFX;
                    break;
                case 'B': /* USASCII */
                    term.c.attr.mode &= ~ATTR_GFX;
                    break;
                case 'A': /* UK (IGNORED) */
                case '<': /* multinational charset (IGNORED) */
                case '5': /* Finnish (IGNORED) */
                case 'C': /* Finnish (IGNORED) */
                case 'K': /* German (IGNORED) */
                    break;
                default:
                    fprintf(stderr, "esc unhandled charset: ESC ( %c\n", ascii);
            }
            term.esc = 0;
        } else if (term.esc & ESC_TEST) {
            if (ascii == '8') { /* DEC screen alignment test. */
                char E[UTF_SIZ] = "E";
                int x, y;

                for (x = 0; x < term.col; ++x) {
                    for (y = 0; y < term.row; ++y) t_set_char(E, &term.c.attr, x, y);
                }
            }
            term.esc = 0;
        } else {
            switch (ascii) {
                case '[':
                    term.esc |= ESC_CSI;
                    break;
                case '#':
                    term.esc |= ESC_TEST;
                    break;
                case 'P': /* DCS -- Device Control String */
                case '_': /* APC -- Application Program Command */
                case '^': /* PM -- Privacy Message */
                case ']': /* OSC -- Operating System Command */
                case 'k': /* old title set compatibility */
                    str_reset();
                    strescseq.type = ascii;
                    term.esc |= ESC_STR;
                    break;
                case '(': /* set primary charset G0 */
                    term.esc |= ESC_ALTCHARSET;
                    break;
                case ')': /* set secondary charset G1 (IGNORED) */
                case '*': /* set tertiary charset G2 (IGNORED) */
                case '+': /* set quaternary charset G3 (IGNORED) */
                    term.esc = 0;
                    break;
                case 'D': /* IND -- Linefeed */
                    if (term.c.y == term.bot) {
                        t_scroll_up(term.top, 1);
                    } else {
                        t_move_to(term.c.x, term.c.y + 1);
                    }
                    term.esc = 0;
                    break;
                case 'E':         /* NEL -- Next line */
                    t_newline(1); /* always go to first col */
                    term.esc = 0;
                    break;
                case 'H': /* HTS -- Horizontal tab stop */
                    term.tabs[term.c.x] = 1;
                    term.esc = 0;
                    break;
                case 'M': /* RI -- Reverse index */
                    if (term.c.y == term.top) {
                        t_scroll_down(term.top, 1);
                    } else {
                        t_move_to(term.c.x, term.c.y - 1);
                    }
                    term.esc = 0;
                    break;
                case 'Z': /* DECID -- Identify Terminal */
                    tty_write(VT102ID, sizeof(VT102ID) - 1);
                    term.esc = 0;
                    break;
                case 'c': /* RIS -- Reset to inital state */
                    t_reset();
                    term.esc = 0;
                    break;
                case '=': /* DECPAM -- Application keypad */
                    term.mode |= MODE_APPKEYPAD;
                    term.esc = 0;
                    break;
                case '>': /* DECPNM -- Normal keypad */
                    term.mode &= ~MODE_APPKEYPAD;
                    term.esc = 0;
                    break;
                case '7': /* DECSC -- Save Cursor */
                    t_cursor(CURSOR_SAVE);
                    term.esc = 0;
                    break;
                case '8': /* DECRC -- Restore Cursor */
                    t_cursor(CURSOR_LOAD);
                    term.esc = 0;
                    break;
                case '\\': /* ST -- Stop */
                    term.esc = 0;
                    break;
                default:
                    fprintf(stderr, "erresc: unknown sequence ESC 0x%02X '%c'\n", (uchar)ascii, isprint(ascii) ? ascii : '.');
                    term.esc = 0;
            }
        }
        /*
         * All characters which forms part of a sequence are not
         * printed
         */
        return;
    }
    /*
     * Display control codes only if we are in graphic mode
     */
    if (control && !(term.c.attr.mode & ATTR_GFX)) return;
    if (IS_SET(MODE_WRAP) && term.c.state & CURSOR_WRAPNEXT) t_newline(1); /* always go to first col */
    t_set_char(c, &term.c.attr, term.c.x, term.c.y);
    if (term.c.x + 1 < term.col)
        t_move_to(term.c.x + 1, term.c.y);
    else
        term.c.state |= CURSOR_WRAPNEXT;
}

int t_resize(int col, int row) {
    int i, x;
    int minrow = MIN(row, term.row);
    int mincol = MIN(col, term.col);
    int slide = term.c.y - row + 1;
    bool *bp;

    if (col < 1 || row < 1) return 0;

    /* free unneeded rows */
    i = 0;
    if (slide > 0) {
        /* slide screen to keep cursor where we expect it -
         * tscrollup would work here, but we can optimize to
         * memmove because we're freeing the earlier lines */
        for (/* i = 0 */; i < slide; i++) {
            free(term.line[i]);
            free(term.alt[i]);
        }
        memmove(term.line, term.line + slide, row * sizeof(Line));
        memmove(term.alt, term.alt + slide, row * sizeof(Line));
    }
    for (i += row; i < term.row; i++) {
        free(term.line[i]);
        free(term.alt[i]);
    }

    /* resize to new height */
    term.line = x_realloc(term.line, row * sizeof(Line));
    term.alt = x_realloc(term.alt, row * sizeof(Line));
    term.dirty = x_realloc(term.dirty, row * sizeof(*term.dirty));
    term.tabs = x_realloc(term.tabs, col * sizeof(*term.tabs));

    /* resize each row to new width, zero-pad if needed */
    for (i = 0; i < minrow; i++) {
        term.dirty[i] = 1;
        term.line[i] = x_realloc(term.line[i], col * sizeof(Glyph));
        term.alt[i] = x_realloc(term.alt[i], col * sizeof(Glyph));
        for (x = mincol; x < col; x++) {
            term.line[i][x].state = 0;
            term.alt[i][x].state = 0;
        }
    }

    /* allocate any new rows */
    for (/* i == minrow */; i < row; i++) {
        term.dirty[i] = 1;
        term.line[i] = x_calloc(col, sizeof(Glyph));
        term.alt[i] = x_calloc(col, sizeof(Glyph));
    }
    if (col > term.col) {
        bp = term.tabs + term.col;

        memset(bp, 0, sizeof(*term.tabs) * (col - term.col));
        while (--bp > term.tabs && !*bp) /* nothing */
            ;
        for (bp += tabspaces; bp < term.tabs + col; bp += tabspaces) *bp = 1;
    }
    /* update terminal size */
    term.col = col;
    term.row = row;
    /* make use of the LIMIT in t_move_to */
    t_move_to(term.c.x, term.c.y);
    /* reset scrolling region */
    t_set_scroll(0, row - 1);

    return (slide > 0);
}