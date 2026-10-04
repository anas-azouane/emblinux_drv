/* 128x64 monochrome OLED over I2C (SSD1306, and SH1106 modulo a 2 px shift). */
#ifndef SSD1306_H
#define SSD1306_H

#include <stdint.h>
#include "stm32mp1xx_hal.h"

#define OLED_W     128
#define OLED_H     64
#define OLED_PAGES (OLED_H / 8)
#define OLED_COLS  (OLED_W / 8)          /* text columns, 8 px glyphs */

HAL_StatusTypeDef oled_init(uint8_t addr7);
HAL_StatusTypeDef oled_flush(uint8_t addr7);
HAL_StatusTypeDef oled_invert(uint8_t addr7, int on);

/* Pushes only the pages set in the mask, bit 0 = top page. A full frame is
 * 8 x 129 bytes, about 95 ms of bus time at 100 kHz, so anything that
 * animates wants to send just the pages it touched. */
HAL_StatusTypeDef oled_flush_pages(uint8_t addr7, uint8_t page_mask);

/* Compares the framebuffer against what the panel was last sent and pushes
 * only the pages that actually changed. Lets a game redraw the whole frame
 * every tick and still send two or three pages. */
HAL_StatusTypeDef oled_flush_dirty(uint8_t addr7);

/* Page mask sent by the last oled_flush_dirty(), for frame-cost accounting. */
uint8_t oled_last_mask(void);

void oled_pixel(uint16_t x, uint16_t y, int on);
void oled_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, int on);

/* Row-major bitmap, MSB leftmost, one byte per row, up to 8 px wide. */
void oled_sprite(int x, int y, const uint8_t *rows, int w, int h, int flip);
void oled_fill(uint8_t byte);
void oled_checkerboard(void);
void oled_frame(void);                             /* 1 px border */
void oled_text(uint8_t col, uint8_t page, const char *s, int invert);
void oled_bar(uint8_t page, uint8_t percent);      /* framed progress bar */

#endif /* SSD1306_H */
