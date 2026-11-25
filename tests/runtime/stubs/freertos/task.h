#pragma once
#include <setjmp.h>
#include <stddef.h>
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
extern jmp_buf stub_task_exit;
extern int stub_exit_on_idle,stub_exit_after_delay,stub_notifications;
extern int64_t stub_time_us;
extern void (*stub_delay_hook)(void);
static inline BaseType_t xTaskCreate(void (*entry)(void *),const char *name,unsigned stack,
                                    void *arg,unsigned priority,TaskHandle_t *handle){
    (void)entry;(void)name;(void)stack;(void)arg;(void)priority;
    if(handle)*handle=(void *)1;
    return pdPASS;
}
static inline BaseType_t xTaskCreatePinnedToCore(void (*entry)(void *),const char *name,unsigned stack,
       void *arg,unsigned priority,TaskHandle_t *handle,BaseType_t core){
    (void)core;return xTaskCreate(entry,name,stack,arg,priority,handle);
}
static inline void xTaskNotifyGive(TaskHandle_t task){(void)task;stub_notifications++;}
static inline uint32_t ulTaskNotifyTake(BaseType_t clear,TickType_t ticks){
    (void)clear;(void)ticks;if(stub_exit_on_idle)longjmp(stub_task_exit,1);return 0;
}
static inline void vTaskDelay(TickType_t ticks){
    stub_time_us+=(int64_t)ticks*1000;
    if(stub_delay_hook)stub_delay_hook();
    if(stub_exit_after_delay)longjmp(stub_task_exit,1);
}
