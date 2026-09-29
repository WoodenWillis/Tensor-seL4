/* SPDX-License-Identifier: BSD-2-Clause */

    .section .text.start
    .global _start
_start:
    ldr     x0, =harness_stack_top
    mov     sp, x0
    bl      harness_main
1:
    wfe
    b       1b
