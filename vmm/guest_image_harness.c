/* SPDX-License-Identifier: BSD-2-Clause */

#include <string.h>
#include <libvmm/libvmm.h>
#include <sddf/util/cache.h>

#include "guest_map.h"
#include "guest_image.h"

extern char _guest_harness_image[];
extern char _guest_harness_image_end[];
extern uintptr_t guest_ram_vaddr;

bool guest_image_load(struct guest_boot *boot, uint64_t select)
{
    size_t size = _guest_harness_image_end - _guest_harness_image;

    if (GUEST_SELECT_GUEST(select) != GUEST_SELECT_DEFAULT && !GUEST_SELECT_IS_HARNESS(select)) {
        LOG_VMM_ERR("guest selection 0x%lx: this VMM carries only the harness\n", select);
        return false;
    }
    if (size == 0 || size > GUEST_RAM_SIZE) {
        LOG_VMM_ERR("harness image size 0x%lx does not fit guest RAM 0x%x\n", size, GUEST_RAM_SIZE);
        return false;
    }
    memset((void *)guest_ram_vaddr, 0, GUEST_RAM_SIZE);
    memcpy((void *)guest_ram_vaddr, _guest_harness_image, size);
    cache_clean_and_invalidate(guest_ram_vaddr, guest_ram_vaddr + GUEST_RAM_SIZE);
    *boot = (struct guest_boot) { .pc = GUEST_RAM_GPA, .dtb = GUEST_SELECT_HARNESS_MODE(select), .initrd = 0 };
    return true;
}

const char *guest_image_name(uint64_t select)
{
    return "harness";
}
