#ifndef DEVICE_MPU6050_H
#define DEVICE_MPU6050_H
#include <stddef.h>
#include <stdint.h>

typedef enum { BUS_OK, BUS_NACK, BUS_TIMEOUT, BUS_ERROR } BusResult;
typedef struct {
    void *context;
    BusResult (*read)(void *, uint8_t, uint8_t, uint8_t *, size_t);
    BusResult (*write)(void *, uint8_t, uint8_t, const uint8_t *, size_t);
} SensorBus;
typedef struct { SensorBus bus; uint8_t address; } Mpu6050;
typedef struct { int32_t accel_mg[3], gyro_mdps[3], temperature_mc; } ImuSample;
BusResult mpu6050_init(Mpu6050 *device);
BusResult mpu6050_read(Mpu6050 *device, ImuSample *sample);
void mpu6050_decode(const uint8_t bytes[14], ImuSample *sample);
#endif
