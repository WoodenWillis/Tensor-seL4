/* SPDX-License-Identifier: BSD-2-Clause */

    .section .guest_linux_images, "a"
    .global _guest_kernel_image, _guest_kernel_image_end
    .global _guest_dtb_image, _guest_dtb_image_end
    .global _guest_initrd_image, _guest_initrd_image_end

    .balign 4096
_guest_kernel_image:
    .incbin GUEST_KERNEL_IMAGE_PATH
_guest_kernel_image_end:

    .balign 4096
_guest_dtb_image:
    .incbin GUEST_DTB_IMAGE_PATH
_guest_dtb_image_end:

    .balign 4096
_guest_initrd_image:
    .incbin GUEST_INITRD_IMAGE_PATH
_guest_initrd_image_end:
