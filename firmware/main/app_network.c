/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "app_network.h"
#include <ctype.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_hosted.h"
#include "driver/usb_serial_jtag.h"
#include "input_ble.h"
#include "native_ui.h"
#include "display_profile.h"
#include "lwip/sockets.h"

static const char *TAG="network";
static char name[41],ssid[33],password[64],ap_name[33],ap_password[13],token[33];
static network_state_cb callback;
static void *callback_ctx;
static atomic_bool ready=false;
static bool setup_active;
static int retries;
static httpd_handle_t server;
static esp_netif_t *setup_netif;
static void report(network_state_t s,const char *a,const char *b){if(callback)callback(s,a,b,callback_ctx);}
const char *app_network_device_name(void){return name;}
bool app_network_ready(void){return atomic_load(&ready);}
static void restart_task(void *p){(void)p;vTaskDelay(pdMS_TO_TICKS(1200));esp_restart();}
static int hexval(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static bool form_value(const char *body,const char *key,char *out,size_t capacity){
    size_t k=strlen(key);
    for(const char *p=body;*p;){
        const char *end=strchr(p,'&');if(!end)end=p+strlen(p);
        if((size_t)(end-p)>k&&memcmp(p,key,k)==0&&p[k]=='='){
            size_t used=0;
            for(p+=k+1;p<end;p++){
                unsigned char c=*p;
                if(c=='+')c=' ';
                else if(c=='%'){
                    if(end-p<3||hexval(p[1])<0||hexval(p[2])<0)return false;
                    c=(hexval(p[1])<<4)|hexval(p[2]);p+=2;
                }
                if(c==0||used+1>=capacity)return false;
                out[used++]=c;
            }
            out[used]=0;return true;
        }
        p=*end?end+1:end;
    }
    return false;
}
/* Both provisioning paths commit the same NVS keys. Credentials are never logged. */
static esp_err_t save_configuration(const char *new_ssid,const char *new_password,const char *new_name){
    if(!new_ssid[0]||strlen(new_ssid)>32||strlen(new_password)>63||
       (new_password[0]&&strlen(new_password)<8)||!new_name[0]||strlen(new_name)>40)
        return ESP_ERR_INVALID_ARG;
    for(const char *p=new_name;*p;p++)if(!isalnum((unsigned char)*p)&&!strchr(" ._-",*p))return ESP_ERR_INVALID_ARG;
    nvs_handle_t nvs;esp_err_t err=nvs_open("p4_network",NVS_READWRITE,&nvs);
    if(err==ESP_OK){err=nvs_set_str(nvs,"ssid",new_ssid);if(!err)err=nvs_set_str(nvs,"password",new_password);if(!err)err=nvs_set_str(nvs,"name",new_name);if(!err)err=nvs_commit(nvs);nvs_close(nvs);}
    return err;
}
static void clear_secret(void *data,size_t size){volatile unsigned char *p=data;while(size--)*p++=0;}
static void usb_provision_task(void *arg){
    (void)arg;
    char line[641]={0};size_t used=0;bool discard=false;
    for(;;){
        uint8_t byte;
        if(usb_serial_jtag_read_bytes(&byte,1,pdMS_TO_TICKS(100))!=1)continue;
        if(byte=='\r')continue;
        if(byte!='\n'){
            if(!byte||used==sizeof line-1)discard=true;
            if(!discard)line[used++]=(char)byte;
            continue;
        }
        line[used]=0;
        if(!discard&&strcmp(line,"P4-PING")==0){
            const char reply[]="P4_PONG\n";
            usb_serial_jtag_write_bytes(reply,sizeof reply-1,pdMS_TO_TICKS(500));
        }
        if(!discard&&strcmp(line,"P4-STATUS")==0){
            esp_netif_ip_info_t ip={0};native_ui_stats_t stats;native_ui_get_stats(&stats);
            esp_netif_t *sta=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if(sta)esp_netif_get_ip_info(sta,&ip);
            char reply[384];int n=snprintf(reply,sizeof reply,
                "P4_STATUS {\"wifi_ready\":%d,\"ip\":\"" IPSTR "\",\"ble_ready\":%d,\"airplay\":\"%s\",\"decoded\":%llu,\"shown\":%llu,\"dropped\":%lu,\"decoder_error\":%d,\"source_width\":%u,\"source_height\":%u,\"profile\":\"%s\"}\n",
                app_network_ready(),IP2STR(&ip.ip),input_ble_is_ready(),airplay_receiver_state_name(airplay_receiver_get_state()),
                (unsigned long long)stats.decoded,(unsigned long long)stats.shown,(unsigned long)stats.dropped,stats.decoder_error,
                stats.source_width,stats.source_height,display_profile_name(display_profile_get()));
            if(n>0&&(size_t)n<sizeof reply)usb_serial_jtag_write_bytes(reply,n,pdMS_TO_TICKS(500));
        }
        if(!discard&&strcmp(line,"P4-PAIR")==0){
            input_ble_pairing_window();
            const char reply[]="P4_PAIR_OK\n";
            usb_serial_jtag_write_bytes(reply,sizeof reply-1,pdMS_TO_TICKS(500));
        }
        if(!discard&&strncmp(line,"P4-MODE ",8)==0){
            const char *mode=line+8;esp_err_t err=ESP_ERR_INVALID_ARG;
            if(strcmp(mode,"smooth")==0)err=display_profile_set(DISPLAY_SMOOTH);
            else if(strcmp(mode,"sharp")==0)err=display_profile_set(DISPLAY_SHARP);
            const char *reply=err==ESP_OK?"P4_MODE_OK\n":"P4_MODE_ERROR\n";
            usb_serial_jtag_write_bytes(reply,strlen(reply),pdMS_TO_TICKS(500));
        }
        if(!discard&&strncmp(line,"P4-WIFI ",8)==0){
            char new_ssid[33]={0},new_password[64]={0},new_name[41]={0};
            esp_err_t err=ESP_ERR_INVALID_ARG;
            if(form_value(line+8,"ssid",new_ssid,sizeof new_ssid)&&
               form_value(line+8,"password",new_password,sizeof new_password)&&
               form_value(line+8,"name",new_name,sizeof new_name))
                err=save_configuration(new_ssid,new_password,new_name);
            clear_secret(new_password,sizeof new_password);
            clear_secret(line,sizeof line);
            const char *reply=err==ESP_OK?"P4_WIFI_OK\n":"P4_WIFI_ERROR\n";
            usb_serial_jtag_write_bytes(reply,strlen(reply),pdMS_TO_TICKS(500));
            usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(500));
            if(err==ESP_OK){vTaskDelay(pdMS_TO_TICKS(1200));esp_restart();}
        }
        clear_secret(line,sizeof line);used=0;discard=false;
    }
}
esp_err_t app_network_start_usb_provisioning(void){
    static bool started;
    if(started)return ESP_ERR_INVALID_STATE;
    if(!usb_serial_jtag_is_driver_installed()){
        usb_serial_jtag_driver_config_t cfg={.rx_buffer_size=1024,.tx_buffer_size=512};
        esp_err_t err=usb_serial_jtag_driver_install(&cfg);if(err!=ESP_OK)return err;
    }
    if(xTaskCreate(usb_provision_task,"usb_wifi",4096,NULL,2,NULL)!=pdPASS)return ESP_ERR_NO_MEM;
    started=true;ESP_LOGI(TAG,"USB Wi-Fi provisioning ready");return ESP_OK;
}
/* The setup service is reachable only through the device's WPA2 setup AP. */
static bool on_setup_interface(httpd_req_t *req){
    struct sockaddr_in local={0};socklen_t len=sizeof local;esp_netif_ip_info_t ip={0};
    return setup_netif&&esp_netif_get_ip_info(setup_netif,&ip)==ESP_OK&&
        getsockname(httpd_req_to_sockfd(req),(struct sockaddr *)&local,&len)==0&&
        local.sin_family==AF_INET&&local.sin_addr.s_addr==ip.ip.addr;
}
static esp_err_t setup_get(httpd_req_t *req){
    if(!on_setup_interface(req))return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Connect to the setup Wi-Fi shown on the display");
    char page[3600];
    snprintf(page,sizeof page,
        "<!doctype html><html lang='zh-CN'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>P4 Display 配网</title>"
        "<style>body{margin:0;background:#11151d;color:#eef2ff;font:17px/1.6 system-ui}main{max-width:480px;margin:5vh auto;padding:24px}label{display:block;margin-top:20px}input,button{box-sizing:border-box;width:100%%;padding:14px;font:inherit;border:1px solid #53647c;border-radius:10px}input{background:#1c2534;color:white}button{margin-top:24px;background:#8cf5c6;color:#10231d;font-weight:700}small{color:#b9c7d8}</style>"
        "<main><h1>P4 Native Display</h1><p>让设备与 Mac 连接同一个局域网。保存后设备重启，请在 Mac 的系统屏幕镜像菜单中选择设备。</p>"
        "<form method='post' action='/configure'><input type='hidden' name='token' value='%s'>"
        "<label>Wi-Fi 名称<input name='ssid' maxlength='32' required autocomplete='off'></label>"
        "<label>Wi-Fi 密码<input name='password' type='password' maxlength='63' autocomplete='new-password'></label>"
        "<small>开放网络可留空；Tab5 使用 2.4 GHz，Claw4 可使用兼容的 2.4 / 5 GHz 网络。</small>"
        "<label>投屏设备名称<input name='name' value='P4 Display' maxlength='40' pattern='[A-Za-z0-9 ._-]+' required></label>"
        "<button>保存并连接</button></form><p><small>键盘与触控另外在 Mac 蓝牙设置中配对。此页面只保存本设备网络设置。</small></p></main></html>",token);
    httpd_resp_set_type(req,"text/html; charset=utf-8");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_send(req,page,HTTPD_RESP_USE_STRLEN);
}
static esp_err_t configure_post(httpd_req_t *req){
    if(!on_setup_interface(req))return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Connect to the setup Wi-Fi shown on the display");
    if(req->content_len<=0||req->content_len>640)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid form length");
    char body[641],new_ssid[33],new_password[64],new_name[41],given_token[33];
    int offset=0;
    while(offset<req->content_len){int n=httpd_req_recv(req,body+offset,req->content_len-offset);if(n<=0)return ESP_FAIL;offset+=n;}
    body[offset]=0;
    if(!form_value(body,"token",given_token,sizeof given_token)||strcmp(given_token,token)!=0)
        return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Reload the setup form");
    if(!form_value(body,"ssid",new_ssid,sizeof new_ssid)||!new_ssid[0]||
       !form_value(body,"password",new_password,sizeof new_password)||
       (new_password[0]&&strlen(new_password)<8)||
       !form_value(body,"name",new_name,sizeof new_name)||!new_name[0])
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid SSID, password or device name");
    for(char *p=new_name;*p;p++)if(!isalnum((unsigned char)*p)&&!strchr(" ._-",*p))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Use English letters, digits, spaces, . _ - for the name");
    esp_err_t err=save_configuration(new_ssid,new_password,new_name);
    memset(new_password,0,sizeof new_password);memset(body,0,sizeof body);
    if(err!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Settings could not be saved");
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_sendstr(req,"<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width'><p style='font:20px system-ui;padding:40px'>设置已保存。设备正在重启，请回到原来的 Wi-Fi。若无法连接，设备屏幕会显示新的配网热点。</p>");
    if(xTaskCreate(restart_task,"setup_restart",2048,NULL,2,NULL)!=pdPASS)esp_restart();
    return ESP_OK;
}
static esp_err_t start_setup(void){
    if(setup_active)return ESP_OK;
    uint32_t random=esp_random();snprintf(ap_name,sizeof ap_name,"P4-Display-%04lX",(unsigned long)(random&65535));
    snprintf(ap_password,sizeof ap_password,"%08lX%04lX",(unsigned long)esp_random(),(unsigned long)(random>>16));
    uint8_t bytes[16];esp_fill_random(bytes,sizeof bytes);
    for(int i=0;i<16;i++)snprintf(token+i*2,3,"%02x",bytes[i]);
    wifi_config_t ap={0};strlcpy((char *)ap.ap.ssid,ap_name,sizeof ap.ap.ssid);ap.ap.ssid_len=strlen(ap_name);
    strlcpy((char *)ap.ap.password,ap_password,sizeof ap.ap.password);
    ap.ap.channel=1;ap.ap.max_connection=2;ap.ap.authmode=WIFI_AUTH_WPA2_PSK;
    esp_err_t err=esp_wifi_set_mode(WIFI_MODE_APSTA);if(!err)err=esp_wifi_set_config(WIFI_IF_AP,&ap);
    if(err!=ESP_OK)return err;
    httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();cfg.stack_size=6144;cfg.max_open_sockets=2;cfg.lru_purge_enable=true;
    err=httpd_start(&server,&cfg);if(err!=ESP_OK)return err;
    httpd_uri_t get={.uri="/",.method=HTTP_GET,.handler=setup_get};
    httpd_uri_t post={.uri="/configure",.method=HTTP_POST,.handler=configure_post};
    if((err=httpd_register_uri_handler(server,&get))!=ESP_OK||(err=httpd_register_uri_handler(server,&post))!=ESP_OK){httpd_stop(server);server=NULL;return err;}
    setup_active=true;
    report(NETWORK_SETUP,ap_name,ap_password);
    ESP_LOGI(TAG,"Setup AP active. Credentials are shown only on the device screen. Open http://192.168.4.1");
    return ESP_OK;
}
static void wifi_event(void *arg,esp_event_base_t base,int32_t id,void *data){
    (void)arg;
    if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_START){if(ssid[0])esp_wifi_connect();}
    else if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED){
        atomic_store(&ready,false);
        if(ssid[0]&&retries++<8){if(!setup_active)report(NETWORK_CONNECTING,"Connecting to Wi-Fi","");esp_wifi_connect();}
        else if(!setup_active){esp_err_t e=start_setup();if(e!=ESP_OK)report(NETWORK_ERROR,"Wi-Fi setup failed",esp_err_to_name(e));}
    }else if(base==IP_EVENT&&id==IP_EVENT_STA_GOT_IP){
        ip_event_got_ip_t *event=data;char ip[32];snprintf(ip,sizeof ip,IPSTR,IP2STR(&event->ip_info.ip));
        retries=0;atomic_store(&ready,true);
        report(NETWORK_READY,name,ip);
    }
}
esp_err_t app_network_start(const char *default_name,network_state_cb cb,void *ctx){
    callback=cb;callback_ctx=ctx;strlcpy(name,default_name,sizeof name);
    nvs_handle_t nvs;
    if(nvs_open("p4_network",NVS_READONLY,&nvs)==ESP_OK){
        size_t n=sizeof ssid;if(nvs_get_str(nvs,"ssid",ssid,&n)!=ESP_OK)ssid[0]=0;
        n=sizeof password;if(nvs_get_str(nvs,"password",password,&n)!=ESP_OK)password[0]=0;
        n=sizeof name;if(nvs_get_str(nvs,"name",name,&n)!=ESP_OK)strlcpy(name,default_name,sizeof name);
        nvs_close(nvs);
    }
    report(NETWORK_STARTING,"Starting wireless coprocessor","");
    esp_err_t err=esp_netif_init();if(err!=ESP_OK)return err;
    err=esp_event_loop_create_default();if(err!=ESP_OK&&err!=ESP_ERR_INVALID_STATE)return err;
    if(!esp_netif_create_default_wifi_sta())return ESP_ERR_NO_MEM;
    setup_netif=esp_netif_create_default_wifi_ap();if(!setup_netif)return ESP_ERR_NO_MEM;
    err=esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,NULL);if(err!=ESP_OK)return err;
    err=esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,NULL);if(err!=ESP_OK)return err;
    wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();err=esp_wifi_init(&cfg);if(err!=ESP_OK)return err;
    err=esp_wifi_set_storage(WIFI_STORAGE_RAM);if(err!=ESP_OK)return err;
    wifi_config_t sta={0};memcpy(sta.sta.ssid,ssid,strlen(ssid));memcpy(sta.sta.password,password,strlen(password));
    sta.sta.threshold.authmode=password[0]?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;sta.sta.pmf_cfg.capable=true;sta.sta.pmf_cfg.required=false;
    if(ssid[0])report(NETWORK_CONNECTING,"Connecting to saved Wi-Fi","");
    err=esp_wifi_set_mode(WIFI_MODE_STA);if(!err)err=esp_wifi_set_config(WIFI_IF_STA,&sta);if(!err)err=esp_wifi_start();
    memset(password,0,sizeof password);memset(&sta,0,sizeof sta);
    if(err!=ESP_OK)return err;
    esp_wifi_set_ps(WIFI_PS_NONE);
    if(!ssid[0])return start_setup();
    return ESP_OK;
}
void app_network_forget_wifi(void){
    nvs_handle_t nvs;
    if(nvs_open("p4_network",NVS_READWRITE,&nvs)==ESP_OK){nvs_erase_key(nvs,"ssid");nvs_erase_key(nvs,"password");nvs_commit(nvs);nvs_close(nvs);}
    esp_restart();
}
