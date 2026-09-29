/* SPDX-License-Identifier: BSD-2-Clause */

    .section .guest_harness_image, "a"
    .global _guest_harness_image, _guest_harness_image_end
    .balign 16
_guest_harness_image:
    .incbin GUEST_HARNESS_IMAGE_PATH
_guest_harness_image_end:
