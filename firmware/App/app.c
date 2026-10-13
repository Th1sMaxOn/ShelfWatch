#include "app.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

void app_emit(App *app, const char *source, const char *event, const char *level, int32_t value) {
    ++app->sequence;
    if (strcmp(level,"ALARM")==0) app->alarm=true;
    if (app->sink) app->sink(app->sink_context,app->sequence,app->now,source,event,level,value);
}
const char *app_mode_name(const App *app) {
    static const char *const names[4][3]={{"idle","work","service"},{"guard","issue","unused"},{"disarmed","inspection","armed"},{"expo","move","guard"}};
    return names[app->kind][app->mode];
}
void app_start_calibration(App *app) {
    app->calibrating=true;app->cal_count=0;app->cal_gyro_max=0;
    for(unsigned axis=0;axis<3;++axis) {
        app->cal_sum[axis]=0;app->cal_min[axis]=INT32_MAX;app->cal_max[axis]=INT32_MIN;
    }
    app_emit(app,"CAL","calibrating","INFO",0);
}
void app_init(App *app,ProjectKind kind,EventSink sink,void *context) {
    memset(app,0,sizeof(*app));app->kind=kind;app->sink=sink;app->sink_context=context;
    app->impact_limit=kind==SHELFWATCH?600:800;app->motion_limit=30000;app->light_limit=500;
    if(kind==TOOLSAFE || kind==SHELFWATCH) app_start_calibration(app);
}
void app_fault(App *app,bool imu,bool fault,uint32_t now) {
    app->now=now;bool *state=imu?&app->imu_fault:&app->adc_fault;
    if(*state==fault)return;
    *state=fault;
    app_emit(app,imu?"IMU":"ADC",fault?"sensor_error":"sensor_recovered",fault?"ERROR":"INFO",0);
    if(imu && fault) {
        app->base_valid[0]=false;app->base_valid[1]=false;
        app->missing_notices=0;
        app->impact_active=false;app->motion_active=false;app->motion_candidate=false;
    }
    if(!imu && fault) {
        app->light_ready=false;app->light_count=0;app->light_position=0;app->light_sum=0;
        app->light_candidate=false;
    }
    if(fault && app->calibrating) {
        app->calibrating=false;app_emit(app,"CAL","calibration_interrupted","ERROR",0);
    }
    if(imu && !fault && (app->kind==TOOLSAFE || app->kind==SHELFWATCH)) app_start_calibration(app);
}
void app_imu(App *app,const ImuSample *sample,uint32_t now) {
    app->now=now;app->latest=*sample;
    if(app->calibrating) {
        float speed_squared=0;
        for(unsigned axis=0;axis<3;++axis) {
            int32_t value=sample->accel_mg[axis];app->cal_sum[axis]+=value;
            if(value<app->cal_min[axis])app->cal_min[axis]=value;
            if(value>app->cal_max[axis])app->cal_max[axis]=value;
            float rotation=(float)sample->gyro_mdps[axis];
            speed_squared+=rotation*rotation;
        }
        int32_t speed=(int32_t)sqrtf(speed_squared);
        if(speed>app->cal_gyro_max)app->cal_gyro_max=speed;
        unsigned required=app->kind==LABWATCH?500:100;
        if(++app->cal_count<required)return;
        int32_t spread=0;
        for(unsigned axis=0;axis<3;++axis)if(app->cal_max[axis]-app->cal_min[axis]>spread)spread=app->cal_max[axis]-app->cal_min[axis];
        int32_t allowed=app->kind==LABWATCH && app->mode==1?500:50;
        app->calibrating=false;
        if(spread>allowed || app->cal_gyro_max>3000 || !app->light_ready || app->adc_fault) {
            app_emit(app,"CAL","calibration_rejected","ERROR",spread);return;
        }
        unsigned slot=app->kind==LABWATCH && app->mode==1?1:0;
        for(unsigned axis=0;axis<3;++axis)app->base[slot][axis]=app->cal_sum[axis]/(int32_t)app->cal_count;
        app->noise[slot]=spread;app->base_valid[slot]=true;app->light_base=app->light_mean;app->light_base_valid=true;
        app->impact_active=false;app->motion_active=false;app->motion_candidate=false;
        app->light_active=false;app->light_candidate=false;app->quiet_since=now;
        app_emit(app,"CAL","calibration_ready","INFO",spread);return;
    }
    events_imu(app);
}
void app_light(App *app,uint16_t value,uint32_t now) {
    app->now=now;
    if(app->light_count==5)app->light_sum-=app->light_window[app->light_position];
    else ++app->light_count;
    app->light_window[app->light_position]=value;app->light_position=(app->light_position+1)%5;
    app->light_sum+=value;app->light_mean=app->light_sum/(int32_t)app->light_count;
    if(app->light_count<5)return;
    if(!app->light_ready) {
        app->light_ready=true;
        if(!app->light_base_valid){app->light_base=app->light_mean;app->light_base_valid=true;}
    }
    events_light(app);
}
void app_pir(App *app,bool value,uint32_t now) {
    app->now=now;if(value&&!app->pir)events_pir(app);app->pir=value;
}
