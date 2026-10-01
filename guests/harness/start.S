/* SPDX-License-Identifier: BSD-2-Clause */

    .section .text.start
    .global _start
_start:
    mov     x19, x0
    ldr     x1, =harness_stack_top
    mov     sp, x1
    mov     x0, x19
    bl      harness_main
1:
    wfe
    b       1b
