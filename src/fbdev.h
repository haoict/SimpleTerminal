#ifndef FBDEV_H
#define FBDEV_H

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

/* Basic SDL-like definitions for framebuffer mode */

typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef int32_t Sint32;

typedef struct SDL_Rect {
    int x, y, w, h;
} SDL_Rect;

typedef struct SDL_Color {
    Uint8 r, g, b, a;
} SDL_Color;

typedef struct SDL_PixelFormat {
    Uint32 format;
    Uint32 Rmask;
    Uint32 Gmask;
    Uint32 Bmask;
    Uint32 Amask;
    int BytesPerPixel;
} SDL_PixelFormat;

typedef struct SDL_Surface {
    Uint32 flags;
    SDL_PixelFormat *format;
    int w;
    int h;
    int pitch;
    void *pixels;
    void *userdata;
} SDL_Surface;

typedef void SDL_Window;
typedef void SDL_Renderer;
typedef void SDL_Texture;

typedef struct SDL_DisplayMode {
    int w;
    int h;
    int refresh_rate;
} SDL_DisplayMode;

typedef int SDL_Keycode;
typedef Uint16 SDL_Keymod;
enum {
    SDLK_UNKNOWN = 0,
    SDLK_RETURN = '\r',
    SDLK_ESCAPE = '\x1B',
    SDLK_BACKSPACE = '\b',
    SDLK_TAB = '\t',
    SDLK_SPACE = ' ',
    SDLK_EXCLAIM = '!',
    SDLK_QUOTEDBL = '"',
    SDLK_HASH = '#',
    SDLK_PERCENT = '%',
    SDLK_DOLLAR = '$',
    SDLK_AMPERSAND = '&',
    SDLK_QUOTE = '\'',
    SDLK_LEFTPAREN = '(',
    SDLK_RIGHTPAREN = ')',
    SDLK_ASTERISK = '*',
    SDLK_PLUS = '+',
    SDLK_COMMA = ',',
    SDLK_MINUS = '-',
    SDLK_PERIOD = '.',
    SDLK_SLASH = '/',
    SDLK_0 = '0',
    SDLK_1 = '1',
    SDLK_2 = '2',
    SDLK_3 = '3',
    SDLK_4 = '4',
    SDLK_5 = '5',
    SDLK_6 = '6',
    SDLK_7 = '7',
    SDLK_8 = '8',
    SDLK_9 = '9',
    SDLK_COLON = ':',
    SDLK_SEMICOLON = ';',
    SDLK_LESS = '<',
    SDLK_EQUALS = '=',
    SDLK_GREATER = '>',
    SDLK_QUESTION = '?',
    SDLK_AT = '@',
    SDLK_LEFTBRACKET = '[',
    SDLK_BACKSLASH = '\\',
    SDLK_RIGHTBRACKET = ']',
    SDLK_CARET = '^',
    SDLK_UNDERSCORE = '_',
    SDLK_BACKQUOTE = '`',
    SDLK_a = 'a',
    SDLK_b = 'b',
    SDLK_c = 'c',
    SDLK_d = 'd',
    SDLK_e = 'e',
    SDLK_f = 'f',
    SDLK_g = 'g',
    SDLK_h = 'h',
    SDLK_i = 'i',
    SDLK_j = 'j',
    SDLK_k = 'k',
    SDLK_l = 'l',
    SDLK_m = 'm',
    SDLK_n = 'n',
    SDLK_o = 'o',
    SDLK_p = 'p',
    SDLK_q = 'q',
    SDLK_r = 'r',
    SDLK_s = 's',
    SDLK_t = 't',
    SDLK_u = 'u',
    SDLK_v = 'v',
    SDLK_w = 'w',
    SDLK_x = 'x',
    SDLK_y = 'y',
    SDLK_z = 'z',
    SDLK_CAPSLOCK = 0x40000039,
    SDLK_F1 = 0x4000003a,
    SDLK_F2 = 0x4000003b,
    SDLK_F3 = 0x4000003c,
    SDLK_F4 = 0x4000003d,
    SDLK_F5 = 0x4000003e,
    SDLK_F6 = 0x4000003f,
    SDLK_F7 = 0x40000040,
    SDLK_F8 = 0x40000041,
    SDLK_F9 = 0x40000042,
    SDLK_F10 = 0x40000043,
    SDLK_F11 = 0x40000044,
    SDLK_F12 = 0x40000045,
    SDLK_PRINTSCREEN = 0x40000046,
    SDLK_INSERT = 0x40000049,
    SDLK_HOME = 0x4000004a,
    SDLK_PAGEUP = 0x4000004b,
    SDLK_DELETE = '\x7F',
    SDLK_END = 0x4000004d,
    SDLK_PAGEDOWN = 0x4000004e,
    SDLK_RIGHT = 0x4000004f,
    SDLK_LEFT = 0x40000050,
    SDLK_DOWN = 0x40000051,
    SDLK_UP = 0x40000052,
    SDLK_NUMLOCKCLEAR = 0x40000053,
    SDLK_LCTRL = 0x400000e0,
    SDLK_LSHIFT = 0x400000e1,
    SDLK_LALT = 0x400000e2,
    SDLK_LGUI = 0x400000e3,
    SDLK_RCTRL = 0x400000e4,
    SDLK_RSHIFT = 0x400000e5,
    SDLK_RALT = 0x400000e6,
    SDLK_RGUI = 0x400000e7,
    SDLK_VOLUMEUP = 0x40000080,
    SDLK_VOLUMEDOWN = 0x40000081,
    SDLK_MODE = 0x4000008f,
};
#define SDL_BUTTON_LEFT 1
#define SDL_WINDOWEVENT 0x1003
#define SDL_WINDOWEVENT_RESIZED 0x0005
#define SDL_JOYBUTTONDOWN 0x600
#define SDL_JOYBUTTONUP 0x601
#define SDL_QUIT 0x1001
#define SDL_KEYDOWN 0x300
#define SDL_KEYUP 0x301
#define SDL_TEXTINPUT 0x302
#define SDL_USEREVENT 0x8000
#define SDL_LASTEVENT 0x10000
#define SDL_INIT_EVERYTHING 0xFFFF
#define SDL_TEXTUREACCESS_STREAMING 1
#define SDL_PIXELFORMAT_RGB565 0x0002
#define SDL_PRESSED 1
#define SDL_RELEASED 0

