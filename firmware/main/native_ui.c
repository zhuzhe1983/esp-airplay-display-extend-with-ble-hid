/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "native_ui.h"
#include "display_board.h"
#include "input_core.h"
#include "ui_font.h"
#include "sdkconfig.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define RGB(r,g,b) (uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3))
#define BG RGB(16,22,31)
#define FG RGB(234,241,249)
#define MUTED RGB(145,163,188)
#define KEY RGB(38,51,69)
#define GREEN RGB(135,244,195)
#define AMBER RGB(255,197,107)
typedef struct {int x,y,w,h;char label[7];uint8_t usage,modifier;bool special;} ui_key_t;
static SemaphoreHandle_t lock;
static uint16_t *video,*canvas;
static uint16_t width,height;
static bool have_video,dirty=true,keyboard,info,was_down,blocked,reset_fired,reset_armed,touch_fault;
static bool controls=true,recovering,edge_capture,edge_done,edge_show,edge_top,edge_tap;
static int edge_x,edge_y;
typedef enum { UI_ACTION_NONE=0, UI_ACTION_PAIR=1, UI_ACTION_RESET_WIFI=2 } ui_action_t;
static char net_a[64]="Starting",net_b[64]="",air_detail[96]="Waiting for Wi-Fi",ble_detail[96]="Starting";
static network_state_t net_state=NETWORK_STARTING;
static airplay_receiver_state_t air_state=AIRPLAY_RECEIVER_STOPPED;
static input_ble_state_t ble_state=INPUT_BLE_OFF;
static input_engine_t input;
static uint8_t modifiers;
static ui_key_t keys[80];static int key_count,pressed_key=-1;
static uint32_t pressed_at,last_repeat,reset_started,drops;
static uint64_t decoded_frames,presented_frames,last_presented_frame;
static int decoder_error;
static uint16_t source_width,source_height;
static int toolbar_y(void){return height-58;}
static int keyboard_y(void){return toolbar_y()-6*52-8;}
static bool controls_shown(void){return !have_video||controls||keyboard||info;}
static int controls_handle_y(void){return (keyboard?keyboard_y():toolbar_y())-18;}
static void rect(int x,int y,int w,int h,uint16_t color){
    if(x<0){w+=x;x=0;}if(y<0){h+=y;y=0;}
    if(x+w>width)w=width-x;
    if(y+h>height)h=height-y;
    for(int j=0;j<h;j++)for(int i=0;i<w;i++)canvas[(size_t)(y+j)*width+x+i]=color;
}
static void text(int x,int y,const char *s,int scale,uint16_t color){
    for(;*s;s++,x+=6*scale){if(x+5*scale>width)break;
        for(int row=0;row<7;row++){uint8_t bits=glyph_row(*s,row);for(int col=0;col<5;col++)if(bits&(16>>col))rect(x+col*scale,y+row*scale,scale,scale,color);}
    }
}
static void button(int x,int y,int w,int h,const char *label,bool active){
    rect(x,y,w,h,active?GREEN:KEY);
    int cells=(int)strlen(label)*6-1,scale=3;
    while(scale>1&&cells*scale>w-6)scale--;
    text(x+(w-cells*scale)/2,y+(h-7*scale)/2,label,scale,active?BG:FG);
}
static void add_key(int row,int col,int units,int total,const char *label,uint8_t usage,uint8_t mod,bool special){
    if(key_count>=80)return;
    ui_key_t *k=&keys[key_count++];k->x=4+col*(width-8)/total;k->y=keyboard_y()+row*52+4;
    k->w=units*(width-8)/total-4;k->h=47;strlcpy(k->label,label,sizeof k->label);k->usage=usage;k->modifier=mod;k->special=special;
}
static void layout_keyboard(void){
    key_count=0;
    add_key(0,0,1,13,"ESC",0x29,0,false);
    const char *digits="1234567890";
    for(int i=0;i<10;i++){char s[]={digits[i],0};add_key(0,i+1,1,13,s,i==9?0x27:0x1e + i,0,false);}
    add_key(0,11,2,13,"DELETE",0x2A,0,false);
    const char *row1="qwertyuiop";
    add_key(1,0,1,13,"TAB",0x2B,0,false);
    for(int i=0;i<10;i++){char s[]={row1[i],0};add_key(1,i+1,1,13,s,4+row1[i]-'a',0,false);}
    add_key(1,11,1,13,"[",0x2F,0,false);add_key(1,12,1,13,"]",0x30,0,false);
    const char *row2="asdfghjkl";
    for(int i=0;i<9;i++){char s[]={row2[i],0};add_key(2,i,1,12,s,4+row2[i]-'a',0,false);}
    add_key(2,9,1,12,";",0x33,0,false);add_key(2,10,2,12,"ENTER",0x28,0,false);
    const char *row3="zxcvbnm";
    add_key(3,0,2,13,"SHIFT",0,MOD_SHIFT,true);
    for(int i=0;i<7;i++){char s[]={row3[i],0};add_key(3,i+2,1,13,s,4+row3[i]-'a',0,false);}
    add_key(3,9,1,13,",",0x36,0,false);add_key(3,10,1,13,".",0x37,0,false);add_key(3,11,1,13,"/",0x38,0,false);add_key(3,12,1,13,"UP",0x52,0,false);
    add_key(4,0,2,15,"CTRL",0,MOD_CTRL,true);add_key(4,2,2,15,"OPT",0,MOD_ALT,true);
    add_key(4,4,2,15,"CMD",0,MOD_GUI,true);add_key(4,6,5,15,"SPACE",0x2C,0,false);
    add_key(4,11,1,15,"LT",0x50,0,false);add_key(4,12,1,15,"DN",0x51,0,false);add_key(4,13,2,15,"RT",0x4F,0,false);
    add_key(5,0,2,12,"IME",0x2C,MOD_CTRL,false);
    add_key(5,2,1,12,"-",0x2D,0,false);add_key(5,3,1,12,"=",0x2E,0,false);
    add_key(5,4,1,12,"'",0x34,0,false);add_key(5,5,1,12,"\\",0x31,0,false);
    add_key(5,6,1,12,"`",0x35,0,false);
    add_key(5,7,2,12,"COPY",0x06,MOD_GUI,false);add_key(5,9,3,12,"PASTE",0x19,MOD_GUI,false);
}
static void draw(void){
    if(have_video&&!info)memcpy(canvas,video,(size_t)width*height*2);else rect(0,0,width,height,BG);
    if(!controls_shown())return;
    rect(0,0,width,52,BG);text(16,15,"P4 NATIVE DISPLAY",3,FG);
    text(width-174,18,input_ble_is_ready()?"INPUT READY":"INPUT OFF",2,input_ble_is_ready()?GREEN:AMBER);
    if(!have_video||info){
        bool compact=height<900;
        int y=compact?76:94;text(24,y,"MAC SYSTEM SCREEN MIRRORING",3,GREEN);y+=compact?44:58;
        text(24,y,"WI-FI",2,MUTED);y+=27;text(24,y,net_a,3,FG);y+=34;
        if(net_state==NETWORK_SETUP){text(24,y,"PASSWORD",2,MUTED);y+=23;text(24,y,net_b,3,AMBER);y+=34;text(24,y,"OPEN HTTP://192.168.4.1",3,GREEN);y+=compact?37:45;}
        else{text(24,y,net_b,2,MUTED);y+=compact?34:42;}
        text(24,y,"AIRPLAY",2,MUTED);y+=27;text(24,y,airplay_receiver_state_name(air_state),3,FG);y+=30;
        text(24,y,air_detail,2,MUTED);y+=compact?34:42;
        text(24,y,"BLUETOOTH KEYBOARD + MOUSE",2,MUTED);y+=27;text(24,y,ble_detail,2,ble_state==INPUT_BLE_READY?GREEN:AMBER);y+=compact?35:43;
        char stats[96];snprintf(stats,sizeof stats,"DECODED %llu  SHOWN %llu  DROP %lu",(unsigned long long)decoded_frames,(unsigned long long)presented_frames,(unsigned long)drops);text(24,y,stats,2,MUTED);y+=30;
        if(decoder_error){snprintf(stats,sizeof stats,"DECODER ERROR %d",decoder_error);text(24,y,stats,2,AMBER);y+=30;}
        if(info){
            text(24,y,"TAP: CLICK   HOLD: DRAG",2,FG);y+=27;
            text(24,y,"TWO FINGERS: SCROLL / RIGHT CLICK",2,FG);y+=27;
            text(24,y,"IME: CTRL+SPACE (MAC INPUT SOURCE)",2,FG);y+=27;
            text(24,y,"EXPERIMENTAL AIRPLAY + STANDARD HID",2,AMBER);
            button(width-236,toolbar_y()-65,220,50,"WIFI RESET",reset_armed);
            text(20,toolbar_y()-48,"HOLD RESET FOR 2 SECONDS",2,MUTED);
        }
    }
    if(keyboard&&!info){
        rect(0,keyboard_y(),width,toolbar_y()-keyboard_y(),BG);
        for(int i=0;i<key_count;i++)button(keys[i].x,keys[i].y,keys[i].w,keys[i].h,keys[i].label,i==pressed_key||(keys[i].special&&(modifiers&keys[i].modifier)));
    }
    const char *labels[]={keyboard?"HIDE KEYS":"KEYBOARD","PAIR","INFO",input.absolute?"ABS TEST":"TOUCHPAD"};
    if(have_video)rect(width/2-40,controls_handle_y()+7,80,4,MUTED);
    rect(0,toolbar_y(),width,58,BG);
    for(int i=0;i<4;i++)button(4+i*(width-8)/4,toolbar_y()+5,(width-8)/4-4,48,labels[i],(i==0&&keyboard)||(i==2&&info));
    if(have_video&&recovering&&!info){
        rect(0,52,width,36,BG);
        text(16,62,"WAITING FOR VIDEO - LAST FRAME",2,AMBER);
    }
}
static void release_key(void){
    if(pressed_key<0)return;
    if(pressed_key>=0&&!keys[pressed_key].special){input_key(&input,0,keys[pressed_key].usage,false);modifiers=0;}
    pressed_key=-1;dirty=true;
}
static bool reset_hit(int x,int y){
    return x>=width-236&&x<width-16&&y>=toolbar_y()-65&&y<toolbar_y()-15;
}
static bool video_hit(int x,int y){
    if(!have_video||recovering||info||x<input.map.x||x>=input.map.x+input.map.w||
       y<input.map.y||y>=input.map.y+input.map.h)return false;
    return !controls_shown()||(y>=52&&y<toolbar_y()&&(!keyboard||y<keyboard_y()));
}
/* Actions that may synchronously publish state (or reboot) leave the UI mutex
 * before they execute. In particular pairing -> native_ui_ble is reentrant. */
