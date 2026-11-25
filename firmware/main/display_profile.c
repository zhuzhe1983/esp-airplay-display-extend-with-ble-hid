/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "display_profile.h"
#include "sdkconfig.h"
#include <stdatomic.h>
#ifdef ESP_PLATFORM
#include "nvs.h"
#endif
static atomic_uint requested_profile=DISPLAY_SMOOTH;
void display_profile_init(void){
    unsigned profile=DISPLAY_SMOOTH;
#ifdef ESP_PLATFORM
    nvs_handle_t nvs;
    if(nvs_open("p4_display",NVS_READONLY,&nvs)==ESP_OK){
        uint8_t saved;
        if(nvs_get_u8(nvs,"profile",&saved)==ESP_OK&&saved<=DISPLAY_SHARP)profile=saved;
        nvs_close(nvs);
    }
#endif
    atomic_store(&requested_profile,profile);
}
display_profile_t display_profile_get(void){return (display_profile_t)atomic_load(&requested_profile);}
esp_err_t display_profile_set(display_profile_t profile){
    if(profile!=DISPLAY_SMOOTH&&profile!=DISPLAY_SHARP)return ESP_ERR_INVALID_ARG;
    if(profile==display_profile_get())return ESP_OK;
#ifdef ESP_PLATFORM
    nvs_handle_t nvs;
    esp_err_t err=nvs_open("p4_display",NVS_READWRITE,&nvs);
    if(err!=ESP_OK)return err;
    err=nvs_set_u8(nvs,"profile",(uint8_t)profile);
    if(err==ESP_OK)err=nvs_commit(nvs);
    nvs_close(nvs);
    if(err!=ESP_OK)return err;
#endif
    atomic_store(&requested_profile,(unsigned)profile);
    return ESP_OK;
}
display_profile_config_t display_profile_config(display_profile_t profile){
    uint16_t w=CONFIG_P4_STREAM_WIDTH,h=CONFIG_P4_STREAM_HEIGHT;
    if(profile==DISPLAY_SMOOTH){w=(w/2)&~1u;h=(h/2)&~1u;}
    return (display_profile_config_t){w,h,CONFIG_P4_STREAM_FPS};
}
const char *display_profile_name(display_profile_t profile){return profile==DISPLAY_SHARP?"sharp":"smooth";}
