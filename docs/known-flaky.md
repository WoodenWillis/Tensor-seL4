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

Seen again at e018e8b (`logs/boot.log`, not committed), this time with the guest's state known. The kernel reached `Run /init as init process`, and the guest then sat at `0xffffffc008fe8c80`, the `wfi` in `cpu_do_idle()`, taking repeated `Spurious interrupt!` exceptions at that PC. `status` 18 s later timed out; `ping` and `trace-dump` still worked (54879 guest records, the last one the `\n` after `TERM=linux`). While idle, the guest traps every `wfi` to the VMM (`HCR_EL2.TWI`), and libvmm replies without advancing the PC. So in this case the VMM was deaf while handling a continuous stream of WFx faults. The heartbeat added in ffcd608 is there to locate it.

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

## Linux guest: goes silent mid-boot while the rest of the system runs

| | |
|---|---|
| First seen | 2026-09-30, at 0b90826 plus `arm64.nopauth` (trace stamp `0b9082623b6d-dirty`) |
| Build | as above |
| Occurrences | 2 of 10 Linux guest boots, both stopping after the same guest record |
| Symptom | Last guest line `[    0.464540][    T1] kvm [1]: HYP mode not available`. `Trying to unpack rootfs image as initramfs...` (T8) never reports finishing |
| Still working | uartrx (`ping` answered `pong`) and the tracer (`trace-dump` printed 41827 records, 0 not archived, 0 decode errors) |
| Not yet known | Whether the VMM still answered; no `status` was sent |

What the trace shows:

- The guest trapped 41824 times in 0.887 s after `GUEST STARTED`: 13925 characters through the emulated UART (UFCON read, UFSTAT read, UTXH write each) and 8 SMCs, all PSCI and all emulated.
- The last guest record is the UTXH write of the `\n` that ends the `kvm` line. Nothing from the guest in the 11.0 s before `ping`.
- No refusal and no UNHANDLED record, so the VMM didn't stop the guest.

The trace doesn't record WFI/WFE traps or virtual interrupt delivery, so it can't tell a guest spinning at EL1 apart from a guest idling in WFI and waiting for an interrupt that never arrives.

Seen again at ffcd608 (`logs/boot2.log`, not committed), and the stop is deterministic, not random. The last guest line is again `kvm [1]: HYP mode not available`. The dump again has 41827 records, 41824 of them from the guest, the same count as the first occurrence; the two differ only in printed timestamps. This build prints a VMM heartbeat once a second from the fault path, and none appeared. So after the stop the VMM received no fault of any kind: no WFI/WFE trap, and no virtual-timer VPPI event, although the guest's tick would produce one every few milliseconds. The guest isn't idling. Either it spins at EL1 without trapping, with no timer interrupt reaching the VMM, or CPU 0 isn't running it. In the boot that reached `/init`, the next line after `kvm` was `Initialise system trusted keyrings`.
