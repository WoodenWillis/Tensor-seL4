/* SPDX-License-Identifier: BSD-2-Clause */

#include <string.h>
#include <libvmm/libvmm.h>
#include <sddf/util/cache.h>

#include "guest_map.h"
#include "guest_image.h"

extern char _guest_kernel_image[];
extern char _guest_kernel_image_end[];
extern char _guest_dtb0_image[];
extern char _guest_dtb0_image_end[];
#if GUEST_NUM_VCPUS > 1
extern char _guest_dtb1_image[];
extern char _guest_dtb1_image_end[];
extern char _guest_dtb2_image[];
extern char _guest_dtb2_image_end[];
#endif
extern char _guest_initrd_image[];
extern char _guest_initrd_image_end[];
extern uintptr_t guest_ram_vaddr;

static void zero_guest_ram(void)
{
    memset((void *)guest_ram_vaddr, 0, GUEST_RAM_SIZE);
    cache_clean_and_invalidate(guest_ram_vaddr, guest_ram_vaddr + GUEST_RAM_SIZE);
}

struct image {
    char *start;
    char *end;
};

static bool dtb_for_vcpu(size_t vcpu, struct image *dtb)
{
    static const struct image dtbs[GUEST_NUM_VCPUS] = {
        { _guest_dtb0_image, _guest_dtb0_image_end },
#if GUEST_NUM_VCPUS > 1
        { _guest_dtb1_image, _guest_dtb1_image_end },
        { _guest_dtb2_image, _guest_dtb2_image_end },
#endif
    };

    if (vcpu >= GUEST_NUM_VCPUS) {
        LOG_VMM_ERR("no device tree for vCPU %lu\n", vcpu);
        return false;
    }
    *dtb = dtbs[vcpu];
    return true;
}

bool guest_image_load(struct guest_boot *boot, uint64_t select)
{
    size_t kernel_size = _guest_kernel_image_end - _guest_kernel_image;
    size_t initrd_size = _guest_initrd_image_end - _guest_initrd_image;
    struct image dtb;

    if (GUEST_SELECT_GUEST(select) != GUEST_SELECT_DEFAULT && GUEST_SELECT_GUEST(select) != GUEST_SELECT_LINUX) {
        LOG_VMM_ERR("guest selection 0x%lx: this VMM carries only Linux\n", select);
        return false;
    }
    if (!dtb_for_vcpu(GUEST_SELECT_VCPU(select), &dtb)) {
        return false;
    }
    zero_guest_ram();
    uintptr_t pc = linux_setup_images(GUEST_RAM_GPA, (uintptr_t)_guest_kernel_image, kernel_size,
                                      (uintptr_t)dtb.start, GUEST_DTB_GPA, dtb.end - dtb.start,
                                      (uintptr_t)_guest_initrd_image, GUEST_INITRD_GPA, initrd_size);
    if (pc == 0) {
        LOG_VMM_ERR("failed to place the Linux images in guest RAM\n");
        return false;
    }
    *boot = (struct guest_boot) { .pc = pc, .dtb = GUEST_DTB_GPA, .initrd = GUEST_INITRD_GPA };
    return true;
}

const char *guest_image_name(uint64_t select)
{
    return "linux";
}
