#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
extern int stub_alloc_fail;
static inline void *heap_caps_malloc(size_t n,int caps){(void)caps;return stub_alloc_fail?NULL:malloc(n);}
static inline void *heap_caps_calloc(size_t n,size_t size,int caps){(void)caps;return stub_alloc_fail?NULL:calloc(n,size);}
