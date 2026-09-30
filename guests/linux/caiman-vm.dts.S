/* SPDX-License-Identifier: BSD-2-Clause */

#include "guest_map.h"

/dts-v1/;

/ {
    #address-cells = <2>;
    #size-cells = <2>;
    model = "caiman seL4 VM";
    interrupt-parent = <&gic>;

    cpus {
        #address-cells = <1>;
        #size-cells = <0>;

        cpu@0 {
            device_type = "cpu";
            compatible = "arm,armv8";
            reg = <0x0>;
            enable-method = "psci";
        };
    };

    psci {
        compatible = "arm,psci-1.0";
        method = "smc";
    };

    memory@80000000 {
        device_type = "memory";
        reg = <0x0 GUEST_RAM_GPA 0x0 GUEST_RAM_SIZE>;
    };

    gic: interrupt-controller@10400000 {
        compatible = "arm,gic-v3";
        #interrupt-cells = <3>;
        interrupt-controller;
        reg = <0x0 GUEST_GICD_GPA 0x0 0x10000>,
              <0x0 GUEST_GICR_GPA 0x0 0x20000>;
    };

    timer {
        compatible = "arm,armv8-timer";
        interrupts = <1 13 0xff08>,
                     <1 14 0xff08>,
                     <1 11 0xff08>,
                     <1 10 0xff08>;
        clock-frequency = <24576000>;
    };

    chosen {
        bootargs = "earlycon=exynos4210,mmio32,0x10870000 keep_bootcon rdinit=/init loglevel=8 nokaslr arm64.nosve arm64.nomte";
        linux,initrd-start = <0x0 GUEST_INITRD_GPA>;
        linux,initrd-end = <0x0 (GUEST_INITRD_GPA + GUEST_INITRD_SIZE)>;
    };
};
