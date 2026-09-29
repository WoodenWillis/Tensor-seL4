# Trace format

Current version: **0**. Changing anything below is a breaking change: bump the version, and keep the old decoder working.

Definitions: `include/trace/trace_v0.h` (C) and `tools/trace-decode.py` (host decoder). All integers are little-endian.

## Transport (v0)

The tracer PD writes to the serial console with `microkit_dbg_puts`. That output exists only in debug kernel configurations. Each unit is one line:

| Prefix | Payload | Meaning |
|---|---|---|
| `TRH0 ` | 672 lowercase hex characters (336 bytes) | Stream header. Sent once, before the first record. |
| `TRC0 ` | 128 lowercase hex characters (64 bytes) | One record. |

Other console output can appear on other lines. The decoder finds the prefix anywhere in a line. A line with a known prefix but the wrong payload length is an error, never skipped.

## Header (336 bytes)

| Offset | Size | Field | Contents |
|---|---|---|---|
| 0 | 8 | magic | `SEL4TRC\0` |
| 8 | 2 | version | 0 |
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

| Offset | Size | Field | Contents |
|---|---|---|---|
| 0 | 8 | seq | Global sequence number, starting at 0, with no gaps. Allocated atomically from the ring header, so it is monotonic across producers. |
| 8 | 8 | time | `CNTPCT_EL0` when the record was made |
| 16 | 8 | pc | Guest PC of the faulting instruction |
| 24 | 8 | addr | Guest-physical address accessed |
| 32 | 8 | value | Value read or written. 0 for unhandled accesses. |
| 40 | 4 | esr | Fault syndrome (ESR_EL2) as delivered by seL4 |
| 44 | 2 | vcpu | vCPU ID within the producer's VM |
| 46 | 1 | producer | ID of the VMM that produced the record |
| 47 | 1 | kind | 1 = MMIO |
| 48 | 1 | size | Access size in bytes |
| 49 | 1 | flags | For MMIO: bit 0 WRITE, bit 1 FORWARDED (performed on the real device), bit 2 UNHANDLED (not performed; the guest was stopped). Neither bit 1 nor bit 2 set means emulated by the VMM. |
| 50 | 14 | reserved | 0 |

## Guarantees and limits (v0)

- **Nothing is dropped.** If the ring is full, the producer waits for the tracer to drain it. The guest is stalled for that time and keeps its place in the sequence.
- **A gap or reordering in `seq` is always an error.** The decoder reports it and exits non-zero.
- **Only one producer** per ring (one VMM). The sequence counter is already shared, so more producers can be added without changing the record format.
- **Only MMIO traps are recorded.** SMCs and interrupt delivery are not yet traced.
- **No DMA is observed.** A trace does not show memory accesses made by devices.
- **No identifier scrubbing yet.** The records produced so far come from the test harness and contain no device identifiers. Scrubbing will happen in the tracer, before any driver that can see identifiers is observed.
- **The tracer is the only writer to the console while the system runs.** Everything the VMM prints, including libvmm's messages and register dumps, goes into a separate console ring (`include/trace/console_ring.h`), which the tracer prints after each batch of records. The kernel's own debug messages, and libmicrokit's direct `microkit_dbg_puts` calls, still go straight to the UART and can land inside a line. A record damaged that way is reported as an error, never skipped.
- **Console text isn't ordered relative to records.** The tracer drains the trace ring before the console ring each time it wakes, so VMM messages can appear after records that were produced later. Order comes from `seq` only.

## Decoding

```
tools/trace-decode.py serial.log --console-tx 0x10870020
```

`--console-tx` reassembles guest output from writes to that UART TX register.
