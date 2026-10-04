#include "i2c5.h"
#include "trace.h"

I2C_HandleTypeDef hi2c5;

/* TIMINGR has to be derived from whatever clock Linux left feeding I2C3/5,
 * which is why it is computed at run time instead of pasted from CubeMX.
 * Fields: PRESC[31:28] SCLDEL[23:20] SDADEL[19:16] SCLH[15:8] SCLL[7:0],
 * with tSCLL = (SCLL+1) x tPRESC and tPRESC = (PRESC+1) / f_i2cclk. */
static uint32_t timingr_for(uint32_t i2cclk_hz, uint32_t scl_hz)
{
    const uint64_t period_ps = 1000000000000ULL / scl_hz;

    for (uint32_t presc = 0; presc < 16; presc++) {
        uint64_t tpresc_ps = (uint64_t)(presc + 1) * 1000000000000ULL / i2cclk_hz;
        uint64_t ticks = period_ps / tpresc_ps;

        /* Need the low and high counts to fit in 8 bits each, and enough
         * ticks left that the duty cycle is still meaningful. */
        if (ticks < 24 || ticks > 480)
            continue;

        uint32_t scll = (uint32_t)(ticks * 2 / 3);     /* ~66% low, as in SMBus */
        uint32_t sclh = (uint32_t)(ticks - scll);
        if (scll > 256) scll = 256;
        if (sclh > 256) sclh = 256;

        /* Data setup before the rising edge, and data hold after the falling
         * edge: 1 us and 300 ns are comfortably inside the 100/400 kHz specs. */
        uint32_t scldel = (uint32_t)(1000000ULL / tpresc_ps);
        uint32_t sdadel = (uint32_t)(300000ULL / tpresc_ps);
        if (scldel > 15) scldel = 15;
        if (scldel == 0) scldel = 1;
        if (sdadel > 15) sdadel = 15;

        return (presc << 28) | (scldel << 20) | (sdadel << 16) |
               ((sclh - 1) << 8) | (scll - 1);
    }
    return 0;
}

uint32_t i2c5_init(uint32_t scl_hz)
{
    uint32_t clk = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_I2C35);
    uint32_t src = (RCC->I2C35CKSELR & RCC_I2C35CKSELR_I2C35SRC);
    static const char *const src_name[] = { "pclk1", "pll4_r", "hsi_ker", "csi_ker" };

    tprintf("i2c5: kernel clock = %s, %u Hz\n", src_name[src & 3u], clk);

    if (clk < 1000000u) {
        tprintf("i2c5: implausible kernel clock, aborting\n");
        return 0;
    }

    uint32_t timing = timingr_for(clk, scl_hz);
    if (!timing) {
        tprintf("i2c5: no TIMINGR fits %u Hz from a %u Hz clock\n", scl_hz, clk);
        return 0;
    }
    tprintf("i2c5: %u Hz requested, TIMINGR = 0x%8x\n", scl_hz, timing);

    if (hi2c5.Instance)                  /* re-init: drop the old config first */
        HAL_I2C_DeInit(&hi2c5);

    hi2c5.Instance = I2C5;
    hi2c5.Init.Timing = timing;
    hi2c5.Init.OwnAddress1 = 0;
    hi2c5.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c5.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c5.Init.OwnAddress2 = 0;
    hi2c5.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c5.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c5.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c5) != HAL_OK) {
        tprintf("i2c5: HAL_I2C_Init failed\n");
        return 0;
    }
    if (HAL_I2CEx_ConfigAnalogFilter(&hi2c5, I2C_ANALOGFILTER_ENABLE) != HAL_OK ||
        HAL_I2CEx_ConfigDigitalFilter(&hi2c5, 0) != HAL_OK) {
        tprintf("i2c5: filter config failed\n");
        return 0;
    }
    return clk;
}

int i2c5_present(uint8_t addr7)
{
    return HAL_I2C_IsDeviceReady(&hi2c5, (uint16_t)(addr7 << 1), 2, 10) == HAL_OK;
}

HAL_StatusTypeDef i2c5_write(uint8_t addr7, const uint8_t *data, uint16_t len)
{
    return HAL_I2C_Master_Transmit(&hi2c5, (uint16_t)(addr7 << 1),
                                  (uint8_t *)data, len, 200);
}

int i2c5_scan(uint8_t *first)
{
    int found = 0;

    trace_puts("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");
    for (uint8_t row = 0; row < 8; row++) {
        tprintf("%2x: ", (uint32_t)(row << 4));
        for (uint8_t col = 0; col < 16; col++) {
            uint8_t addr = (uint8_t)((row << 4) | col);

            if (addr < 0x08 || addr > 0x77) {
                trace_puts("   ");
                continue;
            }
            if (i2c5_present(addr)) {
                tprintf("%2x ", (uint32_t)addr);
                if (!found && first)
                    *first = addr;
                found++;
            } else {
                trace_puts("-- ");
            }
        }
        trace_puts("\n");
    }
    return found;
}
