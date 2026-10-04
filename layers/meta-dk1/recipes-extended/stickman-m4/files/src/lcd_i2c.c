#include "lcd_i2c.h"
#include "i2c5.h"

#define LCD_RS 0x01
#define LCD_EN 0x04
#define LCD_BL 0x08                      /* backlight, kept on throughout */

static HAL_StatusTypeDef expander(uint8_t addr7, uint8_t v)
{
    return i2c5_write(addr7, &v, 1);
}

/* The HD44780 latches on the falling edge of E, so each nibble is clocked out
 * as write / E high / E low. */
static HAL_StatusTypeDef nibble(uint8_t addr7, uint8_t n, uint8_t rs)
{
    uint8_t v = (uint8_t)((n << 4) | rs | LCD_BL);
    HAL_StatusTypeDef st;

    if ((st = expander(addr7, v)) != HAL_OK) return st;
    if ((st = expander(addr7, (uint8_t)(v | LCD_EN))) != HAL_OK) return st;
    if ((st = expander(addr7, v)) != HAL_OK) return st;
    return HAL_OK;
}

static HAL_StatusTypeDef send(uint8_t addr7, uint8_t byte, uint8_t rs)
{
    HAL_StatusTypeDef st = nibble(addr7, (uint8_t)(byte >> 4), rs);
    if (st != HAL_OK)
        return st;
    return nibble(addr7, (uint8_t)(byte & 0x0F), rs);
}

HAL_StatusTypeDef lcd_init(uint8_t addr7)
{
    HAL_StatusTypeDef st;

    HAL_Delay(50);                       /* power-on time */

    /* Three 0x3 nibbles drag the controller into a known 8-bit state, then
     * 0x2 switches it to the 4-bit interface. */
    for (int i = 0; i < 3; i++) {
        if ((st = nibble(addr7, 0x3, 0)) != HAL_OK) return st;
        HAL_Delay(5);
    }
    if ((st = nibble(addr7, 0x2, 0)) != HAL_OK) return st;
    HAL_Delay(5);

    if ((st = send(addr7, 0x28, 0)) != HAL_OK) return st;   /* 4-bit, 2 lines, 5x8 */
    if ((st = send(addr7, 0x0C, 0)) != HAL_OK) return st;   /* display on, no cursor */
    if ((st = send(addr7, 0x06, 0)) != HAL_OK) return st;   /* increment, no shift */
    return lcd_clear(addr7);
}

HAL_StatusTypeDef lcd_clear(uint8_t addr7)
{
    HAL_StatusTypeDef st = send(addr7, 0x01, 0);
    HAL_Delay(2);                        /* clear takes ~1.5 ms */
    return st;
}

HAL_StatusTypeDef lcd_puts(uint8_t addr7, uint8_t row, uint8_t col, const char *s)
{
    static const uint8_t row_base[4] = { 0x00, 0x40, 0x14, 0x54 };
    HAL_StatusTypeDef st;

    if (row > 3)
        return HAL_ERROR;
    if ((st = send(addr7, (uint8_t)(0x80 | (row_base[row] + col)), 0)) != HAL_OK)
        return st;
    while (*s) {
        if ((st = send(addr7, (uint8_t)*s++, LCD_RS)) != HAL_OK)
            return st;
    }
    return HAL_OK;
}