static ui_action_t touch(const board_touch_point_t *points,int count,uint32_t now){
    if(count<0||count>2||(count&&!points))return UI_ACTION_NONE;
    bool down=count>0;
    int x=down?points[0].x:0,y=down?points[0].y:0;
    if(!down){
        if(edge_capture&&edge_tap&&!edge_done){
            input_point_t point={(uint16_t)edge_x,(uint16_t)edge_y};
            input_engine_update(&input,&point,1,pressed_at);
        }
        edge_capture=edge_done=edge_tap=false;
        release_key();input_engine_update(&input,NULL,0,now);was_down=false;blocked=false;
        if(reset_armed)dirty=true;
        reset_armed=false;reset_fired=false;return UI_ACTION_NONE;
    }
    if(!was_down){
        pressed_at=last_repeat=now;was_down=true;
        bool shown=controls_shown();
        bool top=shown?y<52:y<24;
        bool bottom=shown?(x>=width/2-90&&x<width/2+90&&y>=controls_handle_y()&&y<controls_handle_y()+23):y>=height-24;
        if(have_video&&count==1&&(top||bottom)){
            edge_capture=true;edge_done=false;edge_show=!shown;edge_top=top;
            edge_tap=!shown&&video_hit(x,y);edge_x=x;edge_y=y;
            blocked=true;input_engine_cancel(&input);release_key();return UI_ACTION_NONE;
        }
        if(y>=toolbar_y()){
            blocked=true;input_engine_cancel(&input);release_key();
            if(count!=1)return UI_ACTION_NONE;
            int item=x*4/width;
            if(item==0){keyboard=!keyboard;info=false;modifiers=0;}
            if(item==1){dirty=true;return UI_ACTION_PAIR;}
            if(item==2){info=!info;keyboard=false;}
            if(item==3){
#if CONFIG_P4_INPUT_ABSOLUTE
                input.absolute=!input.absolute;
#endif
            }
            dirty=true;return UI_ACTION_NONE;
        }
        if(info){
            blocked=true;
            if(count==1&&reset_hit(x,y)){reset_started=now;reset_armed=true;dirty=true;}
            return UI_ACTION_NONE;
        }
        if(keyboard&&y>=keyboard_y()){
            blocked=true;input_engine_cancel(&input);
            if(count!=1)return UI_ACTION_NONE;
            for(int i=0;i<key_count;i++){ui_key_t *k=&keys[i];if(x>=k->x&&x<k->x+k->w&&y>=k->y&&y<k->y+k->h){
                pressed_key=i;
                if(k->special)modifiers^=k->modifier;else input_key(&input,modifiers|k->modifier,k->usage,true);
                dirty=true;break;
            }}
            return UI_ACTION_NONE;
        }
        if(!video_hit(x,y)){blocked=true;return UI_ACTION_NONE;}
    }
    if(edge_capture){
        if(count!=1){edge_tap=false;edge_done=true;return UI_ACTION_NONE;}
        int dx=x-edge_x,dy=y-edge_y;
        bool inward=edge_top?dy>=40:dy<=-40;
        bool outward=edge_top?dy<=-32:dy>=32;
        if(!edge_done&&abs(dy)>abs(dx)*2&&(edge_show?inward:outward)){
            controls=edge_show;info=false;
            if(!controls){keyboard=false;modifiers=0;release_key();}
            edge_done=true;edge_tap=false;dirty=true;
        }else if(!edge_done&&edge_tap&&((abs(dx)>32&&abs(dx)>abs(dy)*2)||now-pressed_at>=350)){
            input_point_t start={(uint16_t)edge_x,(uint16_t)edge_y};
            edge_capture=false;blocked=false;input_engine_update(&input,&start,1,pressed_at);
        }
        if(edge_capture)return UI_ACTION_NONE;
    }
    if(reset_armed){
        if(count!=1||!info||!reset_hit(x,y)){reset_armed=false;dirty=true;}
        else if(!reset_fired&&now-reset_started>=2000){
            reset_fired=true;return UI_ACTION_RESET_WIFI;
        }
    }
    if(pressed_key>=0){
        ui_key_t *k=&keys[pressed_key];
        if(count!=1||x<k->x||x>=k->x+k->w||y<k->y||y>=k->y+k->h){release_key();return UI_ACTION_NONE;}
        if(!k->special&&(k->usage==0x2A||(k->usage>=0x4F&&k->usage<=0x52))&&now-pressed_at>=500&&now-last_repeat>=80){
            input_key(&input,0,k->usage,false);input_key(&input,modifiers|k->modifier,k->usage,true);last_repeat=now;
        }
    }
    if(!blocked){
        input_point_t p[2];
        for(int i=0;i<count;i++){
            if(!video_hit(points[i].x,points[i].y)){input_engine_cancel(&input);blocked=true;return UI_ACTION_NONE;}
            p[i]=(input_point_t){points[i].x,points[i].y};
        }
        input_engine_update(&input,p,count,now);
    }
    return UI_ACTION_NONE;
}
static void execute_action(ui_action_t action){
    if(action==UI_ACTION_PAIR)input_ble_pairing_window();
    else if(action==UI_ACTION_RESET_WIFI)app_network_forget_wifi();
}
static void ui_task(void *p){
    (void)p;uint32_t last_present=0;
    for(;;){
        board_touch_point_t points[2];int count=board_read_touch(points,2);
        uint32_t now=(uint32_t)(esp_timer_get_time()/1000);
        ui_action_t action=UI_ACTION_NONE;
        xSemaphoreTake(lock,portMAX_DELAY);
        if(count<0||count>2){
            if(!touch_fault){release_key();input_engine_cancel(&input);if(reset_armed)dirty=true;}
            touch_fault=true;was_down=false;blocked=true;reset_armed=false;reset_fired=false;edge_capture=false;edge_tap=false;
        }else if(touch_fault){
            /* An I2C failure must not become a fresh click while a finger is
             * still down; resume only after an observed all-fingers release. */
            if(count==0){touch_fault=false;blocked=false;was_down=false;}
        }else action=touch(points,count,now);
        bool update=dirty&&now-last_present>=33;
        uint64_t submitted_frame=update&&have_video&&!info?decoded_frames:0;
        if(update){draw();dirty=false;last_present=now;}
        xSemaphoreGive(lock);
        if(update){
            esp_err_t err=board_present_rgb565(canvas,width,height,width*2);
            if(err!=ESP_OK)ESP_LOGE("ui","Panel present: %s",esp_err_to_name(err));
            else if(submitted_frame){
                xSemaphoreTake(lock,portMAX_DELAY);
                if(submitted_frame>last_presented_frame){last_presented_frame=submitted_frame;presented_frames++;}
                xSemaphoreGive(lock);
            }
        }
        execute_action(action);
        vTaskDelay(pdMS_TO_TICKS(12));
    }
}
esp_err_t native_ui_start(void){
    if(lock||video||canvas)return ESP_ERR_INVALID_STATE;
    width=board_info()->width;height=board_info()->height;
    if(width<320||height<480||width>1920||height>1920||(size_t)width*height>1920U*1080U)return ESP_ERR_INVALID_SIZE;
    lock=xSemaphoreCreateMutex();if(!lock)return ESP_ERR_NO_MEM;
    video=heap_caps_calloc((size_t)width*height,2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    canvas=heap_caps_malloc((size_t)width*height*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!video||!canvas){free(video);free(canvas);video=canvas=NULL;vSemaphoreDelete(lock);lock=NULL;return ESP_ERR_NO_MEM;}
    input_engine_init(&input,input_ble_send,NULL);layout_keyboard();
    if(xTaskCreate(ui_task,"display_ui",6144,NULL,4,NULL)==pdPASS)return ESP_OK;
    free(video);free(canvas);video=canvas=NULL;vSemaphoreDelete(lock);lock=NULL;return ESP_ERR_NO_MEM;
}
void native_ui_network(network_state_t s,const char *a,const char *b,void *ctx){
    (void)ctx;if(!lock)return;xSemaphoreTake(lock,portMAX_DELAY);
    if(net_state!=s||strcmp(net_a,a?a:"")||strcmp(net_b,b?b:""))dirty=true;
    net_state=s;strlcpy(net_a,a?a:"",sizeof net_a);strlcpy(net_b,b?b:"",sizeof net_b);xSemaphoreGive(lock);
}
void native_ui_airplay(airplay_receiver_state_t s,const char *detail,void *ctx){
    (void)ctx;if(!lock)return;xSemaphoreTake(lock,portMAX_DELAY);air_state=s;strlcpy(air_detail,detail?detail:"",sizeof air_detail);
    if(s!=AIRPLAY_RECEIVER_STREAMING){
        recovering=have_video&&(s==AIRPLAY_RECEIVER_WAITING_VIDEO||s==AIRPLAY_RECEIVER_NO_VIDEO);
        if(!recovering)have_video=false;
        controls=true;keyboard=false;release_key();modifiers=0;input_engine_cancel(&input);blocked=was_down;edge_capture=false;edge_tap=false;
    }
    dirty=true;xSemaphoreGive(lock);
}
void native_ui_ble(input_ble_state_t s,const char *detail,void *ctx){
    (void)ctx;if(!lock)return;xSemaphoreTake(lock,portMAX_DELAY);ble_state=s;strlcpy(ble_detail,detail?detail:"",sizeof ble_detail);
    if(s!=INPUT_BLE_READY){modifiers=0;pressed_key=-1;input_engine_cancel(&input);}dirty=true;xSemaphoreGive(lock);
}
void native_ui_video(void *ctx,const video_decoder_frame_t *f){
    (void)ctx;if(!lock||!f||!f->rgb565||f->width!=width||f->height!=height||f->stride_bytes<(size_t)width*2||
        !f->view_rect.width||!f->view_rect.height||f->view_rect.x+f->view_rect.width>width||f->view_rect.y+f->view_rect.height>height)return;
    xSemaphoreTake(lock,portMAX_DELAY);
    if(!have_video||recovering){controls=false;keyboard=false;info=false;edge_capture=false;edge_tap=false;}
    recovering=false;
    for(int y=0;y<height;y++)memcpy(video+(size_t)y*width,(const uint8_t *)f->rgb565+y*f->stride_bytes,width*2);
    input.map=(input_mapping_t){.x=f->view_rect.x,.y=f->view_rect.y,.w=f->view_rect.width,.h=f->view_rect.height,.xmax=32767,.ymax=32767};
    source_width=f->source_width;source_height=f->source_height;
    have_video=true;decoded_frames++;decoder_error=0;dirty=true;xSemaphoreGive(lock);
}
void native_ui_decoder_error(int error){
    if(!lock)return;
    xSemaphoreTake(lock,portMAX_DELAY);decoder_error=error;have_video=false;controls=true;edge_capture=false;edge_tap=false;release_key();modifiers=0;
    input_engine_cancel(&input);blocked=was_down;dirty=true;xSemaphoreGive(lock);
}
void native_ui_queue_drop(uint32_t total){
    if(!lock)return;
    /* Receiver callbacks can publish snapshots out of order after unlocking. */
    xSemaphoreTake(lock,portMAX_DELAY);if(total>drops){drops=total;dirty=true;}xSemaphoreGive(lock);
}
void native_ui_get_stats(native_ui_stats_t *out){
    if(!out)return;
    memset(out,0,sizeof *out);if(!lock)return;
    xSemaphoreTake(lock,portMAX_DELAY);
    *out=(native_ui_stats_t){decoded_frames,presented_frames,drops,decoder_error,source_width,source_height};
    xSemaphoreGive(lock);
}
