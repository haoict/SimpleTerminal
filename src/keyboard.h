#ifndef __KEYBOARD_H__
#define __KEYBOARD_H__

#include <SDL2/SDL.h>

#define KMOD_SYNTHETIC (1 << 14)

#if defined(BR2) && !defined(RPI)  // Buildroot handhelds with SDL2 (not raspberry pi)

// Physical button layout

#if defined(R36S)
// R36S / dArkOS (GO-Super Gamepad) confirmed via jstest/evtest:
// D-pad: 8=Up, 9=Down, 10=Left, 11=Right
// Select=12, Start=13, L3=14, R3=15, FN(MENU)=16
#define JOYBUTTON_UP -8
#define JOYBUTTON_DOWN -9
#define JOYBUTTON_LEFT -10
#define JOYBUTTON_RIGHT -11
#define JOYBUTTON_SELECT -12
#define JOYBUTTON_START -13
#define JOYBUTTON_L3 -14
#define JOYBUTTON_R3 -15
#define JOYBUTTON_MENU -16

#else
// Default BR2 handheld layout (rgb30, h700, etc.)
#define JOYBUTTON_UP -13
#define JOYBUTTON_DOWN -14
#define JOYBUTTON_LEFT -15
#define JOYBUTTON_RIGHT -16
#define JOYBUTTON_SELECT -8
#define JOYBUTTON_START -9
#define JOYBUTTON_L3 -11
#define JOYBUTTON_R3 -12
#define JOYBUTTON_MENU -10
#endif

// Shared buttons
#define JOYBUTTON_A -1
#define JOYBUTTON_B -0
#define JOYBUTTON_X -2
#define JOYBUTTON_Y -3
#define JOYBUTTON_L1 -4
#define JOYBUTTON_R1 -5
#define JOYBUTTON_L2 -6
#define JOYBUTTON_R2 -7

// Logical key bindings

#define KEY_UP JOYBUTTON_UP
#define KEY_DOWN JOYBUTTON_DOWN
#define KEY_LEFT JOYBUTTON_LEFT
#define KEY_RIGHT JOYBUTTON_RIGHT
#define KEY_ENTER JOYBUTTON_A
#define KEY_BACKSPACE JOYBUTTON_B
#define KEY_SHIFT JOYBUTTON_L1
#define KEY_OSKACTIVATE JOYBUTTON_X
#define KEY_OSKLOCATION JOYBUTTON_Y
#define KEY_OSKTOGGLE JOYBUTTON_R1
#define KEY_SCROLLUP JOYBUTTON_L2
#define KEY_SCROLLDOWN JOYBUTTON_R2
#define KEY_QUIT JOYBUTTON_MENU
#define KEY_TAB JOYBUTTON_SELECT
#define KEY_RETURN JOYBUTTON_START
#define KEY_ARROW_LEFT JOYBUTTON_L2
#define KEY_ARROW_RIGHT JOYBUTTON_R2
#define KEY_ARROW_UP JOYBUTTON_L3
#define KEY_ARROW_DOWN JOYBUTTON_R3

#else
// generic Linux PC
#define KEY_UP SDLK_UP
#define KEY_DOWN SDLK_DOWN
#define KEY_LEFT SDLK_LEFT
#define KEY_RIGHT SDLK_RIGHT
#define KEY_ENTER SDLK_F10
#define KEY_BACKSPACE SDLK_BACKSPACE
#define KEY_SHIFT SDLK_LSHIFT
#define KEY_OSKACTIVATE SDLK_F12
#define KEY_OSKLOCATION SDLK_F11
#define KEY_OSKTOGGLE SDLK_F9
#define KEY_SCROLLUP SDLK_F8
#define KEY_SCROLLDOWN SDLK_F7
#define KEY_QUIT SDLK_UNKNOWN  // not used
#define KEY_TAB SDLK_TAB
#define KEY_RETURN SDLK_RETURN
#define KEY_ARROW_LEFT SDLK_UNKNOWN   // not used
#define KEY_ARROW_RIGHT SDLK_UNKNOWN  // not used
#define KEY_ARROW_UP SDLK_UNKNOWN     // not used
#define KEY_ARROW_DOWN SDLK_UNKNOWN   // not used

#endif

void init_keyboard();
void draw_keyboard(SDL_Surface *surface);
int handle_keyboard_event(SDL_Event *event);
int handle_narrow_keys_held(int sym);
extern int active;
extern int show_help;

#endif
