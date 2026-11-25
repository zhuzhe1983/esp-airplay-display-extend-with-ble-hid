#pragma once
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
typedef struct {size_t capacity,size,head,count;unsigned char *items;} *QueueHandle_t;
static inline QueueHandle_t xQueueCreate(UBaseType_t capacity,UBaseType_t size){
    QueueHandle_t q=calloc(1,sizeof(*q));if(!q)return NULL;
    q->items=calloc(capacity,size);if(!q->items){free(q);return NULL;}
    q->capacity=capacity;q->size=size;return q;
}
static inline BaseType_t xQueueSend(QueueHandle_t q,const void *item,TickType_t ticks){
    (void)ticks;if(q->count==q->capacity)return pdFALSE;
    memcpy(q->items+((q->head+q->count)%q->capacity)*q->size,item,q->size);q->count++;return pdTRUE;
}
static inline BaseType_t xQueueReceive(QueueHandle_t q,void *item,TickType_t ticks){
    (void)ticks;if(!q->count)return pdFALSE;
    memcpy(item,q->items+q->head*q->size,q->size);q->head=(q->head+1)%q->capacity;q->count--;return pdTRUE;
}
static inline UBaseType_t uxQueueSpacesAvailable(QueueHandle_t q){return (UBaseType_t)(q->capacity-q->count);}
static inline void vQueueDelete(QueueHandle_t q){free(q->items);free(q);}
