/* HD44780 character LCD behind a PCF8574 I2C backpack.
 * Backpack wiring, as on every common module: P0=RS P1=RW P2=E P3=backlight,
 * P4..P7 = D4..D7. */
#ifndef LCD_I2C_H
#define LCD_I2C_H

#include <stdint.h>
#include "stm32mp1xx_hal.h"

HAL_StatusTypeDef lcd_init(uint8_t addr7);
HAL_StatusTypeDef lcd_clear(uint8_t addr7);
HAL_StatusTypeDef lcd_puts(uint8_t addr7, uint8_t row, uint8_t col, const char *s);

#endif /* LCD_I2C_H */
