/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "input_ble.h"
#include "input_core.h"
#include "input_tx_queue.h"
#include <stdatomic.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_hosted.h"
#include "esp_hosted_misc.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/util/util.h"
#include "host/ble_gap.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG="input_ble";
static char device_name[25];
static input_ble_state_cb state_cb;
static void *state_ctx;
static uint8_t own_addr_type;
static atomic_uint_fast16_t connection=BLE_HS_CONN_HANDLE_NONE;
static atomic_bool encrypted=false, synced=false, suspended=false;
static bool started;
static atomic_uint subscriptions=0;
static atomic_uchar leds=0;
static atomic_uint_least32_t pairing_until_ms=0;
static uint16_t handles[4];
static const uint8_t sizes[]={8,4,5,2};
static uint8_t latest[4][8];
static portMUX_TYPE report_lock=portMUX_INITIALIZER_UNLOCKED;
static input_tx_queue_t reports;
static portMUX_TYPE queue_lock=portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t sender_handle, host_handle;
static void advertise(void);
static int gap_event(struct ble_gap_event *event,void *arg);
void ble_store_config_init(void);

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static int32_t pairing_remaining_ms(void) {
    return (int32_t)(atomic_load(&pairing_until_ms) - now_ms());
}
static bool queue_ready(void) {
    portENTER_CRITICAL(&queue_lock);
    bool ready = !reports.resetting;
    portEXIT_CRITICAL(&queue_lock);
    return ready;
}
static void request_release(void) {
    portENTER_CRITICAL(&queue_lock);
    input_tx_reset(&reports);
    portEXIT_CRITICAL(&queue_lock);
    if (sender_handle) xTaskNotifyGive(sender_handle);
}
static bool secure_reports_ready(void) {
    return atomic_load(&encrypted) && !atomic_load(&suspended) &&
           atomic_load(&connection) != BLE_HS_CONN_HANDLE_NONE && (atomic_load(&subscriptions) & 3) == 3;
}

