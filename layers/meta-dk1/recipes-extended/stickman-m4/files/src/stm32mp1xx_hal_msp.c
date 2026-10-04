#include "main.h"

void HAL_MspInit(void)
{
}

/* I2C5 reaches the Arduino header on PA11 (SCL, D15) and PA12 (SDA, D14).
 * Linux leaves i2c@40015000 disabled on the DK boards, so the M4 owns the
 * pins and the peripheral clock gate. */
void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
    GPIO_InitTypeDef gpio = { 0 };

    if (hi2c->Instance != I2C5)
        return;

    if (IS_ENGINEERING_BOOT_MODE()) {
        /* Only reachable when no A7 firmware owns the clock tree. Under Linux
         * the kernel clock mux keeps whatever TF-A left, and i2c5_init()
         * derives TIMINGR from it. */
        RCC_PeriphCLKInitTypeDef periph = { 0 };

        periph.PeriphClockSelection = RCC_PERIPHCLK_I2C35;
        periph.I2c35ClockSelection = RCC_I2C35CLKSOURCE_HSI;
        HAL_RCCEx_PeriphCLKConfig(&periph);
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_NOPULL;             /* the screen module carries the pull-ups */
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF4_I2C5;
    HAL_GPIO_Init(GPIOA, &gpio);

    __HAL_RCC_I2C5_CLK_ENABLE();
}

void HAL_I2C_MspDeInit(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance != I2C5)
        return;

    __HAL_RCC_I2C5_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11 | GPIO_PIN_12);
}
