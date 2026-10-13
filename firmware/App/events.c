#include "app.h"
#include <math.h>
#include <stdlib.h>

static int32_t norm(const int32_t v[3]) {
    return (int32_t)sqrtf((float)v[0]*v[0]+(float)v[1]*v[1]+(float)v[2]*v[2]);
}
static const char *mechanical_level(const App *app) {
    switch(app->kind) {
    case LABWATCH:return app->mode==2?"INFO":"ALARM";
    case TOOLSAFE:return app->mode==1?"INFO":"ALARM";
    case SHELFWATCH:return app->mode==0?"INFO":"ALARM";
    case EXPOGUARD:return app->mode==2?"ALARM":"INFO";
    }
    return "ERROR";
}
void events_imu(App *app) {
    unsigned slot=app->kind==LABWATCH&&app->mode==1?1:0;
    bool base=app->base_valid[slot];
    int32_t difference[3],rotation=norm(app->latest.gyro_mdps);
    for(unsigned axis=0;axis<3;++axis) {
        difference[axis]=app->latest.accel_mg[axis]-app->base[slot][axis];
    }
    if(!base && app->kind==LABWATCH && !(app->missing_notices&(1u<<slot))) {
        app->missing_notices|=1u<<slot;
        app_emit(app,"CAL","calibration_required","INFO",slot);
    }
    int32_t deviation=base?norm(difference):0;
    int32_t impact=app->kind==TOOLSAFE?abs(norm(app->latest.accel_mg)-norm(app->base[slot])):deviation;
    int32_t limit=app->impact_limit+(app->kind==LABWATCH?app->noise[slot]:0);
    if(base && app->kind!=EXPOGUARD) {
        bool over=app->kind==SHELFWATCH?impact>=limit:impact>limit;
        uint32_t repeat=app->kind==SHELFWATCH?500:0;
        if(over&&!app->impact_active && (!app->impact_seen || (uint32_t)(app->now-app->impact_last)>=repeat)) {
            app->impact_active=true;
            app->impact_seen=true;app->impact_last=app->now;
            app_emit(app,"IMU",app->kind==LABWATCH?"shock_candidate":"impact",mechanical_level(app),impact);
        } else if(impact<limit*5/8)app->impact_active=false;
    }
    bool moving=app->kind==SHELFWATCH?rotation>=app->motion_limit:rotation>app->motion_limit;
    uint32_t duration=app->kind==SHELFWATCH?200:app->kind==EXPOGUARD?20:0;
    if(app->kind==EXPOGUARD) {
        int32_t gyro_limit=app->motion_active?app->motion_limit*2/3:app->motion_limit;
        int32_t accel_limit=app->motion_active?200:300;
        moving=rotation>gyro_limit||(base&&deviation>accel_limit);
    }
    if(app->kind==LABWATCH) {
        if(!base)return;
        int64_t dot=0,a2=0,b2=0;
        for(unsigned axis=0;axis<3;++axis) {
            int64_t a=app->latest.accel_mg[axis],b=app->base[slot][axis];dot+=a*b;a2+=a*a;b2+=b*b;
        }
        moving=a2>250000 && b2>250000 && (dot<=0 || dot*dot*10000<a2*b2*9698);
        duration=500;
    }
    if(!base && app->kind!=EXPOGUARD)moving=false;
    if(moving) {
        if(!app->motion_candidate){app->motion_candidate=true;app->motion_since=app->now;}
        uint32_t repeat=app->kind==EXPOGUARD?1000:app->kind==SHELFWATCH?500:0;
        if(!app->motion_active && (uint32_t)(app->now-app->motion_since)>=duration &&
           (!app->motion_seen || (uint32_t)(app->now-app->motion_last)>=repeat)) {
            app->motion_active=true;
            app->motion_seen=true;app->motion_last=app->now;
            const char *event=app->kind==LABWATCH?"instrument_motion":app->kind==TOOLSAFE?"body_motion":app->kind==SHELFWATCH?"rack_motion":app->mode==1?"authorized_motion":"model_motion";
            app_emit(app,"IMU",event,mechanical_level(app),app->kind==LABWATCH?deviation:rotation);
        }
    } else if(rotation<app->motion_limit*2/3 || app->kind==LABWATCH) {
        app->motion_candidate=false;app->motion_active=false;
    }
    if(app->impact_active||app->motion_candidate||app->imu_fault)app->quiet_since=app->now;
}
void events_light(App *app) {
    if(app->calibrating)return;
    int32_t delta=abs(app->light_mean-app->light_base);
    bool above;
    uint32_t stable;
    int32_t high=app->light_limit,low=app->light_limit*3/5;
    if(app->kind==TOOLSAFE){high=614;low=409;}
    if(app->kind==EXPOGUARD) {
        above=app->light_active?app->light_mean>2457:app->light_mean>=2867;stable=0;
    } else {above=app->light_active?delta>low:delta>=high;stable=app->kind==LABWATCH?200:app->kind==SHELFWATCH?100:0;}
    if(above) {
        if(!app->light_candidate){app->light_candidate=true;app->light_since=app->now;}
        if(!app->light_active&&(uint32_t)(app->now-app->light_since)>=stable) {
            app->light_active=true;
            const char *level="INFO";
            if(app->kind==LABWATCH&&app->mode!=2)level="ALARM";
            if(app->kind==TOOLSAFE&&app->mode==0)level="ALARM";
            app_emit(app,"ADC",app->kind==EXPOGUARD?"dark":"light_change",level,app->light_mean);
        }
    } else {
        if(app->light_active)app_emit(app,"ADC",app->kind==EXPOGUARD?"light_restored":"light_normal","INFO",app->light_mean);
        app->light_active=false;app->light_candidate=false;
    }
    if(app->light_active && (app->kind==LABWATCH || app->kind==TOOLSAFE))app->quiet_since=app->now;
}
void events_pir(App *app) {
    static const char *const names[]={"operator_motion","approach","motion","visitor_motion"};
    app_emit(app,"GPIO",names[app->kind],app->kind==SHELFWATCH&&app->mode==2?"ALARM":"INFO",1);
}
