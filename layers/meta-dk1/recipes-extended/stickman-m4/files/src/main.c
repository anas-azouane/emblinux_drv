/* I2C screen test for the Cortex-M4 of an STM32MP157x-DK1.
 *
 * Wiring (Arduino headers):
 *   VCC -> 3V3  CN16 pin 4      SDA -> D14  CN13 pin 9   (PA12, I2C5_SDA)
 *   GND -> GND  CN16 pin 6      SCL -> D15  CN13 pin 10  (PA11, I2C5_SCL)
 *
 * The firmware probes the bus, reports an i2cdetect-style map in the
 * remoteproc trace buffer, then drives whichever display answered: an
 * SSD1306/SH1106 OLED at 0x3c/0x3d, or an HD44780 behind a PCF8574 at
 * 0x27/0x3f.
 */
#include "main.h"
#include "trace.h"
#include "i2c5.h"
#include "ssd1306.h"
#include "lcd_i2c.h"
#include "game.h"
#include "rpmsg.h"

#define SCL_HZ      100000u
#define GAME_SCL_HZ 400000u

static void uptime(char *out, uint32_t out_sz)
{
    uint32_t s = HAL_GetTick() / 1000u;

    sfmt(out, out_sz, "%u:%2u:%2u", s / 3600u, (s / 60u) % 60u, s % 60u);
}

static void run_oled(uint8_t addr)
{
    char line[20], up[24];

    tprintf("oled: init at 0x%2x\n", (uint32_t)addr);
    if (oled_init(addr) != HAL_OK) {
        tprintf("oled: init failed, the device stopped acking\n");
        return;
    }

    /* Three obvious stages first: every pixel lit (spots dead pixels and proves
     * the charge pump), blank, then an 8x8 checkerboard. */
    oled_fill(0xFF);
    if (oled_flush(addr) != HAL_OK) {
        tprintf("oled: all-on frame failed\n");
        return;
    }
    tprintf("oled: all pixels on\n");
    HAL_Delay(1500);

    oled_fill(0x00);
    oled_flush(addr);
    HAL_Delay(400);

    oled_checkerboard();
    oled_flush(addr);
    tprintf("oled: checkerboard\n");
    HAL_Delay(1500);

    oled_fill(0x00);
    oled_text(0, 0, "MP157D-DK1 CM4  ", 1);
    sfmt(line, sizeof(line), "I2C5 0x%2x %ukHz", (uint32_t)addr, SCL_HZ / 1000u);
    oled_text(0, 1, line, 0);
    oled_text(0, 2, "SCL PA11 D15", 0);
    oled_text(0, 3, "SDA PA12 D14", 0);
    uptime(line, sizeof(line));
    sfmt(up, sizeof(up), "UP %s", line);
    oled_text(0, 4, up, 0);
    oled_text(0, 7, "  GAME NEXT     ", 1);
    if (oled_flush(addr) != HAL_OK) {
        tprintf("oled: info screen failed\n");
        return;
    }
    tprintf("oled: info screen\n");
    HAL_Delay(2500);

    /* The game refreshes continuously, so the bus speed is worth raising
     * now that the panel has proved itself at 100 kHz. */
    if (i2c5_init(GAME_SCL_HZ))
        tprintf("oled: bus moved to %u kHz for the game\n", GAME_SCL_HZ / 1000u);

    if (game_run(addr) != HAL_OK && !i2c5_init(SCL_HZ))
        tprintf("oled: falling back to %u kHz failed\n", SCL_HZ / 1000u);
}

static void run_lcd(uint8_t addr)
{
    uint32_t errors = 0;
    char line[24];

    tprintf("lcd: init at 0x%2x (PCF8574 + HD44780)\n", (uint32_t)addr);
    if (lcd_init(addr) != HAL_OK) {
        tprintf("lcd: init failed, the device stopped acking\n");
        return;
    }
    lcd_puts(addr, 0, 0, "MP157D CM4 I2C5");
    tprintf("lcd: header written, updating the clock line every second\n");

    for (;;) {
        char up[16];

        uptime(up, sizeof(up));
        sfmt(line, sizeof(line), "0x%2x UP %s ", (uint32_t)addr, up);
        if (lcd_puts(addr, 1, 0, line) != HAL_OK) {
            errors++;
            if (errors == 1 || errors % 50u == 0)
                tprintf("lcd: write failed (%u errors)\n", errors);
        }
        HAL_Delay(1000);
    }
}

static void no_device(void)
{
    trace_puts("\nNothing answered on I2C5. Worth checking:\n"
               "  - 3V3 on CN16 pin 4 and GND on CN16 pin 6\n"
               "  - SDA on CN13 pin 9 (D14/PA12), SCL on CN13 pin 10 (D15/PA11)\n"
               "  - SDA and SCL not swapped, and pull-ups present on the module\n"
               "  - a 5V-only display will not answer at 3V3\n"
               "Rescanning every 2 s.\n");

    for (uint32_t attempt = 1;; attempt++) {
        HAL_Delay(2000);

        uint8_t addr = 0;
        if (i2c5_scan(&addr)) {
            tprintf("device appeared at 0x%2x on rescan %u\n", (uint32_t)addr, attempt);
            return;                      /* main() picks the driver again */
        }
        tprintf("rescan %u: still nothing\n", attempt);
    }
}

int main(void)
{
    HAL_Init();

    __HAL_RCC_SYSCFG_CLK_ENABLE();

    rpmsg_init(game_command);

    tprintf("\nI2C screen test for STM32MP157 CM4, built " __DATE__ " " __TIME__ "\n");
    tprintf("core clock %u Hz, boot mode %s\n", SystemCoreClock,
            IS_ENGINEERING_BOOT_MODE() ? "engineering" : "production (Linux)");

    if (!i2c5_init(SCL_HZ)) {
        trace_puts("i2c5 unusable, stopping\n");
        Error_Handler();
    }

    for (;;) {
        uint8_t addr = 0;
        int found = i2c5_scan(&addr);

        tprintf("%u device(s) on the bus\n", (uint32_t)found);
        if (!found) {
            no_device();
            continue;
        }

        if (addr == 0x3c || addr == 0x3d) {
            run_oled(addr);
        } else if (addr == 0x27 || addr == 0x3f) {
            run_lcd(addr);
        } else {
            tprintf("0x%2x is not a known display address; trying SSD1306\n",
                    (uint32_t)addr);
            run_oled(addr);
        }

        /* Only reached when the display stopped acking: start over. */
        tprintf("display lost, rescanning\n");
        HAL_Delay(1000);
    }
}

/* __libc_init_array() from newlib calls these; crti.o, which normally
 * provides them, is left out by -nostartfiles. There are no C++ constructors
 * to run here. */
void _init(void) {}
void _fini(void) {}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    tprintf("assert failed: %s:%u\n", (const char *)file, line);
    Error_Handler();
}
#endif
