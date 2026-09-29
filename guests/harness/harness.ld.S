/* SPDX-License-Identifier: BSD-2-Clause */

#include "harness_map.h"

ENTRY(_start)

SECTIONS
{
    . = HARNESS_RAM_GPA;
    .text : { *(.text.start) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
    . = ALIGN(16);
    . += HARNESS_STACK_SIZE;
    harness_stack_top = .;
    ASSERT(harness_stack_top <= HARNESS_RAM_GPA + HARNESS_RAM_SIZE, "harness does not fit in guest RAM")
}
