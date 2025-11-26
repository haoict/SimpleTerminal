#include "keyboard.h"

#include <SDL2/SDL.h>
#include <time.h>

#include "font.h"
#include "vt100.h"

#define KEYBOARD_PADDING 16

#define NUM_ROWS 6
#define NUM_KEYS 18

static int row_length[NUM_ROWS] = {13, 17, 17, 15, 14, 10};

static SDL_Keycode keys[2][NUM_ROWS][NUM_KEYS] = {
    {{SDLK_ESCAPE, SDLK_F1, SDLK_F2, SDLK_F3, SDLK_F4, SDLK_F5, SDLK_F6, SDLK_F7, SDLK_F8, SDLK_F9, SDLK_F10, SDLK_F11, SDLK_F12},
     {SDLK_BACKQUOTE, SDLK_1, SDLK_2, SDLK_3, SDLK_4, SDLK_5, SDLK_6, SDLK_7, SDLK_8, SDLK_9, SDLK_0, SDLK_MINUS, SDLK_EQUALS, SDLK_BACKSPACE, SDLK_INSERT, SDLK_DELETE, SDLK_UP},
     {SDLK_TAB, SDLK_q, SDLK_w, SDLK_e, SDLK_r, SDLK_t, SDLK_y, SDLK_u, SDLK_i, SDLK_o, SDLK_p, SDLK_LEFTBRACKET, SDLK_RIGHTBRACKET, SDLK_BACKSLASH, SDLK_HOME, SDLK_END, SDLK_DOWN},
     {SDLK_CAPSLOCK, SDLK_a, SDLK_s, SDLK_d, SDLK_f, SDLK_g, SDLK_h, SDLK_j, SDLK_k, SDLK_l, SDLK_SEMICOLON, SDLK_QUOTE, SDLK_RETURN, SDLK_PAGEUP, SDLK_LEFT},
     {SDLK_LSHIFT, SDLK_z, SDLK_x, SDLK_c, SDLK_v, SDLK_b, SDLK_n, SDLK_m, SDLK_COMMA, SDLK_PERIOD, SDLK_SLASH, SDLK_RSHIFT, SDLK_PAGEDOWN, SDLK_RIGHT},
     {SDLK_LCTRL, SDLK_LGUI, SDLK_LALT, SDLK_SPACE, SDLK_RALT, SDLK_RGUI, SDLK_RCTRL, SDLK_PRINTSCREEN, KEY_OSKLOCATION, KEY_QUIT}},
    {{SDLK_ESCAPE, SDLK_F1, SDLK_F2, SDLK_F3, SDLK_F4, SDLK_F5, SDLK_F6, SDLK_F7, SDLK_F8, SDLK_F9, SDLK_F10, SDLK_F11, SDLK_F12},
     {'~', SDLK_EXCLAIM, SDLK_AT, SDLK_HASH, SDLK_DOLLAR, '%', SDLK_CARET, SDLK_AMPERSAND, SDLK_ASTERISK, SDLK_LEFTPAREN, SDLK_RIGHTPAREN, SDLK_UNDERSCORE, SDLK_PLUS, SDLK_BACKSPACE, SDLK_INSERT, SDLK_DELETE, SDLK_UP},
     {SDLK_TAB, SDLK_q, SDLK_w, SDLK_e, SDLK_r, SDLK_t, SDLK_y, SDLK_u, SDLK_i, SDLK_o, SDLK_p, '{', '}', '|', SDLK_HOME, SDLK_END, SDLK_DOWN},
     {SDLK_CAPSLOCK, SDLK_a, SDLK_s, SDLK_d, SDLK_f, SDLK_g, SDLK_h, SDLK_j, SDLK_k, SDLK_l, SDLK_COLON, SDLK_QUOTEDBL, SDLK_RETURN, SDLK_PAGEUP, SDLK_LEFT},
     {SDLK_LSHIFT, SDLK_z, SDLK_x, SDLK_c, SDLK_v, SDLK_b, SDLK_n, SDLK_m, SDLK_LESS, SDLK_GREATER, SDLK_QUESTION, SDLK_RSHIFT, SDLK_PAGEDOWN, SDLK_RIGHT},
     {SDLK_LCTRL, SDLK_LGUI, SDLK_LALT, SDLK_SPACE, SDLK_RALT, SDLK_RGUI, SDLK_RCTRL, SDLK_PRINTSCREEN, KEY_OSKLOCATION, KEY_QUIT}}};

