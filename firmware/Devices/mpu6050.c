#include "mpu6050.h"

static int32_t signed_word(const uint8_t *data) {
    uint32_t word = ((uint32_t)data[0] << 8) | data[1];
    return word < 32768u ? (int32_t)word : (int32_t)word - 65536;
}

void mpu6050_decode(const uint8_t bytes[14], ImuSample *sample) {
    for (size_t axis = 0; axis < 3; ++axis) {
        sample->accel_mg[axis] = signed_word(bytes + 2 * axis) * 1000 / 16384;
        sample->gyro_mdps[axis] = signed_word(bytes + 8 + 2 * axis) * 1000 / 131;
    }
    sample->temperature_mc = signed_word(bytes + 6) * 1000 / 340 + 36530;
}

BusResult mpu6050_init(Mpu6050 *device) {
    uint8_t id = 0;
    BusResult result = device->bus.read(device->bus.context, device->address, 0x75, &id, 1);
    if (result != BUS_OK) return result;
    if (id != 0x68) return BUS_ERROR;
    const uint8_t config[][2] = {{0x6b, 0x01}, {0x19, 0x09}, {0x1a, 0x03}, {0x1b, 0}, {0x1c, 0}};
    for (size_t index = 0; index < sizeof(config) / sizeof(config[0]); ++index) {
        result = device->bus.write(device->bus.context, device->address, config[index][0], &config[index][1], 1);
        if (result != BUS_OK) return result;
    }
    return BUS_OK;
}

BusResult mpu6050_read(Mpu6050 *device, ImuSample *sample) {
    uint8_t bytes[14];
    BusResult result = device->bus.read(device->bus.context, device->address, 0x3b, bytes, sizeof(bytes));
    if (result == BUS_OK) mpu6050_decode(bytes, sample);
    return result;
}
