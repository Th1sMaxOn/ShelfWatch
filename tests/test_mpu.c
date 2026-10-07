#include "mpu6050.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { BusResult result; uint8_t id; unsigned writes, reads; } Fake;
static BusResult read_bus(void *context, uint8_t address, uint8_t reg, uint8_t *data, size_t size) {
    Fake *fake = context;
    assert(address == 0x68);
    fake->reads++;
    if (fake->result != BUS_OK) return fake->result;
    if (reg == 0x75) { assert(size == 1); data[0] = fake->id; }
    else { assert(reg == 0x3b && size == 14); memset(data, 0, size); data[4] = 0x40; }
    return BUS_OK;
}
static BusResult write_bus(void *context, uint8_t address, uint8_t reg, const uint8_t *data, size_t size) {
    static const uint8_t registers[] = {0x6b, 0x19, 0x1a, 0x1b, 0x1c};
    static const uint8_t values[] = {1, 9, 3, 0, 0};
    Fake *fake = context;
    assert(address == 0x68 && size == 1);
    assert(reg == registers[fake->writes] && data[0] == values[fake->writes]);
    fake->writes++;
    return fake->result;
}
int main(void) {
    Fake fake = {BUS_OK, 0x68, 0, 0};
    Mpu6050 device = {{&fake, read_bus, write_bus}, 0x68};
    assert(mpu6050_init(&device) == BUS_OK && fake.writes == 5);
    ImuSample sample;
    assert(mpu6050_read(&device, &sample) == BUS_OK && sample.accel_mg[2] == 1000);
    assert(sample.temperature_mc == 36530);
    fake.id = 0x69; fake.writes = 0;
    assert(mpu6050_init(&device) == BUS_ERROR && fake.writes == 0);
    for (BusResult failure = BUS_NACK; failure <= BUS_ERROR; ++failure) {
        fake.result = failure;
        memset(&sample, 0x55, sizeof(sample));
        ImuSample before = sample;
        assert(mpu6050_init(&device) == failure);
        assert(mpu6050_read(&device, &sample) == failure);
        assert(memcmp(&sample, &before, sizeof(sample)) == 0);
    }
    const uint8_t limits[14] = {0x80,0, 0x7f,0xff, 0x40,0, 0,0, 0x80,0, 0x7f,0xff, 0,0};
    mpu6050_decode(limits, &sample);
    assert(sample.accel_mg[0] == -2000 && sample.accel_mg[1] == 1999);
    assert(sample.gyro_mdps[0] == -250137 && sample.gyro_mdps[1] == 250129);
    puts("MPU6050: identity, initialization order, units, signed limits and bus failures passed");
}
