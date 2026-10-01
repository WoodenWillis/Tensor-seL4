# seL4 patch ledger

Every change to the seL4 kernel lives in `patches/seL4/` and is applied by `tools/fetch-deps.sh` on top of the pinned upstream commit. The kernel is kept as close to upstream as possible; each patch is listed here with why it exists and whether it is meant to stay.

| Patch | What | Status |
|---|---|---|
| 0001 | Add the `tensor-g4` platform (Google Tensor G4, zumapro): memory, GICv3, timer, UART from the caiman DTB | Permanent |
| 0002 | Reserve the `pkvm_guest_firmware` carveout so seL4 never hands it out as RAM | Permanent |
| 0003 | `hardware_gen`: per-instance kernel devices and reserving a whole block | Permanent |
| 0004 | Describe the 29 S2MPUs as kernel devices, so the kernel owns them | Permanent |
| 0005 | Allow a multi-page bootinfo frame (the device has more untyped regions than one page holds) | Permanent |
| 0006 | Debug-only `seL4_DebugGICDump()`: prints every core's current thread and GIC redistributor state | **Temporary.** Added 2026-10-01 to diagnose cores that stop taking interrupts while running a vCPU (`docs/known-flaky.md`). Built only with `CONFIG_DEBUG_BUILD` and GICv3. Remove once that is understood |
| 0007 | Debug builds only: the aarch64 idle thread spins on `yield` instead of `wfi` | **Temporary experiment.** Added 2026-10-01. It tests whether idle cores sleeping in `wfi` are why a broadcast TLB invalidation from the guest's core never completes (`docs/known-flaky.md`). Remove once that's settled; it keeps idle cores burning power |
