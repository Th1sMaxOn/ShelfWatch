#ifndef PROJECT_BOARD_H
#define PROJECT_BOARD_H
#include "mpu6050.h"
#include <stdbool.h>
#include <stdint.h>
void board_init(void);
SensorBus board_sensor_bus(void);
bool board_read_light(uint16_t *value);
bool board_motion(void);
void board_led(bool on);
int board_get_char(void);
bool board_rx_overflow(void);
#endif
