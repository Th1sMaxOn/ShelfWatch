#include "runtime.h"
#include "board.h"
#include "main.h"
#include <stdio.h>
static Mpu6050 imu;
static bool ready;
static uint32_t last_imu, last_adc, last_status, last_retry;
static ImuSample sample;
static uint16_t light;
void runtime_init(void) {
    board_init(); imu = (Mpu6050){board_sensor_bus(), 0x68};
    last_retry = HAL_GetTick() - 1000u;
    puts("sensor bringup ready");
}
void runtime_poll(void) {
    uint32_t now = HAL_GetTick();
    if (!ready && (uint32_t)(now-last_retry)>=1000u) {
        last_retry=now; ready=mpu6050_init(&imu)==BUS_OK;
        printf("time_ms=%lu imu=%s\r\n",(unsigned long)now,ready?"ready":"error");
    }
    if (ready && (uint32_t)(now-last_imu)>=20u) {
        last_imu=now;
        if (mpu6050_read(&imu,&sample)!=BUS_OK) {ready=false;last_retry=now;}
    }
    if ((uint32_t)(now-last_adc)>=20u) {last_adc=now;(void)board_read_light(&light);}
    if ((uint32_t)(now-last_status)>=1000u) {
        last_status=now;
        printf("time_ms=%lu az_mg=%ld light_raw=%u pir=%u\r\n",(unsigned long)now,(long)sample.accel_mg[2],light,board_motion());
    }
    board_led((now/1000u)%2u);
}
