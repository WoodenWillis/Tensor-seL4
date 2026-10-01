/* SPDX-License-Identifier: BSD-2-Clause */

    .section .guest_linux_images, "a"
    .global _guest_kernel_image, _guest_kernel_image_end
    .global _guest_dtb0_image, _guest_dtb0_image_end
#ifdef GUEST_DTB1_IMAGE_PATH
    .global _guest_dtb1_image, _guest_dtb1_image_end
    .global _guest_dtb2_image, _guest_dtb2_image_end
#endif
    .global _guest_initrd_image, _guest_initrd_image_end

    .balign 4096
_guest_kernel_image:
    .incbin GUEST_KERNEL_IMAGE_PATH
_guest_kernel_image_end:

    .balign 4096
_guest_dtb0_image:
    .incbin GUEST_DTB0_IMAGE_PATH
_guest_dtb0_image_end:

#ifdef GUEST_DTB1_IMAGE_PATH
    .balign 4096
_guest_dtb1_image:
    .incbin GUEST_DTB1_IMAGE_PATH
_guest_dtb1_image_end:

    .balign 4096
_guest_dtb2_image:
    .incbin GUEST_DTB2_IMAGE_PATH
_guest_dtb2_image_end:
#endif

    .balign 4096
_guest_initrd_image:
    .incbin GUEST_INITRD_IMAGE_PATH
_guest_initrd_image_end:
