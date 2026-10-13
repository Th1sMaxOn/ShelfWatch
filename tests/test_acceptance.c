#include "app.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { unsigned count, alarms; char last[64]; } Log;
static void sink(void *context, uint32_t seq, uint32_t time, const char *source,
                 const char *event, const char *level, int32_t value) {
    (void)seq; (void)time; (void)source; (void)value;
    Log *log = context; ++log->count;
    if (!strcmp(level, "ALARM")) ++log->alarms;
    snprintf(log->last, sizeof(log->last), "%s", event);
}
static void init(App *app, Log *log, ProjectKind kind) {
    memset(log, 0, sizeof(*log)); app_init(app, kind, sink, log);
    app->calibrating = false; app->base_valid[0] = app->base_valid[1] = true;
    app->base[0][2] = app->base[1][2] = 1000;
    for (unsigned i = 0; i < 5; ++i) app_light(app, 1000, i * 20);
    memset(log, 0, sizeof(*log));
}
int main(void) {
    App app; Log log;
    ImuSample normal = {{0, 0, 1000}, {0, 0, 0}, 25000};
    puts("ToolSafe command names and strict physical thresholds"); fflush(stdout);
    init(&app, &log, TOOLSAFE);
    assert(app_command(&app, "issue", 100) && app.mode == 1);
    assert(app_command(&app, "guard", 200) && app.mode == 0);
    memset(&log, 0, sizeof(log));
    ImuSample sample = normal; sample.accel_mg[2] = 1800;
    app_imu(&app, &sample, 300); assert(log.alarms == 0);
    sample.accel_mg[2] = 1801; app_imu(&app, &sample, 320); assert(log.alarms == 1);

    puts("ExpoGuard darkness does not prevent mechanical acknowledgement"); fflush(stdout);
    init(&app, &log, EXPOGUARD); app.alarm = true; app.light_active = true;
    app.light_candidate = true; app.quiet_since = 0;
    assert(app_command(&app, "ack", 2000) && !app.alarm);
    init(&app, &log, SHELFWATCH); app.alarm = true;
    for (unsigned i = 0; i < 10; ++i) app_light(&app, 2000, 1500 + i * 50);
    assert(app.light_active && app_command(&app, "ack", 2100) && !app.alarm);

    puts("Vector gyro norm and per-project event rearming interval"); fflush(stdout);
    init(&app, &log, EXPOGUARD); sample = normal;
    sample.gyro_mdps[0] = sample.gyro_mdps[1] = sample.gyro_mdps[2] = 20000;
    app_imu(&app, &sample, 100); app_imu(&app, &sample, 120); assert(log.count == 1);
    app_imu(&app, &normal, 150);
    app_imu(&app, &sample, 200); app_imu(&app, &sample, 220); assert(log.count == 1);
    app_imu(&app, &normal, 1100);
    app_imu(&app, &sample, 1150); app_imu(&app, &sample, 1170); assert(log.count == 2);

    init(&app, &log, SHELFWATCH); app.mode = 1; sample = normal; sample.accel_mg[0] = 800;
    app_imu(&app, &sample, 100); assert(log.alarms == 1);
    app_imu(&app, &normal, 110); app_imu(&app, &sample, 200); assert(log.alarms == 1);
    app_imu(&app, &normal, 700); app_imu(&app, &sample, 710); assert(log.alarms == 2);

    puts("Missing LabWatch baseline emits one explicit notice"); fflush(stdout);
    init(&app, &log, LABWATCH); app.base_valid[0] = false;
    app_imu(&app, &normal, 100); app_imu(&app, &normal, 120);
    assert(log.count == 1 && log.alarms == 0 && !strcmp(log.last, "calibration_required"));

    puts("Calibration rejects vector speed above the limit"); fflush(stdout);
    init(&app, &log, EXPOGUARD); app_start_calibration(&app); sample = normal;
    sample.gyro_mdps[0] = sample.gyro_mdps[1] = sample.gyro_mdps[2] = 2000;
    for (unsigned i = 0; i < 100; ++i) app_imu(&app, &sample, i * 20 + 100);
    assert(!strcmp(log.last, "calibration_rejected"));

    puts("Threshold parsing is atomic for invalid and boundary values"); fflush(stdout);
    init(&app, &log, SHELFWATCH);
    const char *bad[] = {"-1", "nan", "1e2", "1.5001", "0.099", "2147483648", "1.6", "1 ", ".5", "1."};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        char command[80]; snprintf(command, sizeof(command), "threshold impact %s", bad[i]);
        int32_t before = app.impact_limit;
        assert(!app_command(&app, command, 100) && app.impact_limit == before);
    }
    assert(app_command(&app, "threshold impact 0.1", 100) && app.impact_limit == 100);
    assert(app_command(&app, "threshold impact 1.5", 100) && app.impact_limit == 1500);
    assert(app_command(&app, "threshold motion 5", 100) && app.motion_limit == 5000);
    assert(app_command(&app, "threshold motion 200", 100) && app.motion_limit == 200000);
    assert(app_command(&app, "threshold light 100", 100) && app.light_limit == 100);
    assert(app_command(&app, "threshold light 3000", 100) && app.light_limit == 3000);
    unsigned before = app.mode; assert(!app_command(&app, "mode bad", 100) && app.mode == before);
    app.alarm = true; app.quiet_since = UINT32_MAX - 1000u;
    assert(app_command(&app, "ack", 999) && !app.alarm);
    puts("Acceptance boundary and recovery checks passed");
}
