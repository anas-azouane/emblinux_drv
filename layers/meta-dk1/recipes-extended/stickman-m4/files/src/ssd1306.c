#include "ssd1306.h"
#include "i2c5.h"
#include "font8x8.h"

/* One byte per 8-pixel column: fb[page][x], bit 0 = topmost pixel. */
static uint8_t fb[OLED_PAGES][OLED_W];

/* What the panel currently holds, so oled_flush_dirty() can tell what moved. */
static uint8_t shadow[OLED_PAGES][OLED_W];
static int shadow_valid;
static uint8_t last_mask;

static HAL_StatusTypeDef cmd(uint8_t addr7, const uint8_t *c, uint16_t n)
{
    uint8_t buf[8];

    buf[0] = 0x00;                       /* Co=0, D/C#=0: command stream */
    for (uint16_t i = 0; i < n && i < sizeof(buf) - 1; i++)
        buf[i + 1] = c[i];
    return i2c5_write(addr7, buf, (uint16_t)(n + 1));
}

static HAL_StatusTypeDef cmd1(uint8_t addr7, uint8_t c)
{
    return cmd(addr7, &c, 1);
}

HAL_StatusTypeDef oled_init(uint8_t addr7)
{
    /* Page addressing mode (0x20 0x02) rather than horizontal: the SH1106
     * clones ignore the column/page range commands, page mode works on both. */
    static const uint8_t seq[] = {
        0xAE,               /* display off */
        0xD5, 0x80,         /* clock divide / oscillator */
        0xA8, 0x3F,         /* multiplex ratio = 64 rows */
        0xD3, 0x00,         /* display offset */
        0x40,               /* start line 0 */
        0x8D, 0x14,         /* charge pump on (harmless on SH1106) */
        0x20, 0x02,         /* page addressing */
        0xA1,               /* segment remap: SEG0 = column 127 */
        0xC8,               /* COM scan direction reversed */
        0xDA, 0x12,         /* alternative COM pin config */
        0x81, 0x7F,         /* contrast */
        0xD9, 0xF1,         /* pre-charge */
        0xDB, 0x40,         /* VCOMH deselect */
        0xA4,               /* follow RAM, not all-on */
        0xA6,               /* non-inverted */
        0xAF,               /* display on */
    };

    for (uint32_t i = 0; i < sizeof(seq); i++) {
        HAL_StatusTypeDef st = cmd1(addr7, seq[i]);
        if (st != HAL_OK)
            return st;
    }
    HAL_Delay(100);                      /* let the charge pump settle */
    return HAL_OK;
}

HAL_StatusTypeDef oled_flush(uint8_t addr7)
{
    return oled_flush_pages(addr7, 0xFF);
}

HAL_StatusTypeDef oled_flush_pages(uint8_t addr7, uint8_t page_mask)
{
    for (uint8_t page = 0; page < OLED_PAGES; page++) {
        if (!(page_mask & (1u << page)))
            continue;

        uint8_t set[3] = { (uint8_t)(0xB0 | page), 0x00, 0x10 };   /* page, col lo, col hi */
        HAL_StatusTypeDef st = cmd(addr7, set, sizeof(set));
        if (st != HAL_OK)
            return st;

        uint8_t data[1 + OLED_W];
        data[0] = 0x40;                  /* Co=0, D/C#=1: data stream */
        for (uint16_t x = 0; x < OLED_W; x++)
            data[1 + x] = fb[page][x];
        st = i2c5_write(addr7, data, sizeof(data));
        if (st != HAL_OK)
            return st;
    }
    return HAL_OK;
}

