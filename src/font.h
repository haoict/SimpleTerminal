#ifndef __FONT_H__
#define __FONT_H__

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

/* Bitmap font functions */
void draw_char(SDL_Surface *surface, unsigned char symbol, int x, int y, unsigned short color);
void draw_string(SDL_Surface *surface, const char *text, int x, int y, unsigned short color);
int get_embeded_font_char_width(void);
int get_embeded_font_char_height(void);

/* TTF font functions */
int init_ttf_font(const char *font_path, int font_size, int font_shaded);
void cleanup_ttf_font(void);
void draw_string_ttf(SDL_Surface *surface, const char *text, int x, int y, SDL_Color fg, SDL_Color bg);
void draw_string_ttf_with_linebreak(SDL_Surface *surface, const char *text, int x, int y, SDL_Color fg, SDL_Color bg);
int get_ttf_char_width(void);
int get_ttf_char_height(void);
int is_ttf_loaded(void);

#endif
