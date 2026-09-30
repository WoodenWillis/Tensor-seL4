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

## Linux guest: VMM stops answering at guest start

| | |
|---|---|
| First seen | 2026-09-30, first boot at 0b90826 (`build/boot-linux.img`) |
| Build | smp-debug, 8 cores, seL4 `6e7c3b73`, Microkit `ec86afdc`, libvmm `d2e37165` (+ patches), `GUEST=linux` |
| Occurrences | 2 of 7 Linux guest boots: once with `arm64.nopauth` on the cmdline, once without, so not caused by that flag |
| Symptom | After `run 1 started from a fresh linux image`, no `guest|` line at all. Every later command (`status`, `guest-start`, `guest-stop`) gets `UARTRX|WARN: the VMM did not finish command N within 2 s` |
| Still working | uartrx (its warnings print) and the tracer (it prints them). No monitor fault report, so the VMM did not crash |
| Not yet known | Whether the guest trapped at all; no `trace-dump` was taken |

The VMM (priority 254) and the guest vCPU (priority 0) share CPU 0, so a guest spinning without trapping cannot starve the VMM. The VMM answered commands before `guest-start` in the same boots. Something keeps CPU 0 from running the VMM's notification handler. Not yet explained.

## Linux guest: whole system freezes during boot

| | |
|---|---|
| First seen | 2026-09-30, at 0b90826 (`build/boot-linux.img`) |
| Build | as above |
| Occurrences | 1 of 7 Linux guest boots |
| Symptom | Guest output stops after `[    0.361232][    T1] clocksource: Switched to clocksource arch_sys_counter`. Typed characters stop echoing |
| Before it | Several `<<seL4(CPU 0) [checkInterrupt/59 ...]: Spurious interrupt!>>` lines while the guest ran, with the current thread `linux` or `VMM` |

Echo goes uartrx (CPU 2) → tracer (CPU 1) → `seL4_DebugPutChar` and doesn't involve CPU 0, so CPUs 1 and 2 stopped making progress too. One explanation that fits, not yet tested: CPU 0 stuck while holding the kernel lock. Not yet explained.
