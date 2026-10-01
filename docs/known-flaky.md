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

On ffcd608 (`logs/boot3.log`, not committed), the first run reached `init: hello from userspace`. The VMM then answered `status` while the guest idled for more than 100 s: about 98,000 WFI traps per second, and 3–4 timer injections per second, each acknowledged. `guest-stop` and `status` also worked. After `guest-start` (run 2, which inherits run 1's vGIC state because libvmm doesn't reset it), the guest froze after `trace event string verifier disabled`, before its GIC init. `status` timed out, `ping` answered, and there was no heartbeat. Every occurrence so far fits the VMM getting stuck inside the handler for one guest fault. a32b637 makes uartrx print a VMM breadcrumb (phase, fault label, message registers) when a command times out.

On a32b637 (`logs/boot4.log`, not committed), the stall at start happened again, and the breadcrumb ruled out a stuck handler: `idle in its event loop for 13177 ms, label/ch 2 …, seq 2`. After handling `guest-start` (channel 2) the VMM received no guest fault at all, and it never woke up for the `status` notification, although uartrx and the tracer kept running. CPU 0 went into the guest and seL4 never scheduled the VMM on it again. seL4's timer here is CNTHP (PPI 26), which the guest can't reach. 7056e26 moves the guest vCPU to CPU 3 to find out whether the guest's core or the VMM's core is the one that stops.

With the vCPU on CPU 3 (f370e47), the stall at start happened again, and this time the VMM on CPU 0 stayed responsive. `ping` and `status` both answered: `guest running (run 1)`, with every exit counter at 0. So the VMM was only ever collateral damage of sharing its core. The core given the guest is the one that stops, and the guest never makes its first trapped access. Still open: whether CPU 3 never ran the vCPU (a lost wakeup IPI to a core idling with no timer) or ran it and is stuck in Linux's early boot without trapping. The `guest-regs` command (trace v4) reads the vCPU's registers to tell these apart.

At cc3d599 (`logs/boot5.log`), the guest stalled mid-boot after `UDP hash table entries: 256`. `ping` still worked; `guest-regs` then froze the whole system. seL4 stalls a remote core to read a thread's registers only if that thread is the core's current thread (`remoteTCBStall`), so CPU 3 was running the vCPU and never handled the remote-call IPI. CPU 0 spun in `ipi_wait()` holding the kernel lock. So the core running the guest stops taking interrupts. That also cuts off the guest's virtual timer, which explains a guest that goes silent without trapping. f71f484 adds `gic-dump` (debug-only seL4 patch 0006) to read every core's GIC redistributor state from CPU 2 during a stall.

