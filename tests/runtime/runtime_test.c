/* Exercise the actual main/UI code with controlled RTOS interleavings and
 * callback reentrancy. Codec correctness is tested separately in tests/decoder. */
#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static size_t test_strlcpy(char *dst,const char *src,size_t n){
    size_t len=strlen(src);if(n){size_t take=len<n-1?len:n-1;memcpy(dst,src,take);dst[take]=0;}return len;
}
#define strlcpy test_strlcpy
#include "../../firmware/main/main.c"
#include "../../firmware/main/native_ui.c"

int stub_alloc_fail,stub_exit_on_idle,stub_exit_after_delay,stub_notifications;
int64_t stub_time_us;
jmp_buf stub_task_exit;
static int pairing_calls,wifi_reset_calls,reset_calls,push_calls,reset_result,push_result;
static int panel_result,panel_calls;
static bool ble_ready=true;
static const board_info_t test_board={720,1280,"Host test"};

esp_err_t board_init(void){return ESP_OK;}
const board_info_t *board_info(void){return &test_board;}
int board_read_touch(board_touch_point_t *p,size_t cap){(void)p;(void)cap;return 0;}
esp_err_t board_present_rgb565(const uint16_t *p,uint16_t w,uint16_t h,size_t stride){
    assert(p&&w==width&&h==height&&stride==(size_t)width*2);panel_calls++;return panel_result;
}
esp_err_t board_set_backlight(uint8_t value){(void)value;return ESP_OK;}
bool input_ble_send(uint8_t id,const uint8_t *data,size_t len,void *ctx){
    (void)id;(void)data;(void)len;(void)ctx;return ble_ready;
}
bool input_ble_is_ready(void){return ble_ready;}
uint8_t input_ble_keyboard_leds(void){return 0;}
esp_err_t input_ble_start(const char *name,input_ble_state_cb cb,void *ctx){(void)name;(void)cb;(void)ctx;return ESP_OK;}
void input_ble_pairing_window(void){
    assert(!lock->locked);pairing_calls++;
    /* This is the synchronous callback that previously deadlocked the UI. */
    native_ui_ble(INPUT_BLE_PAIRING,"Pairing",NULL);
}
void app_network_forget_wifi(void){assert(!lock->locked);wifi_reset_calls++;}
esp_err_t app_network_start(const char *name,network_state_cb cb,void *ctx){(void)name;(void)cb;(void)ctx;return ESP_OK;}
const char *app_network_device_name(void){return "Host test";}
bool app_network_ready(void){return false;}
esp_err_t airplay_receiver_start(const airplay_receiver_config_t *c){(void)c;return ESP_OK;}
esp_err_t airplay_receiver_stop(void){return ESP_OK;}
airplay_receiver_state_t airplay_receiver_get_state(void){return AIRPLAY_RECEIVER_STOPPED;}
const char *airplay_receiver_state_name(airplay_receiver_state_t s){
    static const char *const names[]={"stopped","advertising","connecting","waiting-video","streaming","no-video","error"};
    return (unsigned)s<sizeof names/sizeof names[0]?names[s]:"unknown";
}
int video_decoder_init(const video_decoder_config_t *c,video_decoder_t **out){(void)c;*out=(void *)1;return 0;}
int video_decoder_push(video_decoder_t *d,const uint8_t *data,size_t n){assert(d&&data&&n);push_calls++;return push_result;}
int video_decoder_flush(video_decoder_t *d){(void)d;return 0;}
int video_decoder_reset(video_decoder_t *d){assert(d);reset_calls++;return reset_result;}
void video_decoder_destroy(video_decoder_t *d){(void)d;}
const char *video_decoder_error_string(int rc){(void)rc;return "test error";}

static ui_action_t sample_touch(int x,int y,int count,uint32_t now){
    board_touch_point_t point={(uint16_t)x,(uint16_t)y,1,0};
    xSemaphoreTake(lock,portMAX_DELAY);
    ui_action_t action=touch(&point,count,now);
    xSemaphoreGive(lock);
    return action;
}

static void ui_regressions(void){
    assert(native_ui_start()==ESP_OK);
    dirty=false;
    for(int i=0;i<100;i++)assert(sample_touch(0,0,0,(uint32_t)i)==UI_ACTION_NONE);
    assert(!dirty);
    native_ui_queue_drop(0);assert(!dirty);
    native_ui_queue_drop(5);native_ui_queue_drop(4);assert(drops==5);
    drops=0;

    ui_action_t action=sample_touch(width*3/8,toolbar_y()+10,1,1000);
    assert(action==UI_ACTION_PAIR&&pairing_calls==0);
    execute_action(action);assert(pairing_calls==1);
    sample_touch(0,0,0,1010);

    info=true;
    action=sample_touch(width-100,toolbar_y()-40,1,2000);
    assert(action==UI_ACTION_NONE&&reset_armed);
    action=sample_touch(width-100,toolbar_y()-40,1,3999);
    assert(action==UI_ACTION_NONE&&wifi_reset_calls==0);
    action=sample_touch(width-100,toolbar_y()-40,1,4000);
    assert(action==UI_ACTION_RESET_WIFI&&wifi_reset_calls==0);
    execute_action(action);assert(wifi_reset_calls==1);
    sample_touch(0,0,0,4010);
    sample_touch(width-100,toolbar_y()-40,1,5000);
    sample_touch(5,100,1,5100);assert(!reset_armed);
    assert(sample_touch(width-100,toolbar_y()-40,1,8000)==UI_ACTION_NONE);
    sample_touch(0,0,0,8010);

    info=false;have_video=true;input.map=(input_mapping_t){.x=100,.y=100,.w=500,.h=500,.xmax=32767,.ymax=32767};
    sample_touch(20,200,1,9000);assert(blocked&&!input.active);
    sample_touch(0,0,0,9010);
    sample_touch(200,200,1,9020);assert(input.active);
    sample_touch(20,200,1,9030);assert(blocked&&!input.active);
    sample_touch(0,0,0,9040);
    puts("UI: idle stays clean; synchronous pairing callback is unlocked; continuous reset hold; black bars ignored");
}

static void feed(bool key,size_t len){
    uint8_t bytes[80]={0};assert(len<=sizeof bytes);
    airplay_receiver_frame_t frame={.data=bytes,.len=len,.keyframe=key};received_frame(&frame,NULL);
}

static void run_decoder_until_idle(void){
    stub_exit_on_idle=1;
    if(setjmp(stub_task_exit)==0)decode_task(NULL);
    stub_exit_on_idle=0;
}

static void pipeline_regressions(void){
    pipeline_lock=xSemaphoreCreateMutex();frame_queue=xQueueCreate(12,sizeof(access_unit_t));
    decode_task_handle=(void *)1;decoder=(void *)1;
    feed(false,24);assert(queued_bytes==0&&needs_idr);
    feed(true,24);feed(false,24);assert(queued_bytes==48);
    access_unit_t old;assert(take_access_unit(&old));
    assert(inflight_bytes==24&&queued_bytes==24);
    feed(false,24);assert(queued_bytes==0&&inflight_bytes==24&&needs_idr);
    assert(old.epoch!=atomic_load(&epoch));
    feed(true,24);assert(queued_bytes==24&&!needs_idr);
    const unsigned fresh=atomic_load(&epoch);
    decoder_failed(old.epoch,VIDEO_DECODER_ERR_CODEC);
    assert(atomic_load(&epoch)==fresh&&queued_bytes==24);
    uint64_t before=decoded_frames;
    video_decoder_frame_t frame={.rgb565=canvas,.width=width,.height=height,.stride_bytes=(size_t)width*2,
        .view_rect={0,0,width,height},.source_width=640,.source_height=360};
    decode_context_t context={old.epoch};decoded_frame(&context,&frame);assert(decoded_frames==before);
    context.current_epoch=fresh;decoded_frame(&context,&frame);assert(decoded_frames==before+1);
    receiver_state(AIRPLAY_RECEIVER_WAITING_VIDEO,"Packet lost",NULL);
    assert(queued_bytes==0&&needs_idr&&!have_video);
    retire_access_unit(&old);assert(inflight_bytes==0);
    feed(false,24);assert(queued_bytes==0);

    reset_result=VIDEO_DECODER_ERR_MEMORY;feed(true,24);
    run_decoder_until_idle();assert(reset_calls==1&&push_calls==0);
    assert(needs_idr&&queued_bytes==0&&inflight_bytes==0&&decoder_error==VIDEO_DECODER_ERR_MEMORY);
    reset_result=0;feed(true,24);run_decoder_until_idle();assert(reset_calls==2&&push_calls==1);
    assert(queued_bytes==0&&inflight_bytes==0);
    stub_alloc_fail=1;feed(true,24);stub_alloc_fail=0;assert(needs_idr&&queued_bytes==0);
    feed(true,65);assert(queued_bytes==0&&needs_idr);
    puts("Pipeline: full budget includes in-flight AU; IDR recovery; old epoch cannot publish or clear new queue; reset failure handled");
}

