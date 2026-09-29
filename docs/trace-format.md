# Trace format

Current version: **1**. Changing anything below is a breaking change: bump the version, and keep the old decoder working. The decoder (`tools/trace-decode.py`) reads versions 0 and 1.

Definitions: `include/trace/trace.h` (C) and `tools/trace-decode.py` (host decoder). All integers are little-endian.

## Transport

The tracer PD writes to the serial console with `microkit_dbg_puts`. That output exists only in debug kernel configurations. Each unit is one line, and the digit in the prefix is the format version:

| Prefix | Payload | Meaning |
|---|---|---|
| `TRH1 ` | 672 lowercase hex characters (336 bytes) | Stream header. Sent once, before the first record. |
| `TRC1 ` | 128 lowercase hex characters (64 bytes) | One record. |

Other console output can appear on other lines. The decoder finds the prefix anywhere in a line. A line with a known prefix but the wrong payload length is an error, never skipped. So is a prefix digit that doesn't match the header's version.

## Header (336 bytes)

| Offset | Size | Field | Contents |
|---|---|---|---|
| 0 | 8 | magic | `SEL4TRC\0` |
| 8 | 2 | version | 1 |
| 10 | 2 | record_size | 64 |
| 12 | 4 | cntfrq | `CNTFRQ_EL0`: ticks per second of the record timestamps |
| 16 | 16 | codename | Device codename, e.g. `caiman` |
| 32 | 32 | build_id | Factory image build ID |
| 64 | 48 | bootloader | Bootloader version |
| 112 | 48 | baseband | Baseband version |
| 160 | 48 | git_sha | This repo's HEAD. Suffixed `-dirty` if `git status --porcelain` is not empty (untracked files count). |
| 208 | 64 | toolchain_sha256 | sha256 of `flake.lock`, in hex |
| 272 | 64 | dtb_sha256 | sha256 of the device DTB in `hw/dts/`, in hex |

Strings are NUL-padded ASCII. They're generated at build time by `tools/gen-trace-stamp.sh` from `hw/dts/<device>-<build>.txt`. The build fails if the DTB's hash doesn't match the one recorded in that file.

## Record (64 bytes)

Every record has the same layout. How `addr`, `value`, `size` and `flags` are interpreted depends on `kind`.

| Offset | Size | Field | Contents |
|---|---|---|---|
| 0 | 8 | seq | Global sequence number, starting at 0, with no gaps. Allocated atomically from the ring header, so it is monotonic across producers. |
| 8 | 8 | time | `CNTPCT_EL0` when the record was made |
| 16 | 8 | pc | Guest PC of the trapping instruction |
| 24 | 8 | addr | Depends on kind |
| 32 | 8 | value | Depends on kind |
| 40 | 4 | esr | Syndrome (ESR_EL2) as delivered by seL4 |
| 44 | 2 | vcpu | vCPU ID within the producer's VM |
| 46 | 1 | producer | ID of the VMM that produced the record |
| 47 | 1 | kind | 1 MMIO, 2 SMC_ENTER, 3 SMC_REGS, 4 SMC_EXIT |
| 48 | 1 | size | Depends on kind |
| 49 | 1 | flags | Depends on kind |
| 50 | 14 | reserved | 0 |

### kind 1: MMIO

One guest access to an address the guest has no mapping for.

| Field | Contents |
|---|---|
| addr | Guest-physical address accessed |
| value | Value read or written. 0 if UNHANDLED. |
| size | Access size in bytes |
| flags | Bit 0 WRITE. Bit 1 FORWARDED (performed on the real device). Bit 2 UNHANDLED (not performed; the guest was stopped). Neither bit 1 nor bit 2 set means the VMM emulated it. |

### kinds 2–4: SMC

A guest `smc` instruction (seL4 traps it: HCR_EL2.TSC). One call produces a run of consecutive `seq` numbers, all with the same `pc` and `esr`:

1. **SMC_ENTER:** `addr` = x0 (the function ID), `value` = x1.
2. **SMC_REGS × 3, inputs:** `size` = n (2, 4 or 6), `addr` = x*n*, `value` = x*n+1*. `flags` bit 0 clear.
3. **SMC_EXIT:** `addr` = x0, `value` = x1 as returned to the guest. `flags`: bit 1 FORWARDED (executed by EL3 firmware). Bit 2 UNHANDLED (refused by policy; the guest was stopped, and `addr`/`value` are 0). Neither set means the VMM emulated it.
4. **SMC_REGS × 3, outputs:** as in step 2, but for x2–x7 on return, with `flags` bit 0 set. These are omitted after an UNHANDLED exit.

Registers x0–x7 are recorded. That's everything seL4's SMC forwarding passes to and from EL3.

### SMC policy (`vmm/smc_policy.c`)

| Call | Action |
|---|---|
| PSCI (standard service, function < 0x1f) | Emulated by libvmm's virtual PSCI, since the VMM owns the guest's vCPUs. The guest therefore sees libvmm's PSCI version, not the firmware's. |
| `SMCCC_VERSION` (0x80000000) | Forwarded to EL3 |
| Anything else | Unhandled: traced, and the guest is stopped |

## Guarantees and limits

- **Nothing is dropped.** If the ring is full, the producer waits for the tracer to drain it. The guest is stalled for that time and keeps its place in the sequence.
- **A gap or reordering in `seq` is always an error.** The decoder reports it and exits non-zero.
- **Only one producer** per ring (one VMM). The sequence counter is already shared, so more producers can be added without changing the record format.
- **MMIO traps and SMCs are recorded.** Interrupt delivery is not yet traced.
- **No DMA is observed.** A trace does not show memory accesses made by devices.
- **No identifier scrubbing yet.** The records produced so far come from the test harness and contain no device identifiers. Scrubbing will happen in the tracer, before any driver that can see identifiers is observed. For SMCs that includes arguments and results.
- **The tracer is the only writer to the console while the system runs.** Everything the VMM prints, including libvmm's messages and register dumps, goes into a separate console ring (`include/trace/console_ring.h`), which the tracer prints after each batch of records. The kernel's own debug messages, and libmicrokit's direct `microkit_dbg_puts` calls, still go straight to the UART and can land inside a line. A record damaged that way is reported as an error, never skipped.
- **Console text isn't ordered relative to records.** The tracer drains the trace ring before the console ring each time it wakes, so VMM messages can appear after records that were produced later. Order comes from `seq` only.

## Version history

| Version | Change |
|---|---|
| 0 | MMIO records only (kind 1). Lines `TRH0` / `TRC0`. |
| 1 | Adds SMC_ENTER, SMC_REGS and SMC_EXIT (kinds 2–4). The record and header layouts are unchanged. Lines `TRH1` / `TRC1`. |

## Decoding

```
tools/trace-decode.py serial.log --console-tx 0x10870020
```

`--console-tx` reassembles guest output from writes to that UART TX register.
