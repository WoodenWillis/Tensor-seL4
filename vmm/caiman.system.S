<?xml version="1.0" encoding="UTF-8"?>
#define STR_(x) #x
#define STR(x) STR_(x)
<!-- SPDX-License-Identifier: BSD-2-Clause -->
#include "guest_map.h"
#include <hw/zumapro_dpu.h>
<system>
    <memory_region name="guest_ram" size=STR(GUEST_RAM_SIZE) />
#ifdef GUEST_WATCHDOG_GPA
    <memory_region name="watchdog_cl0" size="0x1000" phys_addr="0x10060000" />
#endif
    <memory_region name="trace_ring" size="0x10000" />
    <memory_region name="console_ring" size="0x4000" />
    <memory_region name="uartrx_console_ring" size="0x4000" />
    <memory_region name="uart" size="0x1000" phys_addr="0x10870000" />
    <memory_region name="trace_archive" size="0x800000" />
    <memory_region name="trace_control" size="0x1000" />
    <memory_region name="cmd_ring" size="0x1000" />
    <memory_region name="decon0" size="0x1000" phys_addr=STR(DECON0_PHYS) />
    <memory_region name="dpu_rdma0" size="0x1000" phys_addr=STR(DPU_RDMA0_PHYS) />
    <memory_region name="fbcon_fb" size=STR(FBCON_FB_SIZE) page_size="0x200000" phys_addr=STR(FBCON_FB_PHYS) />
    <memory_region name="fbcon_ring" size="0x4000" />

    <protection_domain name="VMM" priority="254" smc="true">
        <program_image path="vmm.elf" />
        <map mr="guest_ram" vaddr="0x20000000" perms="rw" setvar_vaddr="guest_ram_vaddr" />
#ifdef GUEST_WATCHDOG_GPA
        <map mr="watchdog_cl0" vaddr="0x30000000" perms="r" cached="false" setvar_vaddr="watchdog_cl0_vaddr" />
#endif
        <map mr="trace_ring" vaddr="0x40000000" perms="rw" setvar_vaddr="trace_ring_vaddr" />
        <map mr="console_ring" vaddr="0x40010000" perms="rw" setvar_vaddr="console_ring_vaddr" />
        <map mr="cmd_ring" vaddr="0x40014000" perms="rw" setvar_vaddr="cmd_ring_vaddr" />
        <virtual_machine name=GUEST_NAME>
            <vcpu id="0" cpu="3" />
            <map mr="guest_ram" vaddr=STR(GUEST_RAM_GPA) perms="rwx" />
        </virtual_machine>
    </protection_domain>

    <protection_domain name="tracer" priority="200" cpu="1">
        <program_image path="tracer.elf" />
        <map mr="trace_ring" vaddr="0x40000000" perms="rw" setvar_vaddr="trace_ring_vaddr" />
        <map mr="console_ring" vaddr="0x40010000" perms="rw" setvar_vaddr="console_ring_vaddr" />
        <map mr="uartrx_console_ring" vaddr="0x40014000" perms="rw" setvar_vaddr="uartrx_console_ring_vaddr" />
        <map mr="trace_archive" vaddr="0x50000000" perms="rw" setvar_vaddr="trace_archive_vaddr" />
        <map mr="trace_control" vaddr="0x40018000" perms="r" setvar_vaddr="trace_control_vaddr" />
        <map mr="fbcon_ring" vaddr="0x4001c000" perms="rw" setvar_vaddr="fbcon_ring_vaddr" />
    </protection_domain>

    <protection_domain name="fbcon" priority="50" cpu="4">
        <program_image path="fbcon.elf" />
        <map mr="decon0" vaddr="0x30000000" perms="rw" cached="false" setvar_vaddr="decon_vaddr" />
        <map mr="dpu_rdma0" vaddr="0x30001000" perms="rw" cached="false" setvar_vaddr="rdma_vaddr" />
        <map mr="fbcon_fb" vaddr="0x40000000" perms="rw" setvar_vaddr="fb_vaddr" />
        <map mr="fbcon_ring" vaddr="0x50000000" perms="rw" setvar_vaddr="fbcon_ring_vaddr" />
    </protection_domain>

    <protection_domain name="uartrx" priority="100" cpu="2">
        <program_image path="uartrx.elf" />
        <map mr="uart" vaddr="0x30000000" perms="r" cached="false" setvar_vaddr="uart_vaddr" />
        <map mr="trace_ring" vaddr="0x40000000" perms="rw" setvar_vaddr="trace_ring_vaddr" />
        <map mr="uartrx_console_ring" vaddr="0x40010000" perms="rw" setvar_vaddr="console_ring_vaddr" />
        <map mr="trace_control" vaddr="0x40014000" perms="rw" setvar_vaddr="trace_control_vaddr" />
        <map mr="cmd_ring" vaddr="0x40015000" perms="rw" setvar_vaddr="cmd_ring_vaddr" />
    </protection_domain>

    <channel>
        <end pd="VMM" id="1" />
        <end pd="tracer" id="1" />
    </channel>

    <channel>
        <end pd="uartrx" id="1" />
        <end pd="tracer" id="2" />
    </channel>

    <channel>
        <end pd="uartrx" id="2" />
        <end pd="VMM" id="2" />
    </channel>

    <channel>
        <end pd="tracer" id="3" />
        <end pd="fbcon" id="1" />
    </channel>
</system>