static void presentation_regressions(void){
    info=false;have_video=true;decoded_frames=10;last_presented_frame=0;presented_frames=0;
    dirty=true;stub_time_us=100000;stub_exit_after_delay=1;
    if(setjmp(stub_task_exit)==0)ui_task(NULL);
    assert(panel_calls==1&&presented_frames==1&&last_presented_frame==10);
    dirty=true;stub_time_us+=100000;
    if(setjmp(stub_task_exit)==0)ui_task(NULL);
    assert(panel_calls==2&&presented_frames==1);
    decoded_frames=11;dirty=true;panel_result=ESP_ERR_INVALID_SIZE;stub_time_us+=100000;
    if(setjmp(stub_task_exit)==0)ui_task(NULL);
    assert(panel_calls==3&&presented_frames==1&&last_presented_frame==10);
    stub_exit_after_delay=0;
    puts("Presentation: SHOWN counts new frames only after board API success, excludes redraws and errors");
}

static void save_ppm(const char *directory,const char *board,const char *page){
    char path[1024];
    int len=snprintf(path,sizeof path,"%s/%s-%s.ppm",directory,board,page);
    assert(len>0&&(size_t)len<sizeof path);
    FILE *file=fopen(path,"wb");if(!file){perror(path);exit(1);}
    fprintf(file,"P6\n# Actual firmware UI drawn on the host, not a device photograph.\n%u %u\n255\n",width,height);
    for(size_t i=0;i<(size_t)width*height;i++){
        uint16_t pixel=canvas[i];
        uint8_t rgb[]={((pixel>>11)&31)*255/31,((pixel>>5)&63)*255/63,(pixel&31)*255/31};
        assert(fwrite(rgb,1,3,file)==3);
    }
    assert(fclose(file)==0);printf("Host UI render: %s\n",path);
}

static void diagnostic_video(void){
    for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++)
        video[(size_t)y*width+x]=((x/60+y/60)&1)?RGB(24,51,70):RGB(19,38,53);
    uint16_t *saved=canvas;canvas=video;
    text(36,96,"HOST RENDER",4,GREEN);
    text(36,144,"DIAGNOSTIC BACKGROUND",2,FG);
    text(36,174,"NO MAC VIDEO OR DEVICE CAPTURE",2,MUTED);
    canvas=saved;
}

static void render_pages(const char *directory){
    if(mkdir(directory,0777)!=0&&errno!=EEXIST){perror(directory);exit(1);}
    const struct {const char *name;uint16_t h;} boards[]={
        {"metalio_claw4",720},{"m5stack_tab5",1280}
    };
    for(size_t b=0;b<sizeof boards/sizeof boards[0];b++){
        width=720;height=boards[b].h;layout_keyboard();
        info=false;keyboard=false;have_video=false;pressed_key=-1;modifiers=0;
        reset_armed=false;reset_fired=false;was_down=false;blocked=false;
        decoded_frames=0;presented_frames=0;last_presented_frame=0;drops=0;decoder_error=0;
        ble_ready=false;
        native_ui_network(NETWORK_SETUP,"P4-Display-7A3C","19AF07C3AB12",NULL);
        native_ui_airplay(AIRPLAY_RECEIVER_ADVERTISING,"Host render: no Mac connected",NULL);
        native_ui_ble(INPUT_BLE_OFF,"Host render: input not paired",NULL);
        draw();save_ppm(directory,boards[b].name,"home");
        diagnostic_video();have_video=true;keyboard=true;
        draw();save_ppm(directory,boards[b].name,"keyboard");
        keyboard=false;info=true;have_video=false;
        native_ui_network(NETWORK_READY,"Host rendering only","192.0.2.10",NULL);
        draw();save_ppm(directory,boards[b].name,"info");
        /* Worst vertical case: setup credentials plus an error and help rows. */
        native_ui_network(NETWORK_SETUP,"P4-Display-7A3C","19AF07C3AB12",NULL);
        decoder_error=VIDEO_DECODER_ERR_LIMIT;
        draw();save_ppm(directory,boards[b].name,"info-setup-error");
    }
}

int main(int argc,char **argv){
    const char *render_directory=NULL;
    if(argc==3&&strcmp(argv[1],"--render")==0)render_directory=argv[2];
    else if(argc!=1){fprintf(stderr,"usage: %s [--render DIRECTORY]\n",argv[0]);return 2;}
    ui_regressions();pipeline_regressions();presentation_regressions();
    if(render_directory)render_pages(render_directory);
    reset_pipeline();vQueueDelete(frame_queue);vSemaphoreDelete(pipeline_lock);
    free(video);free(canvas);vSemaphoreDelete(lock);
    return 0;
}
