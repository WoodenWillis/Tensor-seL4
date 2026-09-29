# Known flaky behaviour

## Spurious interrupt on CPU 0 while the harness VM runs

| | |
|---|---|
| First seen | 2026-09-29, the boot that validated watchdog_cl0 read forwarding (commit after 78781ae) |
| Build | smp-debug, 8 cores, seL4 `c867f3a2`, Microkit `21c5455e`, libvmm `ae52d393` |
| Occurrences | 1 of the 4 VMM boots so far |
| Message | `<<seL4(CPU 0) [checkInterrupt/59 T0x8080341800 "VMM" @2014b8]: Spurious interrupt!>>` |
| When | Between trace records `seq=33` and `seq=34`, while the guest was printing through the emulated UART |
| Effect | None seen: the trace stayed in order and the guest finished normally |

The kernel took an IRQ exception, but the GIC's acknowledge returned no pending interrupt (INTID 1023). Not yet explained.