#define KMOD_NONE 0
#define KMOD_LSHIFT 0x0001
#define KMOD_RSHIFT 0x0002
#define KMOD_SHIFT (KMOD_LSHIFT | KMOD_RSHIFT)
#define KMOD_LCTRL 0x0040
#define KMOD_RCTRL 0x0080
#define KMOD_CTRL (KMOD_LCTRL | KMOD_RCTRL)
#define KMOD_LALT 0x0100
#define KMOD_RALT 0x0200
#define KMOD_ALT (KMOD_LALT | KMOD_RALT)
#define KMOD_LGUI 0x0400
#define KMOD_RGUI 0x0800
#define KMOD_GUI (KMOD_LGUI | KMOD_RGUI)
#define KMOD_NUM 0x2000
#define KMOD_CAPS 0x1000
#define KMOD_MODE 0x4000

typedef struct SDL_Keysym {
    SDL_Keycode scancode;
    SDL_Keycode sym;
    SDL_Keymod mod;
} SDL_Keysym;

typedef struct SDL_KeyboardEvent {
    Uint32 type;
    Uint8 state;
    SDL_Keysym keysym;
} SDL_KeyboardEvent;

typedef struct SDL_TextInputEvent {
    Uint32 type;
    char text[32];
} SDL_TextInputEvent;

typedef struct SDL_WindowEvent {
    Uint32 type;
    Uint8 event;
    Uint8 padding1;
    Uint8 padding2;
    Uint8 padding3;
    int data1;
    int data2;
} SDL_WindowEvent;

typedef struct SDL_JoyButtonEvent {
    Uint32 type;
    Uint8 state;
    Uint8 button;
    Uint8 padding1;
    Uint8 padding2;
} SDL_JoyButtonEvent;

typedef struct SDL_UserEvent {
    Uint32 type;
    int code;
    void *data1;
    void *data2;
} SDL_UserEvent;

typedef struct SDL_Event {
    Uint32 type;
    union {
        SDL_KeyboardEvent key;
        SDL_TextInputEvent text;
        SDL_WindowEvent window;
        SDL_JoyButtonEvent jbutton;
        SDL_UserEvent user;
    };
} SDL_Event;

typedef pthread_t SDL_Thread;

typedef struct SDL_Joystick SDL_Joystick;

typedef Uint32 (*SDL_TimerCallback)(Uint32 interval, void *param);

int SDL_Init(Uint32 flags);
void SDL_Quit(void);
int SDL_WasInit(Uint32 flags);
void SDL_ShowCursor(int state);
void SDL_StartTextInput(void);
SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags);
SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags);
SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format, int access, int w, int h);
void SDL_DestroyTexture(SDL_Texture *texture);
void SDL_DestroyRenderer(SDL_Renderer *renderer);
void SDL_DestroyWindow(SDL_Window *window);
int fbdev_init(void);
void fbdev_present(SDL_Surface *surface);
SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth, Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask);
void SDL_FreeSurface(SDL_Surface *surface);
Uint32 SDL_MapRGB(SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b);
int SDL_FillRect(SDL_Surface *surface, const SDL_Rect *rect, Uint32 color);
int SDL_LockSurface(SDL_Surface *surface);
void SDL_UnlockSurface(SDL_Surface *surface);
int SDL_SaveBMP(SDL_Surface *surface, const char *file);
SDL_Thread *SDL_CreateThread(int (*fn)(void *), const char *name, void *data);
int SDL_WaitThread(SDL_Thread *thread, int *status);
Uint32 SDL_GetTicks(void);
void SDL_Delay(Uint32 ms);
Uint8 *SDL_GetError(void);
int SDL_PollEvent(SDL_Event *event);
int SDL_PushEvent(SDL_Event *event);
int SDL_AddTimer(Uint32 interval, SDL_TimerCallback callback, void *param);
const char *SDL_GetKeyName(SDL_Keycode key);
void SDL_SetModState(SDL_Keymod mod);
SDL_Joystick *SDL_JoystickOpen(int device_index);
void SDL_JoystickClose(SDL_Joystick *joystick);

#endif // FBDEV_H
