/* The Clear BSD License
*
* Copyright (c) 2025 EdgeImpulse Inc.
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted (subject to the limitations in the disclaimer
* below) provided that the following conditions are met:
*
*   * Redistributions of source code must retain the above copyright notice,
*   this list of conditions and the following disclaimer.
*
*   * Redistributions in binary form must reproduce the above copyright
*   notice, this list of conditions and the following disclaimer in the
*   documentation and/or other materials provided with the distribution.
*
*   * Neither the name of the copyright holder nor the names of its
*   contributors may be used to endorse or promote products derived from this
*   software without specific prior written permission.
*
* NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY
* THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
* CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
* PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
* CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
* EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
* IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/

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

#elif defined(CONFIG_USE_EXTERNAL_HEAP) && (CONFIG_USE_EXTERNAL_HEAP != 0)

static char Z_GENERIC_SECTION(CONFIG_USE_EXTERNAL_HEAP_SECTION) additional_heap_buffer[CONFIG_HEAP_MEM_POOL_SIZE];
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

#else
// the standard ei_malloc/ei_free will be used

#endif
