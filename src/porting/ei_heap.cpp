/* The Clear BSD License
 *
 * Copyright (c) 2026 EdgeImpulse Inc.
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

/*
 * Optional replacement for the Edge Impulse SDK allocators.
 *
 * The model allocates its ~240 KB tensor arena from the heap on
 * first inference. On targets where that does not fit in internal RAM but
 * external RAM is available (the ESP32-S3-EYE's octal PSRAM), enabling
 * CONFIG_EI_HEAP_SHARED_MULTI_HEAP moves every Edge Impulse allocation to the
 * shared multi heap. The SDK declares these functions weak precisely so that
 * they can be replaced here.
 */

#include <zephyr/kernel.h>

#ifdef CONFIG_EI_HEAP_SHARED_MULTI_HEAP

#include <zephyr/multi_heap/shared_multi_heap.h>
#include <string.h>

#include "edge-impulse-sdk/porting/ei_classifier_porting.h"

#define EI_HEAP_ATTR ((enum shared_multi_heap_attr)CONFIG_EI_HEAP_SMH_ATTRIBUTE)

/*
 * Allocations must be 16-byte aligned, not merely pointer aligned.
 *
 * Using plain shared_multi_heap_alloc() hands back 8-byte aligned blocks, and
 * on the ESP32-S3 that corrupts the PSRAM heap: sys_heap_free() panics with
 * "heap corruption (buffer overflow?)" on a block whose neighbour is still
 * live, even though nothing has written out of bounds (verified with a redzone
 * on every allocation -- no guard byte was ever touched). Requesting 16-byte
 * alignment makes it go away. This matches the rest of the platform: the SoC's
 * own heap adapter (modules/hal/espressif .../esp_heap_adapter.h) always calls
 * shared_multi_heap_aligned_alloc(SMH_REG_ATTR_EXTERNAL, 16, ...), and the
 * video subsystem aligns its buffers via CONFIG_VIDEO_BUFFER_POOL_ALIGN.
 */
#define EI_HEAP_ALIGN 16

void *ei_malloc(size_t size)
{
    return shared_multi_heap_aligned_alloc(EI_HEAP_ATTR, EI_HEAP_ALIGN, size);
}

void *ei_calloc(size_t nitems, size_t size)
{
    if (nitems != 0 && size > (SIZE_MAX / nitems)) {
        return NULL;
    }

    const size_t bytes = nitems * size;
    void *ptr = shared_multi_heap_aligned_alloc(EI_HEAP_ATTR, EI_HEAP_ALIGN, bytes);

    if (ptr != NULL) {
        memset(ptr, 0, bytes);
    }

    return ptr;
}

void ei_free(void *ptr)
{
    if (ptr != NULL) {
        shared_multi_heap_free(ptr);
    }
}

#endif // CONFIG_EI_HEAP_SHARED_MULTI_HEAP
