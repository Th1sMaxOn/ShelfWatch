#include "runtime.h"
#include "board.h"
#include "main.h"
#include "app.h"
#include "profile.h"
#include <stdio.h>
#include <string.h>
static Mpu6050 imu;
static bool ready;
static uint32_t last_imu, last_adc, last_status, last_retry;
static App app;
static uint16_t light;
static char command[80];
static unsigned command_length;
static bool drop_line;
static uint32_t tx_overflow_events;

static void event(void *context, uint32_t sequence, uint32_t time,
                  const char *source, const char *name, const char *level, int32_t value) {
    const App *state = context;
    printf("seq=%lu time_ms=%lu mode=%s src=%s event=%s level=%s value=%ld\r\n",
           (unsigned long)sequence, (unsigned long)time, app_mode_name(state),
           source, name, level, (long)value);
}
static void execute(const char *line, uint32_t now) {
    app.now = now;
    if (!strcmp(line, "imuaddr 0x68") || !strcmp(line, "imuaddr 0x69")) {
        imu.address = line[11] == '8' ? 0x68 : 0x69;
        ready = false; last_retry = now - 1000u;
        app_fault(&app, true, true, now);
        app_emit(&app, "CMD", "address_changed", "INFO", imu.address);
    } else if (!strcmp(line, "selftest")) {
        ImuSample sample;
        BusResult result = mpu6050_init(&imu);
        if (result == BUS_OK) result = mpu6050_read(&imu, &sample);
        app_emit(&app, "IMU", result == BUS_OK ? "selftest_pass" : "selftest_failed",
                 result == BUS_OK ? "INFO" : "ERROR", result);
        if (result != BUS_OK) {
            ready = false; last_retry = now;
            app_fault(&app, true, true, now);
        }
    } else {
        (void)app_command(&app, line, now);
    }
}
static void receive_commands(uint32_t now) {
    if (board_rx_overflow()) {
        drop_line = true; command_length = 0;
        app.now = now; app_emit(&app, "UART", "rx_overflow", "ERROR", 0);
    }
    for (unsigned budget = 0; budget < 128; ++budget) {
        int ch = board_get_char();
        if (ch < 0) break;
        if (ch == '\r' || ch == '\n') {
            if (drop_line) {
                app.now = now; app_emit(&app, "UART", "invalid_line", "ERROR", 0);
            } else if (command_length) {
                command[command_length] = 0;
                execute(command, now);
            }
            command_length = 0; drop_line = false;
        } else if (!drop_line) {
            if (!ch || command_length == sizeof(command) - 1) {
                drop_line = true; command_length = 0;
            } else command[command_length++] = (char)ch;
        }
    }
}
void runtime_init(void) {
    board_init();
    imu = (Mpu6050){board_sensor_bus(), 0x68};
    app_init(&app, PROJECT_KIND, event, &app);
    last_retry = HAL_GetTick() - 1000u;
    printf("project=%s sensors=MPU6050,PIR,LDR units=mg,mdps,adc commands=status,selftest,calibrate,mode,ack\r\n", PROJECT_NAME);
}
void runtime_poll(void) {
    uint32_t now = HAL_GetTick();
    receive_commands(now);
    app_pir(&app, board_motion(), now);
    if (!ready && (uint32_t)(now - last_retry) >= 1000u) {
        last_retry = now;
        BusResult result = mpu6050_init(&imu);
        ready = result == BUS_OK;
        app_fault(&app, true, !ready, now);
        if (ready) { app.now = now; app_emit(&app, "IMU", "whoami_0x68", "INFO", 0x68); }
    }
    if (ready && (uint32_t)(now - last_imu) >= IMU_PERIOD_MS) {
        last_imu = now;
        ImuSample sample;
        if (mpu6050_read(&imu, &sample) == BUS_OK) app_imu(&app, &sample, HAL_GetTick());
        else { ready = false; last_retry = now; app_fault(&app, true, true, HAL_GetTick()); }
    }
    now = HAL_GetTick();
    uint32_t adc_period = app.adc_fault ? 1000u : ADC_PERIOD_MS;
    if ((uint32_t)(now - last_adc) >= adc_period) {
        last_adc = now;
        bool ok = board_read_light(&light);
        app_fault(&app, false, !ok, HAL_GetTick());
        if (ok) app_light(&app, light, HAL_GetTick());
    }
    if (board_tx_overflow()) ++tx_overflow_events;
    if ((uint32_t)(now - last_status) >= 1000u) {
        last_status = now;
        printf("time_ms=%lu mode=%s imu=%s ax_mg=%ld ay_mg=%ld az_mg=%ld gx_mdps=%ld gy_mdps=%ld gz_mdps=%ld light_raw=%u light_valid=%u pir=%u baseline=%u,%u cal=%u alarm=%u tx_overflow_events=%lu\r\n",
               (unsigned long)now, app_mode_name(&app), ready ? "valid" : "invalid",
               (long)app.latest.accel_mg[0], (long)app.latest.accel_mg[1], (long)app.latest.accel_mg[2],
               (long)app.latest.gyro_mdps[0], (long)app.latest.gyro_mdps[1], (long)app.latest.gyro_mdps[2],
               light, !app.adc_fault && app.light_ready, app.pir, app.base_valid[0], app.base_valid[1],
               app.calibrating, app.alarm, (unsigned long)tx_overflow_events);
    }
    bool fault = app.imu_fault || app.adc_fault;
    board_led(fault ? ((now / 200u) % 2u) != 0 : app.alarm || ((now / 1000u) % 2u) != 0);
}