static char *syms[2][NUM_ROWS][NUM_KEYS] = {{{"Esc", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", NULL},
                                             {"` ", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "Bsp", "Ins", "Del", " ^ ", NULL},
                                             {"Tab", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "[", "]", "\\", "Home", "End", " \xde ", NULL},
                                             {"Caps", "a", "s", "d", "f", "g", "h", "j", "k", "l", ";", "'", "Enter", "Pg Up", " < ", NULL},
                                             {"Shift", "z", "x", "c", "v", "b", "n", "m", ",", ".", "/", " Shift", "Pg Dn", " > ", NULL},
                                             {"Ctl", "", "Alt", "   Space   ", "Alt", "", "Ctl", "PrS", "Mov", "Exit", NULL}},
                                            {{"Esc", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", NULL},
                                             {"~ ", "!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "_", "+", "Bsp", "Ins", "Del", " ^ ", NULL},
                                             {"Tab", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "{", "}", "|", "Home", "End", " \xde ", NULL},
                                             {"Caps", "A", "S", "D", "F", "G", "H", "J", "K", "L", ":", "\"", "Enter", "Pg Up", " < ", NULL},
                                             {"Shift", "Z", "X", "C", "V", "B", "N", "M", "<", ">", "?", " Shift", "Pg Dn", " > ", NULL},
                                             {"Ctl", "", "Alt", "   Space   ", "Alt", "", "Ctl", "PrS", "Mov", "Exit", NULL}}};

static unsigned char toggled[NUM_ROWS][NUM_KEYS];

static int selected_i = 0, selected_j = 0;
static int visual_offset = 0;
static int shifted = 0;
static int location = 0;
static int mod_state = 0;

int active = 1;
int show_help = 1;

static int embedded_font_name = 1;  // 1 or 2
static int embeddedFontCharWidth;
static int embeddedFontCharHeight;
static int ttfCharWidth;
static int ttfCharHeight;

int useEmbeddedFontForKeyboard = 0;

void init_keyboard(int _embedded_font_name, int _useEmbeddedFontForKeyboard) {
    embedded_font_name = _embedded_font_name;
    useEmbeddedFontForKeyboard = _useEmbeddedFontForKeyboard;
    for (int j = 0; j < NUM_ROWS; j++)
        for (int i = 0; i < NUM_KEYS; i++) toggled[j][i] = 0;
    selected_i = selected_j = shifted = location = 0;
    mod_state = 0;

    embeddedFontCharWidth = get_embedded_font_char_width(embedded_font_name);
    embeddedFontCharHeight = get_embedded_font_char_height(embedded_font_name);
    ttfCharWidth = get_ttf_char_width();
    ttfCharHeight = get_ttf_char_height();

    if (is_ttf_loaded() && !useEmbeddedFontForKeyboard) {
        syms[0][2][16] = " v ";
        syms[1][2][16] = " v ";
    }
}

char *help1 =
    "How to use:\n"
    "  ARROWS:     select key from keyboard\n"
    "  A:          press key\n"
    "  B:          backspace\n"
    "  L1:         shift\n"
    "  R1:         toggle key (for shift/ctrl...)\n"
    "  Y:          change keyboard location\n"
    "  X:          show / hide keyboard\n"
    "  START:      enter\n"
    "  SELECT:     tab\n"
    "  L2:         left\n"
    "  R2:         right\n"
    "  MENU:       quit\n\n"
    "Cheatcheet (tutorial at www.shellscript.sh):\n"
    "  TAB key         complete path\n"
    "  UP/DOWN keys    navigate history\n"
    "  pwd             print current directory\n"
    "  ls              list files (-l for file size)\n"
    "  cd <d>          change directory (.. = go up)\n"
    "  cp <f> <d>      copy files (dest can be dir)\n"
    "  mv <f> <d>      move files (dest can be dir)\n"
    "  rm <f>          remove files (use -rf for dir)\n\n";

char *help2 =
    "How to use:\n"
    "  ARROWS:     select key from keyboard\n"
    "  A:          press key\n"
    "  B:          backspace\n"
    "  L1:         shift\n"
    "  R1:         toggle key (for shift/ctrl...)\n"
    "  Y:          change keyboard location\n"
    "  X:          show / hide keyboard\n"
    "  START:      enter\n"
    "  SELECT:     tab\n"
    "  L2:         left\n"
    "  R2:         right\n"
    "  MENU:       quit\n\n";

#define CREDIT "@haoict (c) 2025"

void draw_keyboard(SDL_Surface *surface) {
    unsigned short bg_color = SDL_MapRGB(surface->format, 64, 64, 64);
    unsigned short key_color = SDL_MapRGB(surface->format, 128, 128, 128);
    unsigned short text_color = SDL_MapRGB(surface->format, 0, 0, 0);
    unsigned short sel_color = SDL_MapRGB(surface->format, 128, 255, 128);
    unsigned short sel_toggled_color = SDL_MapRGB(surface->format, 255, 255, 128);
    unsigned short toggled_color = SDL_MapRGB(surface->format, 192, 192, 0);
    if (is_ttf_loaded()) {
        show_help = 0;  // disable when TTF is available to avoid text overlap
    }
    if (show_help) {
        SDL_FillRect(surface, NULL, text_color);
        if (is_ttf_loaded()) {
            // Use TTF rendering
            draw_string_ttf(surface, "Simple Terminal", 2, 10, (SDL_Color){255, 255, 128, 255}, (SDL_Color){0, 0, 0, 255});
            draw_string_ttf_with_linebreak(surface, embedded_font_name == 1 ? help1 : help2, 8, 30, (SDL_Color){128, 255, 128, 255}, (SDL_Color){0, 0, 0, 255});
        } else {
            draw_string(surface, "Simple Terminal", 2, 10, sel_toggled_color, embedded_font_name);
            draw_string(surface, embedded_font_name == 1 ? help1 : help2, 8, 30, sel_color, embedded_font_name);
        }
#ifdef VERSION
        char credit_str[128];
        snprintf(credit_str, sizeof(credit_str), "Version %s - %s", VERSION, CREDIT);
        if (is_ttf_loaded()) {
            // Use TTF rendering
            draw_string_ttf(surface, credit_str, 2, 400, (SDL_Color){255, 255, 128, 255}, (SDL_Color){0, 0, 0, 255});
        } else {
            draw_string(surface, credit_str, 2, 220, sel_toggled_color, embedded_font_name);
        }
#else
        if (is_ttf_loaded()) {
            // Use TTF rendering
            draw_string_ttf(surface, CREDIT, 2, 220, (SDL_Color){255, 255, 128, 255}, (SDL_Color){0, 0, 0, 255});
        } else {
            draw_string(surface, CREDIT, 2, 220, sel_toggled_color, embedded_font_name);
        }
#endif
        return;
    }

    if (!active) return;

    if (useEmbeddedFontForKeyboard || !is_ttf_loaded()) {
        int total_length = -1;
        for (int i = 0; i < NUM_KEYS && syms[0][0][i]; i++) {
            total_length += (1 + strlen(syms[0][0][i])) * embeddedFontCharWidth;
        }
        int center_x = (surface->w - total_length) / 2;
        int x = center_x, y = surface->h - embeddedFontCharHeight * (NUM_ROWS)-KEYBOARD_PADDING;
        if (location == 1) y = KEYBOARD_PADDING;

        SDL_Rect keyboardRect = {x - 4, y - 3, total_length + 3, NUM_ROWS * embeddedFontCharHeight + 3};
        SDL_FillRect(surface, &keyboardRect, bg_color);

        for (int j = 0; j < NUM_ROWS; j++) {
            x = center_x;
            for (int i = 0; i < row_length[j]; i++) {
                int length = strlen(syms[shifted][j][i]);
                SDL_Rect keyRect = {x - 2, y - 1, length * embeddedFontCharWidth + embeddedFontCharWidth - 2, embeddedFontCharHeight - 1};
                if (toggled[j][i]) {
                    if (selected_i == i && selected_j == j) {
                        SDL_FillRect(surface, &keyRect, sel_toggled_color);
                    } else {
                        SDL_FillRect(surface, &keyRect, toggled_color);
                    }
                } else if (selected_i == i && selected_j == j) {
                    SDL_FillRect(surface, &keyRect, sel_color);
                } else {
                    SDL_FillRect(surface, &keyRect, key_color);
                }
                draw_string(surface, syms[shifted][j][i], x, y, text_color, embedded_font_name);
                x += embeddedFontCharWidth * (length + 1);
            }
            y += embeddedFontCharHeight;
        }
    } else {
        int total_length = -1;
        for (int i = 0; i < NUM_KEYS && syms[0][0][i]; i++) {
            total_length += (1 + strlen(syms[0][0][i])) * ttfCharWidth;
        }
        int center_x = (surface->w - total_length) / 2;
        int x = center_x;
        int y = surface->h - ttfCharHeight * (NUM_ROWS)-KEYBOARD_PADDING;
        if (location == 1) y = KEYBOARD_PADDING;

        SDL_Rect keyboardRect = {x - 4, y - 3, total_length + 3, NUM_ROWS * ttfCharHeight + 3};
        SDL_FillRect(surface, &keyboardRect, bg_color);

        for (int j = 0; j < NUM_ROWS; j++) {
            x = center_x;
            for (int i = 0; i < row_length[j]; i++) {
                SDL_Color ttf_shaded_bg;
                int length = strlen(syms[shifted][j][i]);
                SDL_Rect keyRect = {x - 2, y - 1, length * ttfCharWidth + ttfCharWidth - 2, ttfCharHeight - 1};
                if (toggled[j][i]) {
                    if (selected_i == i && selected_j == j) {
                        ttf_shaded_bg = (SDL_Color){255, 255, 128, 255};
                        SDL_FillRect(surface, &keyRect, sel_toggled_color);
                    } else {
                        ttf_shaded_bg = (SDL_Color){192, 192, 0, 255};
                        SDL_FillRect(surface, &keyRect, toggled_color);
                    }
                } else if (selected_i == i && selected_j == j) {
                    ttf_shaded_bg = (SDL_Color){128, 255, 128, 255};
                    SDL_FillRect(surface, &keyRect, sel_color);
                } else {
                    ttf_shaded_bg = (SDL_Color){128, 128, 128, 255};
                    SDL_FillRect(surface, &keyRect, key_color);
                }
                draw_string_ttf(surface, syms[shifted][j][i], x, y - 2, (SDL_Color){0, 0, 0, 255}, ttf_shaded_bg);
                x += ttfCharWidth * (length + 1);
            }
            y += ttfCharHeight;
        }
    }
}

enum { STATE_TYPED, STATE_UP, STATE_DOWN };

void update_modstate(int key, int state) {
    // SDLMod mod_state = SDL_GetModState();
    if (state == STATE_DOWN) {
        if (key == SDLK_LSHIFT)
            mod_state |= KMOD_LSHIFT;
        else if (key == SDLK_RSHIFT)
            mod_state |= KMOD_RSHIFT;
        else if (key == SDLK_LCTRL)
            mod_state |= KMOD_LCTRL;
        else if (key == SDLK_RCTRL)
            mod_state |= KMOD_RCTRL;
        else if (key == SDLK_LALT)
            mod_state |= KMOD_LALT;
        else if (key == SDLK_RALT)
            mod_state |= KMOD_RALT;
        else if (key == SDLK_LGUI)
            mod_state |= KMOD_LGUI;
        else if (key == SDLK_RGUI)
            mod_state |= KMOD_RGUI;
        else if (key == SDLK_NUMLOCKCLEAR)
            mod_state |= KMOD_NUM;
        else if (key == SDLK_CAPSLOCK)
            mod_state |= KMOD_CAPS;
        else if (key == SDLK_MODE)
            mod_state |= KMOD_MODE;
    } else if (state == STATE_UP) {
        if (key == SDLK_LSHIFT)
            mod_state &= ~KMOD_LSHIFT;
        else if (key == SDLK_RSHIFT)
            mod_state &= ~KMOD_RSHIFT;
        else if (key == SDLK_LCTRL)
            mod_state &= ~KMOD_LCTRL;
        else if (key == SDLK_RCTRL)
            mod_state &= ~KMOD_RCTRL;
        else if (key == SDLK_LALT)
            mod_state &= ~KMOD_LALT;
        else if (key == SDLK_RALT)
            mod_state &= ~KMOD_RALT;
        else if (key == SDLK_LGUI)
            mod_state &= ~KMOD_LGUI;
        else if (key == SDLK_RGUI)
            mod_state &= ~KMOD_RGUI;
        else if (key == SDLK_NUMLOCKCLEAR)
            mod_state &= ~KMOD_NUM;
        else if (key == SDLK_CAPSLOCK)
            mod_state &= ~KMOD_CAPS;
        else if (key == SDLK_MODE)
            mod_state &= ~KMOD_MODE;
    }
    SDL_SetModState(mod_state);
}

void simulate_key(int key, int state) {
    update_modstate(key, state);
    SDL_Event event = {.key = {.type = SDL_KEYDOWN, .state = SDL_PRESSED, .keysym = {.scancode = 0, .sym = key, .mod = KMOD_SYNTHETIC}}};
    if (state == STATE_TYPED) {
        SDL_PushEvent(&event);
        event.key.type = SDL_KEYUP;
        event.key.state = SDL_RELEASED;
    } else if (state == STATE_UP) {
        event.key.type = SDL_KEYUP;
        event.key.state = SDL_RELEASED;
    }
    SDL_PushEvent(&event);
    // printf("%d\n", key);
}

int compute_visual_offset(int col, int row) {
    int sum = 0;
    for (int i = 0; i < col; i++) sum += 1 + strlen(syms[0][row][i]);
    sum += (1 + strlen(syms[0][row][col])) / 2;
    return sum;
}

int compute_new_col(int visual_offset, int old_row, int new_row) {
    // For the short last row (the row has the space key), we manually adjust the mapping to make navigation feel more natural
    if (new_row == 5) {
        if (old_row == 0) {
            if (strncmp(syms[0][0][selected_i], "F5", 2) == 0 || strncmp(syms[0][0][selected_i], "F6", 2) == 0) {
                return 3;  // space
            }
        } else if (old_row == 4) {
            if (strncmp(syms[0][4][selected_i], "n", 1) == 0 || strncmp(syms[0][4][selected_i], "m", 1) == 0 || strncmp(syms[0][4][selected_i], ",", 1) == 0) {
                return 3;  // space
            }
        }
    }

    // Original logic for other cases
    int new_sum = 0;
    int new_col = 0;
    while (new_col < row_length[new_row] - 1 && new_sum + (1 + strlen(syms[0][new_row][new_col])) / 2 < visual_offset) {
        new_sum += 1 + strlen(syms[0][new_row][new_col]);
        new_col++;
    }
    return new_col;
}

#if defined(RGB30)
static int rgb30_first_jbutton10_pressed = 0;  // TODO: temp fix for RGB30, for unknown reason, Joystick jbutton 10 (KEY_QUIT) always triggers at startup, so we must ignore it
#endif
int handle_keyboard_event(SDL_Event *event) {
    if (event->key.type == SDL_KEYDOWN && event->key.keysym.sym == KEY_QUIT) {
#if defined(RGB30)
        if (!rgb30_first_jbutton10_pressed) {
            rgb30_first_jbutton10_pressed = 1;
            return 1;
        }
#endif
        printf("Exit event requested by Exit button\n");
        SDL_Event quit_event;
        quit_event.type = SDL_QUIT;
        SDL_PushEvent(&quit_event);
        return 1;
    }

    if (event->key.type == SDL_KEYDOWN && !(event->key.keysym.mod & KMOD_SYNTHETIC) && event->key.keysym.sym == KEY_OSKACTIVATE) {
        if (show_help) {
            show_help = 0;
            return 1;
        }
        active = !active;
        return 1;
    }

    if ((event->key.type == SDL_KEYUP || event->key.type == SDL_KEYDOWN) && event->key.keysym.mod & KMOD_SYNTHETIC) {
        if (event->key.type == SDL_KEYDOWN) {
            switch (event->key.keysym.sym) {
                case SDLK_PRINTSCREEN:
                    printf("Screenshot event requested\n");
                    SDL_Event screenshotEvent;
                    screenshotEvent.type = SDL_USEREVENT;
                    screenshotEvent.user.code = 1;
                    SDL_PushEvent(&screenshotEvent);
                    return 1;
                case KEY_OSKLOCATION:
                    // printf("Change keyboard location button pressed, sym: %d\n", event->key.keysym.sym);
                    location = !location;
                    return 1;
                default:
                    break;
            }
        }
        // printf("handle_keyboard_event: type: %s, sym: %d (%s), scancode:%d\n", event->key.type == SDL_KEYDOWN ? "keydown" : "keyup", event->key.keysym.sym, SDL_GetKeyName(event->key.keysym.sym), event->key.keysym.scancode);
        return 0;
    }

    if (!active) {
#if defined(BR2) && !defined(RPI)
        // handle joystick button directly when OSK is inactive
        if (event->key.type == SDL_KEYDOWN && event->key.state == SDL_PRESSED) {
            if (event->key.keysym.sym == JOYBUTTON_UP) {
                simulate_key(SDLK_UP, STATE_TYPED);
            } else if (event->key.keysym.sym == JOYBUTTON_DOWN) {
                simulate_key(SDLK_DOWN, STATE_TYPED);
            } else if (event->key.keysym.sym == JOYBUTTON_LEFT) {
                simulate_key(SDLK_LEFT, STATE_TYPED);
            } else if (event->key.keysym.sym == JOYBUTTON_RIGHT) {
                simulate_key(SDLK_RIGHT, STATE_TYPED);
            } else if (event->key.keysym.sym == JOYBUTTON_START || event->key.keysym.sym == JOYBUTTON_A) {
                simulate_key(SDLK_RETURN, STATE_TYPED);
            } else if (event->key.keysym.sym == JOYBUTTON_SELECT) {
                simulate_key(SDLK_TAB, STATE_TYPED);
            } else if (event->key.keysym.sym == JOYBUTTON_B) {
                ttywrite("\003", 1);  // Ctrl+C
            }
            return 1;
        }
#endif
        return 0;
    }

    if (event->key.type == SDL_KEYDOWN && event->key.state == SDL_PRESSED) {
        // printf("handle_keyboard_event: type: %s, sym: %d (%s), scancode:%d\n", event->key.type == SDL_KEYDOWN ? "keydown" : "keyup", event->key.keysym.sym, SDL_GetKeyName(event->key.keysym.sym), event->key.keysym.scancode);
        if (show_help) {
            // do nothing
        } else if (event->key.keysym.sym == KEY_SHIFT) {
            shifted = 1;
            toggled[4][0] = 1;
            update_modstate(SDLK_LSHIFT, STATE_DOWN);
        } else if (event->key.keysym.sym == KEY_OSKLOCATION) {
            location = !location;
        } else if (event->key.keysym.sym == KEY_BACKSPACE) {
            simulate_key(SDLK_BACKSPACE, STATE_TYPED);
        } else if (event->key.keysym.sym == KEY_ARROW_UP || event->key.keysym.sym == SDLK_VOLUMEUP) {
            simulate_key(SDLK_UP, STATE_TYPED);
        } else if (event->key.keysym.sym == KEY_ARROW_DOWN || event->key.keysym.sym == SDLK_VOLUMEDOWN) {
            simulate_key(SDLK_DOWN, STATE_TYPED);
        } else if (event->key.keysym.sym == KEY_ARROW_LEFT) {
            simulate_key(SDLK_LEFT, STATE_TYPED);
        } else if (event->key.keysym.sym == KEY_ARROW_RIGHT) {
            simulate_key(SDLK_RIGHT, STATE_TYPED);
        } else if (event->key.keysym.sym == KEY_TAB) {
            simulate_key(SDLK_TAB, STATE_TYPED);
        } else if (event->key.keysym.sym == KEY_RETURN) {
            simulate_key(SDLK_RETURN, STATE_TYPED);
        } else if (event->key.keysym.sym == KEY_OSKTOGGLE) {
            toggled[selected_j][selected_i] = 1 - toggled[selected_j][selected_i];
            if (toggled[selected_j][selected_i])
                simulate_key(keys[shifted][selected_j][selected_i], STATE_DOWN);
            else
                simulate_key(keys[shifted][selected_j][selected_i], STATE_UP);
            if (selected_j == 4 && (selected_i == 0 || selected_i == 11)) shifted = toggled[selected_j][selected_i];
        } else if (event->key.keysym.sym == KEY_ENTER) {
            int key = keys[shifted][selected_j][selected_i];
            if (mod_state & KMOD_CTRL) {
                if (key >= 64 && key < 64 + 32)
                    simulate_key(key - 64, STATE_DOWN);
                else if (key >= 97 && key < 97 + 31)
                    simulate_key(key - 96, STATE_DOWN);
            } else if (mod_state & KMOD_SHIFT && key >= SDLK_a && key <= SDLK_z) {
                simulate_key(key - SDLK_a + 'A', STATE_TYPED);
            } else {
                simulate_key(key, STATE_TYPED);
            }
        } else {
            // fprintf(stderr,"unrecognized key: %d\n",event->key.keysym.sym);
            // return 0;
        }
    } else if (event->key.type == SDL_KEYUP || event->key.state == SDL_RELEASED) {
        if (show_help) {
            show_help = 0;
        } else if (event->key.keysym.sym == KEY_SHIFT) {
            shifted = 0;
            toggled[4][0] = 0;
            update_modstate(SDLK_LSHIFT, STATE_UP);
        }
    }
    return 1;
}

int handle_narrow_keys_held(int sym) {
    if (!active || show_help) {
        return 0;
    }
    if (sym == KEY_LEFT) {
        if (selected_i > 0)
            selected_i--;
        else
            selected_i = row_length[selected_j] - 1;
        visual_offset = compute_visual_offset(selected_i, selected_j);
    } else if (sym == KEY_RIGHT) {
        if (selected_i < row_length[selected_j] - 1)
            selected_i++;
        else
            selected_i = 0;
        visual_offset = compute_visual_offset(selected_i, selected_j);
    } else if (sym == KEY_UP) {
        if (selected_j > 0) {
            selected_i = compute_new_col(visual_offset, selected_j, selected_j - 1);
            selected_j--;
        } else {
            selected_i = compute_new_col(visual_offset, selected_j, NUM_ROWS - 1);
            selected_j = NUM_ROWS - 1;
        }
        if (selected_i >= row_length[selected_j]) {
            selected_i = row_length[selected_j] - 1;
        }
    } else if (sym == KEY_DOWN) {
        if (selected_j < NUM_ROWS - 1) {
            selected_i = compute_new_col(visual_offset, selected_j, selected_j + 1);
            selected_j++;
        } else {
            selected_i = compute_new_col(visual_offset, selected_j, 0);
            selected_j = 0;
        }
        if (selected_i < 0) {
            selected_i = 0;
        }
    }
    return 1;
}