HAL_StatusTypeDef oled_flush_dirty(uint8_t addr7)
{
    uint8_t mask = 0;

    for (uint8_t p = 0; p < OLED_PAGES; p++) {
        if (!shadow_valid) {
            mask = 0xFF;
            break;
        }
        for (uint16_t x = 0; x < OLED_W; x++) {
            if (fb[p][x] != shadow[p][x]) {
                mask |= (uint8_t)(1u << p);
                break;
            }
        }
    }
    last_mask = mask;
    if (!mask)
        return HAL_OK;

    HAL_StatusTypeDef st = oled_flush_pages(addr7, mask);
    if (st != HAL_OK) {
        shadow_valid = 0;                /* panel content now unknown */
        return st;
    }

    for (uint8_t p = 0; p < OLED_PAGES; p++)
        if (mask & (1u << p))
            for (uint16_t x = 0; x < OLED_W; x++)
                shadow[p][x] = fb[p][x];
    shadow_valid = 1;
    return HAL_OK;
}

uint8_t oled_last_mask(void)
{
    return last_mask;
}

void oled_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, int on)
{
    for (uint16_t j = 0; j < h; j++)
        for (uint16_t i = 0; i < w; i++)
            oled_pixel((uint16_t)(x + i), (uint16_t)(y + j), on);
}

void oled_sprite(int x, int y, const uint8_t *rows, int w, int h, int flip)
{
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            int bit = flip ? i : (w - 1 - i);

            if (rows[j] & (1u << bit))
                oled_pixel((uint16_t)(x + i), (uint16_t)(y + j), 1);
        }
    }
}

HAL_StatusTypeDef oled_invert(uint8_t addr7, int on)
{
    return cmd1(addr7, on ? 0xA7 : 0xA6);
}

/* One byte spans 8 vertically stacked pixels, bit 0 at the top. */
void oled_pixel(uint16_t x, uint16_t y, int on)
{
    if (x >= OLED_W || y >= OLED_H)
        return;

    uint8_t bit = (uint8_t)(1u << (y % 8));
    if (on)
        fb[y / 8][x] |= bit;
    else
        fb[y / 8][x] &= (uint8_t)~bit;
}

void oled_fill(uint8_t byte)
{
    for (uint8_t p = 0; p < OLED_PAGES; p++)
        for (uint16_t x = 0; x < OLED_W; x++)
            fb[p][x] = byte;
}

void oled_checkerboard(void)
{
    /* 8x8 squares: every other page inverts the pattern. */
    for (uint8_t p = 0; p < OLED_PAGES; p++)
        for (uint16_t x = 0; x < OLED_W; x++)
            fb[p][x] = (((x / 8) + p) & 1) ? 0xFF : 0x00;
}

void oled_frame(void)
{
    for (uint16_t x = 0; x < OLED_W; x++) {
        fb[0][x] |= 0x01;
        fb[OLED_PAGES - 1][x] |= 0x80;
    }
    for (uint8_t p = 0; p < OLED_PAGES; p++) {
        fb[p][0] = 0xFF;
        fb[p][OLED_W - 1] = 0xFF;
    }
}

void oled_text(uint8_t col, uint8_t page, const char *s, int invert)
{
    if (page >= OLED_PAGES)
        return;

    for (; *s && col < OLED_COLS; col++, s++) {
        uint8_t c = (uint8_t)*s;
        if (c < FONT_FIRST || c > FONT_LAST)
            c = '?';
        for (uint8_t i = 0; i < FONT_W; i++) {
            uint8_t bits = font8x8[c - FONT_FIRST][i];
            fb[page][col * FONT_W + i] = invert ? (uint8_t)~bits : bits;
        }
    }
}

void oled_bar(uint8_t page, uint8_t percent)
{
    if (page >= OLED_PAGES)
        return;
    if (percent > 100)
        percent = 100;

    uint16_t filled = (uint16_t)((OLED_W - 4) * percent / 100);
    for (uint16_t x = 0; x < OLED_W; x++) {
        if (x == 0 || x == OLED_W - 1)
            fb[page][x] = 0x7E;                  /* bar ends */
        else if (x < 2 + filled)
            fb[page][x] = 0x3C;                  /* filled body */
        else
            fb[page][x] = 0x42;                  /* empty body: top+bottom rule */
    }
}
