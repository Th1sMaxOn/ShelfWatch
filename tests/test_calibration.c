#include "app.h"
#include <assert.h>
#include <stdio.h>

static void light(App *app) { for(unsigned i=0;i<5;i++)app_light(app,1000,i*20); }
static void samples(App *app,unsigned count,int32_t x) {
    ImuSample sample={{x,0,1000},{0,0,0},25000};
    for(unsigned i=0;i<count;i++)app_imu(app,&sample,(i+1)*20);
}
int main(void) {
    for(ProjectKind kind=LABWATCH;kind<=EXPOGUARD;kind++) {
        App app;app_init(&app,kind,NULL,NULL);light(&app);app_start_calibration(&app);
        unsigned count=kind==LABWATCH?500:100;
        samples(&app,count-1,0);assert(app.calibrating&&!app.base_valid[0]);
        samples(&app,1,0);assert(!app.calibrating&&app.base_valid[0]);assert(app.base[0][2]==1000);
        app_start_calibration(&app);samples(&app,1,1000);samples(&app,count-1,0);
        assert(!app.calibrating&&app.base_valid[0]&&app.base[0][0]==0);
        app_start_calibration(&app);app_fault(&app,true,true,100);assert(!app.calibrating&&!app.base_valid[0]);
        unsigned sequence=app.sequence;app_fault(&app,true,true,101);assert(app.sequence==sequence);
        app_fault(&app,true,false,102);
        assert(app.calibrating==(kind==TOOLSAFE||kind==SHELFWATCH));
        app_fault(&app,false,true,103);assert(!app.light_ready&&app.light_count==0);
        app_fault(&app,false,false,104);light(&app);assert(app.light_ready);
    }
    App app;app_init(&app,LABWATCH,NULL,NULL);light(&app);app.mode=1;app_start_calibration(&app);samples(&app,500,100);
    assert(app.base_valid[1]&&!app.base_valid[0]);
    puts("Calibration: exact sample count, noisy rejection, mode baselines and fault interruption passed");
    return 0;
}