`gic-dump` during the next stall (f71f484, `logs/boot5.log` second capture) showed:
- **CPU 3:** current thread `linux`, vCPU loaded and active. `ISENABLER0 0xe000003` (SGI 0–1, PPI 25–27), `ISPENDR0 0x4000000` (INTID 26, seL4's CNTHP timer, pending), `ISACTIVER0 0`. A pending, enabled interrupt and nothing active: CPU 3 takes no interrupts at all, so it isn't blocked behind a stuck active one.
- **CPUs 4–7, idle:** INTID 26 active and pending. The kernel timer was acknowledged and never deactivated on those cores. That's unexplained, but it doesn't block other interrupts, because seL4's split EOI mode drops priority on EOI.
- **`IGROUPR0` reads 0 everywhere:** with GICD_CTLR.DS == 0 that register is RAZ to Non-secure reads, so it tells nothing.

e82c30a adds `pc-sample`, the vendor exynos-coresight PMUPCSR sequence, to see where CPU 3 is executing.

At e82c30a (`logs/boot5.log`, third capture), three `guest-start`/`guest-stop` cycles in one boot all reached `init: hello from userspace`, so restarting with libvmm's un-reset vGIC works at least that far. The fourth run froze the whole system during early boot: the last line, after `dyndbg: Ignore empty _ddebug table`, was cut off mid-print, so the tracer stopped mid-line. A total freeze leaves nothing to type `pc-sample` on, because uartrx's output needs system calls and so the kernel lock. The `freezewatch` PD (CPU 5) makes no system calls after `init()`. When console output is pending and the UART transmitter has been idle for 3 s, it samples every core's PC through CoreSight and writes the result straight to the UART transmit register.

At 9f91a3e (`logs/boot5.log`, fourth capture), the guest stalled at start of run 1 and `pc-sample` worked. CoreSight PC sampling is available on this unlocked device. CPU 3 was at EL1 NS, PC `0xffffffc00804007c`, the same in all five samples. That instruction is a `dsb ish` after a loop of `tlbi vaale1is` (TLB invalidate by VA, all ASIDs, Inner Shareable) in the guest kernel. CPU 3 is blocked in the barrier, waiting for a broadcast TLB invalidation that never completes, and a core blocked in a DSB takes no interrupts. CPUs 0, 6 and 7 were on the `wfi` in seL4's `idle_thread` (`0x80800119a4`). The rest were busy at EL0 or EL2. Leading hypothesis, not yet tested: an idle core in `wfi` drops into a retention or power-down state left enabled by firmware and doesn't acknowledge the broadcast invalidation.

At f27c16f (`logs/boot5.log`, fifth capture), another stall. In all five `pc-sample` samples CPU 3 reported PC `0x8080010480`, EL1 NS. That address is `arm_vector_table + 0x480`, seL4's EL2 vector for an IRQ from a lower EL (`b lower_el_irq`). CPU 3 is stuck at entry to the IRQ vector, and the EL1 field suggests exception entry never completed. The `gic-dump` agrees that an interrupt had started: INTID 27 (virtual timer) is no longer enabled on CPU 3 (`ISENABLER0 0x6000003`), and INTID 26 is pending. The two captures, a `dsb ish` after a broadcast `tlbi` and an exception entry, are both points where the core waits for earlier work to complete. So CPU 3 seems to have an outstanding operation that never completes: a broadcast TLB invalidation or a memory transaction. CPUs 0, 6 and 7 were again in seL4's idle `wfi`.

With seL4 patch 0007 (9bc7dff; idle cores spin on `yield` and never execute `wfi`), the system still froze. So idle cores sleeping in `wfi` are very likely not the non-responder (one run). `freezewatch` stayed silent again. Its only trigger needed console output waiting in a ring, and in this freeze nothing was waiting: uartrx was presumably blocked in a system call before it read the typed key, and the tracer had already emptied the rings. A second trigger now fires when received characters stay unread in the UART FIFO for 4 s. A live uartrx drains the FIFO within microseconds, apart from its own wait of up to 2 s for the VMM.

At 05e2b0a (`logs/boot5.log`, sixth capture; idle cores spinning), a stall during `alternatives: applying system-wide alternatives`. In all five `pc-sample` samples CPU 3 was at guest PC `0xffffffc008040768`, a `dsb ish` right after `tlbi vaale1is`. Two of three captures are this exact pattern: a broadcast TLB invalidation from the guest that never completes, with no core executing `wfi`. A stalled guest can't be stopped or restarted: seL4 needs an IPI taken by CPU 3 to stop or inspect a thread there, so `guest-stop` turns the stall into a total freeze.

At d42d375 (`logs/boot6.log`), `tlbi-stress` was typed while a guest was booting. The system froze before the first progress line, and `freezewatch` reported. It repeated its report about 17 times, because its own UART output re-armed it; that's fixed. The samples:
- **CPU 2, EL2 `0x8080036b3c`:** the stress loop's `dsb ish` after `tlbi vaale1is, xzr`, holding the kernel lock.
- **CPU 3, EL1 `0xffffffc00804007c`:** the guest's `dsb ish` after `tlbi vaale1is`.
- **CPUs 0, 1, 4, 6 and 7, EL2 `0x8080010c2c`–`0x8080010cdc`:** inside `c_handle_interrupt`, spinning on the kernel lock.

So once the guest's core is in this state, broadcast TLB invalidation from any core never completes, seL4's own included. Checked and ruled out: stage-2 guest RAM is `S2_NORMAL` (write-back) and Inner Shareable, and `VTCR_EL2` walks are write-back and Inner Shareable. Still open: whether `tlbi-stress` hangs with no guest running.

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

## Display state left by ABL differs between boots

| | |
|---|---|
| First seen | 2026-10-01, the first boot with `fbcon` (3b7b2e3 + 8fd5281) |
| Symptom | RDMA0's `BASEADDR_P0` read back an address other than `0xfac00000`, the framebuffer attempt 1 had seen. The value wasn't captured. `fbcon` refused to draw, and the panel kept ABL's Google logo |
| Known variation (user, from attempt 1 and this boot) | The DECON command-mode pipeline is inherited from ABL, so its state isn't the same every boot. `RDMA_IMG_SIZE` can read back 0 (then the panel's 1280×2856 applies), and `RDMA_SRC_STRIDE_0` can read back 0 (then 5120, or width × 4 for another width) |

`fbcon` now keeps ABL's pipeline but scans out of its own buffer. It points `BASEADDR_P0` at the 16 MiB at `0xfac00000` and logs the value ABL had left. The display's S2MPU (`dpuf0`) is still as ABL configured it. If a boot's configuration doesn't cover `0xfac00000`, the display's reads of our buffer would be blocked.
