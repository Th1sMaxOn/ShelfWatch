#include "app.h"
#include "board.h"
#include "profile.h"
#include "runtime.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static uint32_t tick, next_tick;
static bool pir, fail_bus, fail_adc, overflow_rx;
static uint16_t light = 1000;
static int16_t accel_x, accel_z = 16384;
static char input[2048], output[65536];
static unsigned input_read, input_size;
static size_t output_size;
static unsigned probes;
static unsigned failed_adc_attempts;
static uint32_t probe_time[256];
uint32_t HAL_GetTick(void) { return tick; }
int printf(const char *format, ...) {
    va_list args; va_start(args, format);
    int written = vsnprintf(output + output_size, sizeof(output) - output_size, format, args);
    va_end(args);
    assert(written >= 0 && (size_t)written < sizeof(output) - output_size);
    output_size += (size_t)written;
    return written;
}
static void word(uint8_t *destination, int16_t value) {
    destination[0] = (uint8_t)((uint16_t)value >> 8); destination[1] = (uint8_t)value;
}
static BusResult read_bus(void *context, uint8_t address, uint8_t reg, uint8_t *data, size_t size) {
    (void)context;
    if (reg == 0x75) { assert(probes < 256); probe_time[probes++] = tick; }
    if (fail_bus || address != 0x68) return BUS_NACK;
    if (reg == 0x75) { assert(size == 1); data[0] = 0x68; }
    else {
        assert(reg == 0x3b && size == 14); memset(data, 0, size);
        word(data, accel_x); word(data + 4, accel_z);
    }
    return BUS_OK;
}
static BusResult write_bus(void *context, uint8_t address, uint8_t reg, const uint8_t *data, size_t size) {
    (void)context; (void)reg; (void)data; assert(size == 1);
    return fail_bus || address != 0x68 ? BUS_NACK : BUS_OK;
}
void board_init(void) {}
SensorBus board_sensor_bus(void) { return (SensorBus){NULL, read_bus, write_bus}; }
bool board_read_light(uint16_t *value) {
    if (fail_adc) { ++failed_adc_attempts; return false; }
    *value = light; return true;
}
bool board_motion(void) { return pir; }
void board_led(bool value) { (void)value; }
int board_get_char(void) { return input_read == input_size ? -1 : (unsigned char)input[input_read++]; }
int board_put_char(int value) { return value; }
bool board_rx_overflow(void) { bool before = overflow_rx; overflow_rx = false; return before; }
bool board_tx_overflow(void) { return false; }
static void send(const char *text) {
    assert(input_read == input_size);
    input_size = (unsigned)strlen(text); assert(input_size < sizeof(input));
    memcpy(input, text, input_size); input_read = 0;
}
static void run(uint32_t milliseconds) {
    uint32_t until = next_tick + milliseconds;
    while (next_tick < until) { tick = next_tick++; runtime_poll(); }
}
static unsigned occurrences(const char *needle) {
    unsigned count = 0; const char *position = output;
    while ((position = strstr(position, needle))) { ++count; position += strlen(needle); }
    return count;
}
int main(void) {
    runtime_init(); run(1);
    if (PROJECT_KIND == LABWATCH || PROJECT_KIND == EXPOGUARD) send("calibrate\n");
    run(PROJECT_KIND == LABWATCH ? 11000 : 3000);
    assert(strstr(output, "event=calibration_ready"));
    if (PROJECT_KIND == EXPOGUARD) send("mode guard\n");
    if (PROJECT_KIND == SHELFWATCH) send("mode armed\n");
    run(10);
    pir = true; light = 4000; accel_x = 32700;
    run(600);
    assert(strstr(output, "level=ALARM"));
    assert(strstr(output, PROJECT_KIND == EXPOGUARD ? "event=dark" : "event=light_change"));
    unsigned before_pir = occurrences("src=GPIO");
    run(300); assert(occurrences("src=GPIO") == before_pir);

    unsigned start_probe = probes;
    send("imuaddr 0x69\n"); run(3500);
    assert(strstr(output, "event=sensor_error") && strstr(output, "imu=invalid"));
    assert(probes - start_probe >= 3 && probes - start_probe <= 4);
    for (unsigned i = start_probe + 1; i < probes; ++i) assert(probe_time[i] - probe_time[i - 1] >= 1000);
    pir = false; run(2); pir = true; run(2);
    assert(occurrences("src=GPIO") == before_pir + 1);

    accel_x = 0; light = 1000;
    send("imuaddr 0x68\n"); run(3000);
    assert(strstr(output, "event=sensor_recovered"));
    assert(occurrences("event=whoami_0x68") >= 2);
    fail_adc = true; run(300);
    assert(failed_adc_attempts == 1);
    assert(strstr(output, "src=ADC event=sensor_error"));
    pir = false; run(2); pir = true; run(2);
    assert(occurrences("src=GPIO") == before_pir + 2);
    fail_adc = false; run(1500); assert(strstr(output, "src=ADC event=sensor_recovered"));

    char long_line[150]; memset(long_line, 'x', sizeof(long_line) - 1);
    long_line[sizeof(long_line) - 2] = '\n'; long_line[sizeof(long_line) - 1] = 0;
    send(long_line); run(2); assert(strstr(output, "event=invalid_line"));
    unsigned status_before = occurrences("event=status");
    overflow_rx = true; send("status\n"); run(2);
    assert(occurrences("event=status") == status_before);
    send("status\r\n"); run(2); assert(occurrences("event=status") == status_before + 1);
    fail_bus = true; send("selftest\n"); run(2); assert(strstr(output, "event=selftest_failed"));
    fputs("Runtime fake: commands, simultaneous sensors, NACK/retry, independent ADC/PIR, recovery and line rejection passed\n", stderr);
}
