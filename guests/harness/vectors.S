/* SPDX-License-Identifier: BSD-2-Clause */

    .section .text
    .balign 2048
    .global lab_vectors
lab_vectors:
    .irp n, 0, 1, 2, 3, 4
    .balign 128
    mov     x0, #\n
    b       lab_bad_entry
    .endr
    .balign 128
    b       lab_irq_entry
    .irp n, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    .balign 128
    mov     x0, #\n
    b       lab_bad_entry
    .endr

lab_irq_entry:
    sub     sp, sp, #(22 * 8)
    stp     x0, x1, [sp, #(0 * 16)]
    stp     x2, x3, [sp, #(1 * 16)]
    stp     x4, x5, [sp, #(2 * 16)]
    stp     x6, x7, [sp, #(3 * 16)]
    stp     x8, x9, [sp, #(4 * 16)]
    stp     x10, x11, [sp, #(5 * 16)]
    stp     x12, x13, [sp, #(6 * 16)]
    stp     x14, x15, [sp, #(7 * 16)]
    stp     x16, x17, [sp, #(8 * 16)]
    stp     x18, x29, [sp, #(9 * 16)]
    str     x30, [sp, #(10 * 16)]
    bl      lab_irq
    ldp     x0, x1, [sp, #(0 * 16)]
    ldp     x2, x3, [sp, #(1 * 16)]
    ldp     x4, x5, [sp, #(2 * 16)]
    ldp     x6, x7, [sp, #(3 * 16)]
    ldp     x8, x9, [sp, #(4 * 16)]
    ldp     x10, x11, [sp, #(5 * 16)]
    ldp     x12, x13, [sp, #(6 * 16)]
    ldp     x14, x15, [sp, #(7 * 16)]
    ldp     x16, x17, [sp, #(8 * 16)]
    ldp     x18, x29, [sp, #(9 * 16)]
    ldr     x30, [sp, #(10 * 16)]
    add     sp, sp, #(22 * 8)
    eret

lab_bad_entry:
    mrs     x1, esr_el1
    mrs     x2, elr_el1
    mrs     x3, far_el1
    bl      lab_bad_exception
1:
    b       1b
