/* Interface every game on this firmware implements. Exactly one game is
 * compiled in; pick it with `make GAME=stickman` or `make GAME=snake`. */
#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include "stm32mp1xx_hal.h"

/* Runs until the display stops acking, so the caller can rescan the bus. */
HAL_StatusTypeDef game_run(uint8_t addr7);

/* Commands from Linux, delivered over rpmsg. Each game documents its own
 * letters; see the README. */
void game_command(const uint8_t *data, uint32_t len);

#endif /* GAME_H */
