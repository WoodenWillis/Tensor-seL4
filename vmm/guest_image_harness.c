/* SPDX-License-Identifier: BSD-2-Clause */

#include <string.h>
#include <libvmm/libvmm.h>
#include <sddf/util/cache.h>

#include "guest_map.h"
#include "guest_image.h"

extern char _guest_harness_image[];
extern char _guest_harness_image_end[];
extern uintptr_t guest_ram_vaddr;

bool guest_image_load(struct guest_boot *boot)
{
    size_t size = _guest_harness_image_end - _guest_harness_image;

    if (size == 0 || size > GUEST_RAM_SIZE) {
        LOG_VMM_ERR("harness image size 0x%lx does not fit guest RAM 0x%x\n", size, GUEST_RAM_SIZE);
        return false;
    }
    memset((void *)guest_ram_vaddr, 0, GUEST_RAM_SIZE);
    memcpy((void *)guest_ram_vaddr, _guest_harness_image, size);
    cache_clean_and_invalidate(guest_ram_vaddr, guest_ram_vaddr + GUEST_RAM_SIZE);
    *boot = (struct guest_boot) { .pc = GUEST_RAM_GPA, .dtb = 0, .initrd = 0 };
    return true;
}
