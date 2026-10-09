#include "app.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {unsigned count,alarms;char last[40];} Log;
static void sink(void *context,uint32_t seq,uint32_t time,const char *src,const char *event,const char *level,int32_t value) {
    (void)seq;(void)time;(void)src;(void)value;Log *log=context;
    ++log->count;if(!strcmp(level,"ALARM"))++log->alarms;strcpy(log->last,event);
}
static void init(App *app,Log *log,ProjectKind kind,unsigned mode) {
    memset(log,0,sizeof(*log));app_init(app,kind,sink,log);app->calibrating=false;app->mode=mode;
    app->base_valid[0]=app->base_valid[1]=true;app->base[0][2]=app->base[1][2]=1000;
    for(unsigned i=0;i<5;i++)app_light(app,1000,i*20);
    memset(log,0,sizeof(*log));
}
int main(void) {
    for(ProjectKind kind=LABWATCH;kind<=EXPOGUARD;kind++) {
        App app;Log log;init(&app,&log,kind,0);
        app_pir(&app,true,100);app_pir(&app,true,101);assert(log.count==1&&log.alarms==0);
        app_pir(&app,false,102);app_pir(&app,true,103);assert(log.count==2);
    }
    App app;Log log;ImuSample shock={{1000,0,1000},{0,0,0},25000};
    init(&app,&log,SHELFWATCH,1);app_imu(&app,&shock,100);assert(log.alarms==1&&!strcmp(log.last,"impact"));
    app_imu(&app,&shock,110);assert(log.count==1);
    init(&app,&log,TOOLSAFE,1);shock.accel_mg[2]=2000;app_imu(&app,&shock,100);assert(log.count==1&&log.alarms==0);
    init(&app,&log,EXPOGUARD,2);shock.gyro_mdps[0]=40000;
    app_imu(&app,&shock,100);assert(log.count==0);app_imu(&app,&shock,120);assert(log.alarms==1);
    init(&app,&log,LABWATCH,2);shock=(ImuSample){{300,0,954},{0,0,0},25000};
    app_imu(&app,&shock,100);app_imu(&app,&shock,599);assert(log.count==0);
    app_imu(&app,&shock,600);assert(log.count==1&&log.alarms==0);
    init(&app,&log,SHELFWATCH,2);app_pir(&app,true,100);assert(log.alarms==1);
    for(unsigned i=0;i<15;i++)app_light(&app,2000,200+i*50);
    assert(!strcmp(log.last,"light_change")&&log.alarms==1);
    puts("Events: profile modes, PIR edges, independent alarms, hysteresis and sustained motion passed");
}