static void status(input_ble_state_t s,const char *message){
    if(state_cb)state_cb(s,message,state_ctx);
}
static int append(struct ble_gatt_access_ctxt *c,const void *p,size_t n){
    return os_mbuf_append(c->om,p,n)==0?0:BLE_ATT_ERR_INSUFFICIENT_RES;
}
static int access(uint16_t conn,uint16_t attr,struct ble_gatt_access_ctxt *c,void *arg){
    (void)conn;(void)attr;
    uintptr_t kind=(uintptr_t)arg;
    if(c->op==BLE_GATT_ACCESS_OP_READ_DSC){
        uint8_t ref[]={(uint8_t)(kind&255),(uint8_t)(kind>>8)};
        return append(c,ref,2);
    }
    if(c->op==BLE_GATT_ACCESS_OP_READ_CHR){
        if(kind==10){const uint8_t info[]={0x11,0x01,0,2};return append(c,info,4);}
        if(kind==11)return append(c,input_report_descriptor,input_report_descriptor_size);
        if(kind==12){uint8_t value=atomic_load(&leds);return append(c,&value,1);}
        if(kind>=1&&kind<=4){
            uint8_t snapshot[8];portENTER_CRITICAL(&report_lock);
            memcpy(snapshot,latest[kind-1],sizes[kind-1]);portEXIT_CRITICAL(&report_lock);
            return append(c,snapshot,sizes[kind-1]);
        }
    }
    if(c->op==BLE_GATT_ACCESS_OP_WRITE_CHR&&(kind==12||kind==13)){
        uint8_t value;uint16_t len;
        if(OS_MBUF_PKTLEN(c->om)!=1||ble_hs_mbuf_to_flat(c->om,&value,1,&len))return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (kind == 12) atomic_store(&leds, value & 0x1f);
        else {
            if (value > 1) return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
            atomic_store(&suspended, value == 0);
            request_release();
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}
#define REF(id,type) (struct ble_gatt_dsc_def[]){{.uuid=BLE_UUID16_DECLARE(0x2908),.att_flags=BLE_ATT_F_READ,.access_cb=access,.arg=(void *)(uintptr_t)(((type)<<8)|(id))},{0}}
#define INPUT(id) {.uuid=BLE_UUID16_DECLARE(0x2A4D),.access_cb=access,.arg=(void *)(uintptr_t)(id),.flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_READ_ENC|BLE_GATT_CHR_F_NOTIFY,.val_handle=&handles[(id)-1],.descriptors=REF(id,1)}
static const struct ble_gatt_svc_def services[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=BLE_UUID16_DECLARE(0x1812),.characteristics=(struct ble_gatt_chr_def[]){
        {.uuid=BLE_UUID16_DECLARE(0x2A4A),.access_cb=access,.arg=(void *)10,.flags=BLE_GATT_CHR_F_READ},
        {.uuid=BLE_UUID16_DECLARE(0x2A4B),.access_cb=access,.arg=(void *)11,.flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_READ_ENC},
        INPUT(1),INPUT(2),INPUT(3),INPUT(4),
        {.uuid=BLE_UUID16_DECLARE(0x2A4D),.access_cb=access,.arg=(void *)12,
         .flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_NO_RSP|BLE_GATT_CHR_F_READ_ENC|BLE_GATT_CHR_F_WRITE_ENC,.descriptors=REF(1,2)},
        {.uuid=BLE_UUID16_DECLARE(0x2A4C),.access_cb=access,.arg=(void *)13,.flags=BLE_GATT_CHR_F_WRITE_NO_RSP|BLE_GATT_CHR_F_WRITE_ENC},
        {0}
    }},{0}
};
bool input_ble_is_ready(void){
    return secure_reports_ready() && queue_ready();
}
uint8_t input_ble_keyboard_leds(void){return atomic_load(&leds);}
static void publish_ready(void){
    if(input_ble_is_ready())status(INPUT_BLE_READY,"Keyboard and mouse ready");
    else if(atomic_load(&connection)!=BLE_HS_CONN_HANDLE_NONE)status(INPUT_BLE_CONNECTED,"Pairing / enabling input reports");
}
static int gap_event(struct ble_gap_event *e,void *arg){
    (void)arg;
    switch(e->type){
    case BLE_GAP_EVENT_CONNECT:
        if(e->connect.status){advertise();break;}
        atomic_store(&connection,e->connect.conn_handle);atomic_store(&encrypted,false);
        atomic_store(&subscriptions,0);atomic_store(&suspended,false);request_release();
        status(INPUT_BLE_CONNECTED,"Bluetooth connected; securing input");
        {
            int rc = ble_gap_security_initiate(e->connect.conn_handle);
            if (rc && rc != BLE_HS_EALREADY) {
                ESP_LOGW(TAG, "security initiate: %d", rc);
                status(INPUT_BLE_ERROR, "Cannot secure Bluetooth input connection");
                ble_gap_terminate(e->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            }
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        atomic_store(&connection,BLE_HS_CONN_HANDLE_NONE);atomic_store(&encrypted,false);
        atomic_store(&subscriptions,0);atomic_store(&suspended,false);request_release();
        portENTER_CRITICAL(&report_lock);
        memset(latest, 0, sizeof latest);
        portEXIT_CRITICAL(&report_lock);
        atomic_store(&leds, 0);
        status(INPUT_BLE_PAIRING,"Reconnect in Mac Bluetooth settings");advertise();break;
    case BLE_GAP_EVENT_ENC_CHANGE:{
        struct ble_gap_conn_desc d;
        bool secure=e->enc_change.status==0&&ble_gap_conn_find(e->enc_change.conn_handle,&d)==0&&d.sec_state.encrypted;
        atomic_store(&encrypted,secure);request_release();publish_ready();break;
    }
    case BLE_GAP_EVENT_SUBSCRIBE:
        for(int i=0;i<4;i++)if(e->subscribe.attr_handle==handles[i]){
            if(e->subscribe.cur_notify)atomic_fetch_or(&subscriptions,1U<<i);
            else atomic_fetch_and(&subscriptions,~(1U<<i));
        }
        request_release();publish_ready();break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        /* New pairing is allowed only in the three-minute physical window. */
        if(pairing_remaining_ms()>0)advertise();
        else status(INPUT_BLE_OFF,"Tap PAIR to reopen Bluetooth pairing");
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING:{
        if(pairing_remaining_ms()<=0)return BLE_GAP_REPEAT_PAIRING_IGNORE;
        struct ble_gap_conn_desc d;
        if(ble_gap_conn_find(e->repeat_pairing.conn_handle,&d))return BLE_GAP_REPEAT_PAIRING_IGNORE;
        ble_store_util_delete_peer(&d.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    default:break;
    }
    return 0;
}
static void advertise(void){
    if(!atomic_load(&synced)||atomic_load(&connection)!=BLE_HS_CONN_HANDLE_NONE||ble_gap_adv_active())return;
    int32_t remaining=pairing_remaining_ms();
    if(remaining<=0){status(INPUT_BLE_OFF,"Tap PAIR to connect keyboard and mouse");return;}
    struct ble_hs_adv_fields f={0},scan={0};
    static const ble_uuid16_t hid_uuid=BLE_UUID16_INIT(0x1812);
    f.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
    f.appearance=0x03C0;f.appearance_is_present=1;
    f.uuids16=(ble_uuid16_t *)&hid_uuid;f.num_uuids16=1;f.uuids16_is_complete=1;
    scan.name=(const uint8_t *)device_name;scan.name_len=strlen(device_name);scan.name_is_complete=1;
    int rc=ble_gap_adv_set_fields(&f);
    if(!rc)rc=ble_gap_adv_rsp_set_fields(&scan);
    struct ble_gap_adv_params p={.conn_mode=BLE_GAP_CONN_MODE_UND,.disc_mode=BLE_GAP_DISC_MODE_GEN,.itvl_min=48,.itvl_max=80};
    if(!rc)rc=ble_gap_adv_start(own_addr_type,NULL,remaining,&p,gap_event,NULL);
    if(rc){ESP_LOGE(TAG,"advertise: %d",rc);status(INPUT_BLE_ERROR,"Bluetooth advertising failed");}
    else status(INPUT_BLE_PAIRING,"Select device in Mac Bluetooth settings");
}
void input_ble_pairing_window(void){
    atomic_store(&pairing_until_ms,now_ms()+180000u);advertise();
}
static void on_sync(void){
    int rc=ble_hs_util_ensure_addr(0);
    if(!rc)rc=ble_hs_id_infer_auto(0,&own_addr_type);
    if(rc){status(INPUT_BLE_ERROR,"No Bluetooth address from coprocessor");return;}
    atomic_store(&synced,true);advertise();
}
static void on_reset(int reason){
    atomic_store(&synced,false);atomic_store(&encrypted,false);atomic_store(&subscriptions,0);
    atomic_store(&connection,BLE_HS_CONN_HANDLE_NONE);request_release();
    ESP_LOGW(TAG,"NimBLE reset: %d",reason);status(INPUT_BLE_ERROR,"Bluetooth reset; waiting for radio");
}
static void host_task(void *unused) {
    (void)unused;
    nimble_port_run();
    host_handle = NULL;
    vTaskDelete(NULL);
}

static bool transmit(const input_tx_report_t *report) {
    uint16_t conn = atomic_load(&connection);
    if (!atomic_load(&encrypted) || atomic_load(&suspended) || conn == BLE_HS_CONN_HANDLE_NONE ||
        !(atomic_load(&subscriptions) & (1u << (report->id - 1)))) return false;
    portENTER_CRITICAL(&queue_lock);
    bool current = report->generation == reports.generation;
    portEXIT_CRITICAL(&queue_lock);
    if (!current) return false;
    struct os_mbuf *buffer = ble_hs_mbuf_from_flat(report->data, report->len);
    if (!buffer) return false;
    /* NimBLE consumes this mbuf on success AND failure (IDF 5.5.4 ble_gatt.h). */
    int rc = ble_gatts_notify_custom(conn, handles[report->id - 1], buffer);
    if (!rc) {
        portENTER_CRITICAL(&report_lock);
        memcpy(latest[report->id - 1], report->data, report->len);
        portEXIT_CRITICAL(&report_lock);
    }
    return rc == 0;
}

static void sender_task(void *unused) {
    (void)unused;
    uint32_t release_generation = 0;
    uint8_t releases_pending = 0;
    for (;;) {
        input_tx_report_t report;
        portENTER_CRITICAL(&queue_lock);
        bool resetting = reports.resetting;
        uint32_t generation = reports.generation;
        bool have_report = input_tx_pop(&reports, &report);
        portEXIT_CRITICAL(&queue_lock);
        if (resetting) {
            if (generation != release_generation) {
                release_generation = generation;
                releases_pending = 1u | 2u | 8u;
                portENTER_CRITICAL(&report_lock);
                if (latest[INPUT_ABSOLUTE - 1][0]) releases_pending |= 4u;
                portEXIT_CRITICAL(&report_lock);
            }
            unsigned subscribed = atomic_load(&subscriptions);
            if (atomic_load(&encrypted) && !atomic_load(&suspended)) {
                for (uint8_t id = 1; id <= 4; ++id) {
                    unsigned bit = 1u << (id - 1);
                    if (!(releases_pending & subscribed & bit)) continue;
                    input_tx_report_t release = {.id = id, .len = sizes[id - 1], .generation = generation};
                    uint8_t previous[8];
                    portENTER_CRITICAL(&report_lock);
                    memcpy(previous, latest[id - 1], release.len);
                    portEXIT_CRITICAL(&report_lock);
                    input_make_release(id, previous, release.len, release.data, sizeof release.data);
                    if (transmit(&release)) releases_pending &= (uint8_t)~bit;
                }
                /* Retry release reports indefinitely while this live connection is writable.
                 * Do not accept new key-down/mouse-down events until every subscribed release succeeded. */
                if (!(releases_pending & subscribed) && secure_reports_ready()) {
                    portENTER_CRITICAL(&queue_lock);
                    bool recovered = input_tx_finish_reset(&reports, generation);
                    portEXIT_CRITICAL(&queue_lock);
                    if (recovered) publish_ready();
                }
            }
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
            continue;
        }
        if (!have_report) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
            continue;
        }
        bool sent = false;
        for (int retry = 0; retry < 3 && !sent; ++retry) {
            sent = transmit(&report);
            if (!sent) vTaskDelay(pdMS_TO_TICKS(8));
        }
        if (!sent) request_release();
        taskYIELD();
    }
}

bool input_ble_send(uint8_t id, const uint8_t *data, size_t len, void *unused) {
    (void)unused;
    if (!started || !data || !len || len != input_report_size(id) || !input_ble_is_ready() ||
        !(atomic_load(&subscriptions) & (1u << (id - 1)))) return false;
    portENTER_CRITICAL(&queue_lock);
    bool queued = input_tx_push(&reports, id, data, len);
    portEXIT_CRITICAL(&queue_lock);
    if (sender_handle) xTaskNotifyGive(sender_handle);
    return queued;
}

esp_err_t input_ble_start(const char *name, input_ble_state_cb callback, void *ctx) {
    if (started) return ESP_ERR_INVALID_STATE;
    if (!name) name = "P4 Display Input";
    size_t len = strnlen(name, sizeof device_name);
    if (!len || len >= sizeof device_name) return ESP_ERR_INVALID_ARG;
    memcpy(device_name, name, len + 1);
    state_cb = callback;
    state_ctx = ctx;
    input_tx_init(&reports);
    input_tx_reset(&reports);
    bool controller_initialized = false, controller_enabled = false, host_initialized = false;
    esp_err_t err = esp_hosted_bt_controller_init();
    if (err != ESP_OK) {
        status(INPUT_BLE_ERROR, "Coprocessor needs Wi-Fi + BLE Hosted firmware");
        return err;
    }
    controller_initialized = true;
    err = esp_hosted_bt_controller_enable();
    if (err != ESP_OK) goto fail;
    controller_enabled = true;
    err = nimble_port_init();
    if (err != ESP_OK) goto fail;
    host_initialized = true;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_svc_gap_device_name_set(device_name);
    if (!rc) rc = ble_svc_gap_device_appearance_set(0x03c0);
    if (!rc) rc = ble_gatts_count_cfg(services);
    if (!rc) rc = ble_gatts_add_svcs(services);
    if (rc) { err = ESP_FAIL; goto fail; }
    ble_store_config_init();
    atomic_store(&pairing_until_ms, now_ms() + 180000u);
    if (xTaskCreate(sender_task, "hid_tx", 4096, NULL, 5, &sender_handle) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    /* The IDF wrapper discards xTaskCreate's result; create its documented host task
     * directly so allocation failure can be reported and rolled back. */
    if (xTaskCreatePinnedToCore(host_task, "nimble_host", NIMBLE_HS_STACK_SIZE, NULL,
                               configMAX_PRIORITIES - 4, &host_handle, NIMBLE_CORE) != pdPASS) {
        vTaskDelete(sender_handle);
        sender_handle = NULL;
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    started = true;
    return ESP_OK;

fail:
    if (host_initialized) nimble_port_deinit();
    if (controller_enabled) esp_hosted_bt_controller_disable();
    if (controller_initialized) esp_hosted_bt_controller_deinit(false);
    status(INPUT_BLE_ERROR, "Bluetooth input initialization failed");
    return err;
}
