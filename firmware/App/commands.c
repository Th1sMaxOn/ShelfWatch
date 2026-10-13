#include "app.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* Fixed decimal units, up to three fractional digits. No floating parser. */
static bool number(const char *text, int32_t scale, int32_t *value) {
    int64_t whole = 0, fraction = 0, divisor = 1;
    unsigned digits = 0;
    if (*text < '0' || *text > '9') return false;
    while (*text >= '0' && *text <= '9') {
        whole = whole * 10 + (*text++ - '0');
        if (whole > INT32_MAX) return false;
    }
    if (*text == '.') {
        ++text;
        while (*text >= '0' && *text <= '9') {
            if (++digits > 3) return false;
            fraction = fraction * 10 + (*text++ - '0'); divisor *= 10;
        }
        if (!digits) return false;
    }
    if (*text) return false;
    int64_t scaled = whole * scale + fraction * scale / divisor;
    if (scaled > INT32_MAX || fraction * scale % divisor) return false;
    *value = (int32_t)scaled;
    return true;
}
static bool bad(App *app, const char *event) {
    app_emit(app, "CMD", event, "ERROR", 0);
    return false;
}
bool app_command(App *app, const char *line, uint32_t now) {
    app->now = now;
    if (app->kind == TOOLSAFE && !strcmp(line, "issue")) return app_command(app, "mode issue", now);
    if (app->kind == TOOLSAFE && !strcmp(line, "guard")) return app_command(app, "mode guard", now);
    if (!strcmp(line, "status")) {
        app_emit(app, "CMD", "status", "INFO", app->alarm);
        return true;
    }
    if (!strcmp(line, "calibrate")) {
        if (app->imu_fault || app->adc_fault || app->calibrating ||
            (app->kind == TOOLSAFE && app->mode != 1) ||
            (app->kind == LABWATCH && app->mode == 2)) return bad(app, "calibration_denied");
        app_start_calibration(app);
        return true;
    }
    if (!strcmp(line, "ack")) {
        unsigned slot = app->kind == LABWATCH && app->mode == 1 ? 1 : 0;
        bool light_alarm = app->kind == LABWATCH || app->kind == TOOLSAFE;
        if (app->imu_fault || app->adc_fault || !app->base_valid[slot] || !app->light_ready ||
            app->calibrating || app->impact_active || app->motion_active ||
            app->motion_candidate || (light_alarm && (app->light_active || app->light_candidate)) ||
            (uint32_t)(now - app->quiet_since) < 2000u) return bad(app, "ack_denied");
        app->alarm = false;
        app_emit(app, "CMD", "acknowledged", "INFO", 0);
        return true;
    }
    if (!strncmp(line, "mode ", 5)) {
        static const char *const modes[4][3] = {
            {"idle", "work", "service"}, {"guard", "issue", NULL},
            {"disarmed", "inspection", "armed"}, {"expo", "move", "guard"}
        };
        unsigned next;
        for (next = 0; next < 3; ++next)
            if (modes[app->kind][next] && !strcmp(line + 5, modes[app->kind][next])) break;
        if (next == 3) return bad(app, "unknown_mode");
        bool guard = (app->kind == TOOLSAFE && next == 0) ||
                     (app->kind == SHELFWATCH && next == 2) ||
                     (app->kind == EXPOGUARD && next == 2);
        if (guard && (!app->base_valid[0] || app->imu_fault || app->adc_fault ||
                      !app->light_ready || app->calibrating)) return bad(app, "calibration_required");
        if (app->mode == next) { app_emit(app, "CMD", "mode_unchanged", "INFO", next); return true; }
        if (app->calibrating) {
            app->calibrating = false;
            app_emit(app, "CAL", "calibration_interrupted", "ERROR", 0);
        }
        if (app->kind == LABWATCH && app->mode == 2) {
            app->base_valid[0] = false; app->base_valid[1] = false;
            app->missing_notices = 0;
        }
        unsigned previous = app->mode;
        const char *previous_name = app_mode_name(app);
        app->mode = next;
        app->motion_candidate = false; app->light_candidate = false;
        char event[64];
        snprintf(event, sizeof(event), "mode_%s_to_%s", previous_name, app_mode_name(app));
        app_emit(app, "CMD", event, "INFO", previous);
        if (guard && (app->impact_active || app->motion_active || (app->kind == TOOLSAFE && app->light_active)))
            app_emit(app, "MODE", "active_on_guard_entry", "ALARM", 0);
        if (app->kind == SHELFWATCH && next == 2 && app->pir)
            app_emit(app, "PIR", "motion_on_arm", "ALARM", 1);
        return true;
    }
    if (!strncmp(line, "threshold ", 10) && app->kind == SHELFWATCH) {
        const char *argument = line + 10;
        int32_t value, *target = NULL, scale = 1, minimum = 0, maximum = 0;
        if (!strncmp(argument, "impact ", 7)) {
            target = &app->impact_limit; scale = 1000; minimum = 100; maximum = 1500; argument += 7;
        } else if (!strncmp(argument, "motion ", 7)) {
            target = &app->motion_limit; scale = 1000; minimum = 5000; maximum = 200000; argument += 7;
        } else if (!strncmp(argument, "light ", 6)) {
            target = &app->light_limit; minimum = 100; maximum = 3000; argument += 6;
        }
        if (!target || !number(argument, scale, &value) || value < minimum || value > maximum)
            return bad(app, "invalid_threshold");
        *target = value;
        app_emit(app, "CMD", "threshold_changed", "INFO", value);
        return true;
    }
    return bad(app, "command_error");
}
