#pragma once
#include <assert.h>
#include <stdlib.h>
#include "FreeRTOS.h"
typedef struct {int locked;} stub_mutex_t;
typedef stub_mutex_t *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void){return calloc(1,sizeof(stub_mutex_t));}
/* Re-taking a held non-recursive lock is a deterministic test failure. */
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t lock,TickType_t ticks){
    (void)ticks;assert(lock&&!lock->locked);lock->locked=1;return pdTRUE;
}
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t lock){assert(lock&&lock->locked);lock->locked=0;return pdTRUE;}
static inline void vSemaphoreDelete(SemaphoreHandle_t lock){assert(!lock->locked);free(lock);}
