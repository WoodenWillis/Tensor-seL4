/* SPDX-License-Identifier: BSD-2-Clause */

#include "guest_image.h"

bool guest_image_harness_load(struct guest_boot *boot, uint64_t select);
bool guest_image_linux_load(struct guest_boot *boot, uint64_t select);

bool guest_image_load(struct guest_boot *boot, uint64_t select)
{
    if (GUEST_SELECT_IS_HARNESS(select)) {
        return guest_image_harness_load(boot, select);
    }
    return guest_image_linux_load(boot, select);
}

const char *guest_image_name(uint64_t select)
{
    return GUEST_SELECT_IS_HARNESS(select) ? "harness" : "linux";
}
