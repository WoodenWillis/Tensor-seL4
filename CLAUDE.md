# CLAUDE.md

## What this project is

Port the Google Pixel 9 Pro (`caiman`, Tensor G4 / `zumapro`) to seL4 Microkit, then use
seL4 as a hypervisor to run isolated VMs on the real hardware.

The point is **not** to build a phone OS. The point is to give the open source community a
controlled, reproducible environment in which the proprietary Android vendor stack runs
inside a VM while we watch every MMIO access, interrupt, SMC and DMA setup it performs from
outside the VM, where the blob cannot see us and cannot lie to us.

Success looks like: someone clones this repo, runs one command, flashes their own Pixel 9
Pro, boots a stock vendor image in a VM, and gets the same register trace we got.

Everything below is in service of that. When a design choice is ambiguous, pick the option
that makes traces more faithful or more reproducible, even at a cost in performance.

---

## How I want you to write code

### Comments: propose, don't write

**Do not write explanatory comments in the code.** No block comments, no design rationale,
no "this is subtle because..." paragraphs, no function-header prose. I write the comments in
this repo myself.

What you may leave inline, without asking:
- SPDX license headers
- `TODO(will):` / `FIXME:` markers, one line
- A bare register name or spec citation where a magic number appears, e.g. `0x30` on its own
  line means nothing, `/* PSCI_CPU_ON */` is fine

Everything else goes in a **Commentary** section after the code block, in chat, not in the
file. Format:

```
## Commentary

**`vgic_handle_dist_write()`, above the `case GICD_ISENABLER` branch**
Proposed: "Enable bits are write-1-to-set; the guest writing 0 must be dropped, not
propagated, or we lose enables the guest set in an earlier access."
Why: the write-1-to-set semantics are not obvious from the code, the `if (!val) return;`
looks like a redundant guard and someone will delete it.

**`caiman_uart_putc()`, the busy-wait loop**
Proposed: none.
Why: it reads as what it is.
```

Rules for the Commentary section:
- One entry per proposed comment, anchored to a function and a specific location.
- Give the **exact text** you would write, then the reason you'd write it.
- "Proposed: none" entries are welcome and useful — tell me where you considered a comment
  and decided the code carried itself.
- Rank them if there are more than about five, most valuable first.
- If the explanation is really about the *system* rather than this code, say so; it probably
  belongs in `docs/`, not in a comment, and I'll decide.

If a piece of code can only be understood with a comment, say that out loud. Usually it
means the code should change.

### The rest of the style

- C11, freestanding, no libc beyond what Microkit gives us. Rust is fine in `tools/`.
- No dynamic allocation in PDs. Microkit systems are static; keep them static.
- Fixed-width types (`uint32_t`, not `unsigned`). MMIO through `volatile` accessors only,
  never a struct overlay cast.
- Functions do one thing. Prefer six small functions over one with six sections.
- Name things after the hardware document that defines them. If the DTS calls it
  `sysmmu_hsi0`, so do we.
- Errors propagate; nothing silently returns 0 on failure. In a VMM, a swallowed fault is a
  corrupted trace.

---

## Architecture

```
EL3   TrustZone / TF-A / Trusty          (not ours, treat as hostile-by-default)
EL2   seL4 kernel, as hypervisor
EL1   VMM PDs, sDDF drivers, tracer, native PDs   (Microkit protection domains)
EL1   guest VMs (stock Android vendor stack; later, minimal harness images)
```

- **seL4 kernel** — needs a new platform. Timer, GICv3, serial, memory map derived from the
  device tree, not from guesswork.
- **Microkit** — needs a board registered in `build_sdk.py` (`KernelARMPlatform`), a UART in
  `loader/src/uart.c`, and whatever `loader/src/aarch64/init.c` needs for PSCI on this SoC.
  Virtualisation support is not in mainline Microkit; we track the dev branch that `libvmm`
  expects. Pin the commit.
- **libvmm** (`au-ts/libvmm`) — our VMM PDs are built on it. Upstream anything generic;
  keep only caiman-specific logic in-tree.
- **sDDF** — for any native (non-VM) driver we end up writing.
- **Tracer PD** — owns the trace ring buffer, does no parsing. Decoding happens on the host.

Each VM gets its own VMM PD. One VM per device-under-observation where possible. Do not
merge VMMs to save memory.

---

## Hardware facts

Confirmed:
- Pixel 9 Pro, codename `caiman`. Sibling boards: `tokay` (9), `komodo` (9 Pro XL),
  `comet` (9 Pro Fold). Same SoC, different memory/display/peripheral config. Structure the
  port so a sibling is a board file, not a fork.
- SoC: Google Tensor G4, codename `zumapro`, Samsung-fabbed, Exynos-derived IP.
- CPU: 1× Cortex-X4, 3× Cortex-A720, 4× Cortex-A520, ARMv9-A.
- GPU: Arm Mali-G715 MC7.
- 16 GB RAM.

Derive, never assume:
- Memory map, reserved/carveout regions, device base addresses, IRQ numbers → from the
  vendor DTB pulled off the device, checked into `hw/dts/` with the build fingerprint.
- Exception level at handoff. Android hands off at EL2 for pKVM; confirm on-device before
  relying on it, and record how you confirmed it.
- GIC version and redistributor layout → from the DTS, then verify by reading `GICD_PIDR2`.
- PSCI conduit (SMC vs HVC) and which PSCI functions TF-A actually implements here.

Exynos lineage means upstream Linux Exynos drivers are a strong prior for the UART, MCT,
SysMMU and clock blocks. A strong prior is not a fact. Check it against silicon.

---

## Boot path

The bootloader is unlocked ABL. It accepts an Android boot image whose kernel payload is an
arm64 `Image` — magic at offset `0x38`, `text_offset` and `image_size` in the header. The
Microkit loader gets wrapped in that header and packed into a `boot.img`, so from ABL's
point of view we look like a Linux kernel.

Practical consequences, in priority order:

1. **Serial first.** Nothing else is debuggable until the loader can print. The Pixel has no
   exposed UART header; the debug UART comes out over USB-C via the SBU pins in a specific
   accessory mode. Until that works, every failure is a black screen and you learn nothing.
   `docs/serial.md` is the single most important document in the repo.
2. **Never touch the locked slot.** Flash to the inactive slot, keep a known-good factory
   image on the host, and document the recovery path in the same commit as any change that
   can brick a boot.
3. **Assume TF-A is still there and still in charge.** Anything at EL3 is out of scope. If
   something only works when we ask EL3 nicely, that is a finding — write it down.

---

## The observation mechanism

This is the part that makes the project worth doing. Get it right.

Give the guest a stage-2 address space with the device MMIO region **unmapped**. Every guest
access faults to our VMM, which decodes the instruction, performs the access on the real
device, records it, and resumes the guest. The guest cannot tell, except through timing.

Each trace record carries at minimum: monotonic timestamp, vCPU, guest PC, faulting address,
access size, direction, value, and the guest's fault syndrome. Sequence numbers must be
monotonic across cores — an out-of-order trace is worse than no trace.

Related channels we also want visible:
- **SMCs** from the guest → trap, log, forward or deny by policy.
- **Interrupts** → log delivery, not just assertion.
- **DMA** → the hard one. Tensor uses Samsung SysMMU per-IP, not an Arm SMMU, and seL4 has
  no SysMMU support. Until that's solved, a device can scribble on memory without us seeing
  it. Treat every DMA-capable passthrough as unsound and say so in the docs. Do not quietly
  ship a trace that implies we saw everything.

Trace format lives in `docs/trace-format.md` and is versioned. Changing it is a breaking
change; bump the version and keep the old decoder.

---

## Reproducibility

An observation nobody can reproduce is an anecdote.

- Pinned toolchain via the Nix flake. If you need a tool, add it to the flake; do not tell me
  to `apt install` it.
- Pin exact commits for seL4, Microkit, libvmm, sDDF. No floating branches in the build.
- Every trace artifact is stamped with: device model and codename, factory image build ID,
  bootloader and baseband version, our git SHA, toolchain hash, and the DTB hash.
- Builds are byte-reproducible or there's an issue open explaining why not.
- If a bug only reproduces sometimes, that goes in `docs/known-flaky.md` with the conditions,
  not in a commit message nobody reads.

---

## Blobs and legality

This is interoperability reverse engineering. Keep it clean so it stays that way.

- **No proprietary blobs in the repo. Ever.** Not firmware, not vendor `.so` files, not
  factory images, not extracted DTBs containing vendor binary nodes. Fetch scripts + checksums
  only.
- Never commit anything pulled off a device without checking what's in it first.
- Document observed behaviour — registers, sequences, timings. Don't paste disassembly of
  vendor code into the repo.
- Keep the observation side (this repo) and any future clean reimplementation separable, so
  the provenance of a driver written from our traces is arguable.
- IMEI, serial numbers, and per-device keys never enter a trace file. Scrub in the tracer,
  not in post-processing.

---

## Working with me

- Read the DTS before proposing a memory map. If you're guessing, say "I'm guessing."
- Small, reviewable diffs. A port is a thousand small facts; don't bundle fifty of them.
- When something can't be verified without the hardware in hand, stop and tell me what to run
  on-device, exactly. Don't write speculative code around an unknown.
- Bare assertions about Tensor internals are worth nothing. Cite the DTS node, the Exynos
  driver, or the experiment.
- If I'm heading toward a design that will make traces unfaithful or unreproducible, say so
  plainly before writing the code, not after.
- Commit messages: imperative subject under 72 chars, body explains *why*. The body is where
  the prose goes, since it isn't going in the code.

---

## Don't

- Don't write the comments. Propose them.
- Don't add abstraction layers for hardware we don't have.
- Don't stub something to make a build pass without a `TODO(will):` and a mention in chat.
- Don't let a VMM silently emulate away a fault it doesn't understand. Log it loudly and
  stop the VM if that's what fidelity requires.
- Don't optimise the trace path for speed before it's correct.

