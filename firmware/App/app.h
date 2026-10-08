#ifndef PROJECT_APP_H
#define PROJECT_APP_H
#include "mpu6050.h"
#include <stdbool.h>
#include <stdint.h>
typedef enum { LABWATCH, TOOLSAFE, SHELFWATCH, EXPOGUARD } ProjectKind;
typedef void (*EventSink)(void *, uint32_t, uint32_t, const char *, const char *, const char *, int32_t);
typedef struct {
    ProjectKind kind;
    unsigned mode;
    uint32_t now, sequence;
    EventSink sink; void *sink_context;
    bool alarm, imu_fault, adc_fault, pir, calibrating, base_valid[2];
    bool light_ready, light_base_valid, impact_active, motion_active, light_active, motion_candidate, light_candidate;
    uint32_t motion_since, light_since, quiet_since;
    int32_t base[2][3], noise[2];
    int32_t cal_sum[3], cal_min[3], cal_max[3], cal_gyro_max;
    unsigned cal_count;
    int32_t light_window[5], light_sum, light_mean, light_base;
    unsigned light_count, light_position;
    int32_t impact_limit, motion_limit, light_limit;
    ImuSample latest;
} App;
void app_init(App *, ProjectKind, EventSink, void *);
void app_emit(App *, const char *, const char *, const char *, int32_t);
const char *app_mode_name(const App *);
void app_start_calibration(App *);
void app_imu(App *, const ImuSample *, uint32_t);
void app_light(App *, uint16_t, uint32_t);
void app_pir(App *, bool, uint32_t);
void app_fault(App *, bool imu, bool fault, uint32_t);
bool app_command(App *, const char *, uint32_t);
void events_imu(App *);
void events_light(App *);
void events_pir(App *);
#endif
