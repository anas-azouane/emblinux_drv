/* I2C5 on the DK1 Arduino header: PA11 = SCL (D15), PA12 = SDA (D14). */
#ifndef I2C5_H
#define I2C5_H

#include <stdint.h>
#include "stm32mp1xx_hal.h"

extern I2C_HandleTypeDef hi2c5;

/* Returns the I2C kernel clock it configured the bus for, or 0 on failure. */
uint32_t i2c5_init(uint32_t scl_hz);

/* 7-bit addresses, as printed by i2cdetect. */
int i2c5_present(uint8_t addr7);
HAL_StatusTypeDef i2c5_write(uint8_t addr7, const uint8_t *data, uint16_t len);

/* Probes 0x08..0x77 and logs an i2cdetect-style map. Returns how many
 * devices answered and stores the first address in *first. */
int i2c5_scan(uint8_t *first);

#endif /* I2C5_H */
