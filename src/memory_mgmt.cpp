#include <zephyr/kernel.h>
#include "memory_mgmt.h"
#include "edge-impulse-sdk/porting/ei_classifier_porting.h"

#if defined(CONFIG_USE_SHARED_MULTI_HEAP) && (CONFIG_USE_SHARED_MULTI_HEAP != 0)
#include <zephyr/multi_heap/shared_multi_heap.h>

#define DEBUG_MEM_MGMT 0

void *ei_malloc(size_t size) 
{
#if DEBUG_MEM_MGMT == 1
    ei_printf("ei_malloc: allocating %d bytes\r\n", (int)size);
    void *ptr = shared_multi_heap_aligned_alloc(SMH_REG_ATTR_EXTERNAL, 32, size);
    ei_printf("ei_malloc: allocated %d bytes at address %p\r\n", (int)size, ptr);
    return ptr;
#else
    return shared_multi_heap_aligned_alloc(SMH_REG_ATTR_EXTERNAL, 32, size);
#endif
}

void *ei_calloc(size_t nitems, size_t size) 
{
#if DEBUG_MEM_MGMT == 1
    ei_printf("ei_calloc: allocating %d bytes\r\n", (int)(size * nitems));
    void *ptr = shared_multi_heap_aligned_alloc(SMH_REG_ATTR_EXTERNAL, 32, size * nitems);
    ei_printf("ei_calloc: allocated %d bytes at address %p\r\n", (int)(size * nitems), ptr);
    return ptr;
#else
    return shared_multi_heap_aligned_alloc(SMH_REG_ATTR_EXTERNAL, 32, size * nitems);
#endif
}

void ei_free(void *ptr) 
{
#if DEBUG_MEM_MGMT == 1
    ei_printf("ei_free: freeing memory at address %p\r\n", ptr);
#endif
    shared_multi_heap_free(ptr);
#if DEBUG_MEM_MGMT == 1
    ei_printf("after ei_free: freed %p\r\n", ptr);
#endif    
}

#elif defined(CONFIG_USE_EXTERNAL_HEAP)

static char Z_GENERIC_SECTION(".ext_ram.data") __aligned(8) additional_heap_buffer[0x40000];
static struct k_heap additional_heap_pool;
static bool mem_heap_initialized = false;

void init_external_heap(void) 
{
    if (mem_heap_initialized == false) {
        mem_heap_initialized = true;
        k_heap_init(&additional_heap_pool, additional_heap_buffer, sizeof(additional_heap_buffer));
    }
}

void *ei_malloc(size_t size) {
    return k_heap_alloc(&additional_heap_pool, size, K_NO_WAIT);
}

void *ei_calloc(size_t nitems, size_t size) {
    return k_heap_calloc(&additional_heap_pool, nitems, size, K_NO_WAIT);
}

void ei_free(void *ptr) {
    k_heap_free(&additional_heap_pool, ptr);
}

#endif