#pragma once
#include <stdint.h>
#include "limine.h"

// Basic graphics primitives
void gfx_init(struct limine_framebuffer* fb);
void gfx_clear(uint32_t color);
void gfx_put_pixel(int32_t x, int32_t y, uint32_t color);
void gfx_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void gfx_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void gfx_draw_cursor(int32_t x, int32_t y);
void gfx_draw_char(int32_t x, int32_t y, char c, uint32_t color);
void gfx_clear_char(int32_t x, int32_t y, uint32_t bg_color);
void gfx_draw_string(int32_t x, int32_t y, const char *str, uint32_t color);
void gfx_draw_centered_text(const char* text, uint32_t color);
void gfx_scroll_up(int pixels, uint32_t fill_color);
void gfx_swap_buffers();
uint64_t gfx_get_width();
uint64_t gfx_get_height();

// Colors
#define COLOR_BLACK       0x000000
#define COLOR_WHITE       0xFFFFFF
#define COLOR_GRAY        0x808080
#define COLOR_DARK_GRAY   0x404040
#define COLOR_LIGHT_GRAY  0xC0C0C0
#define COLOR_BLUE        0x0000AA
#define COLOR_DARK_BLUE   0x000066
#define COLOR_CYAN        0x00AAAA
#define COLOR_GREEN       0x00AA00
#define COLOR_RED         0xAA0000
#define COLOR_DESKTOP     0x008080
