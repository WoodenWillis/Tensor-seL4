# Trace format

Current version: **3**. Changing anything below is a breaking change: bump the version, and keep the old decoder working. The decoder (`tools/trace-decode.py`) reads versions 0 to 3.

Definitions: `include/trace/trace.h` (C) and `tools/trace-decode.py` (host decoder). All integers are little-endian.

## Transport

Records are **not streamed.** The tracer drains the trace ring into an 8 MiB archive in RAM (`include/trace/trace_archive.h`, 131071 records) and prints nothing until the host sends `trace-dump` (see "Host commands"). A dump prints the whole archive so far, then `TRACER|INFO: dumped N records, M not archived (archive full)`.

Consequences:
- **A crash or reset loses everything not yet dumped.** The archive lives only in RAM.
- **When the archive is full, new records are counted, not stored.** Older records are never overwritten, so a dump is always a gapless prefix of the trace.
- **While a dump is printing, the tracer doesn't drain the ring.** If it fills, producers wait and the guest stalls, as usual.

A dump is written to the serial console with `microkit_dbg_puts`. That output exists only in debug kernel configurations. Each unit is one line, and the digit in the prefix is the format version:

| Prefix | Payload | Meaning |
|---|---|---|
| `TRH3 ` | 672 lowercase hex characters (336 bytes) | Stream header. Sent before the first record of a dump and again every 64 records. All copies are identical. |
| `TRC3 ` | 128 lowercase hex characters (64 bytes) | One record. |

The header is repeated because it's the one line whose loss would make the whole trace undecodable. Kernel debug output isn't serialised across cores. On the first v2 boot, the monitor's `MON|INFO: Microkit Monitor started!` on core 0 interleaved character by character with the tracer's first header on core 1. The decoder uses the first valid header copy, holds any records that arrive before it, and treats a later copy that differs as an error.

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
| 46 | 1 | producer | Who produced the record: 0 the VMM, 1 `uartrx` (host commands) |
| 47 | 1 | kind | 1 MMIO, 2 SMC_ENTER, 3 SMC_REGS, 4 SMC_EXIT, 5 CMD, 6 GUEST |
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

### kind 5: CMD

One non-empty command line typed on the console (see "Host commands" below). Produced by `uartrx`. `pc`, `esr`, `vcpu` and `size` are 0.

| Field | Contents |
|---|---|
| addr | Verb: 0 unknown, 1 `ping`, 2 `trace-dump`, 3 `help`, 4 `guest-start`, 5 `guest-stop`, 6 `status` |
| value | The line's number: 1 for the first command typed after boot, then 2, 3, … |
| flags | Bit 0 ACCEPTED. Bit 2 REJECTED_VERB (unknown command). Exactly one is set. Bit 1 is unused. |

Empty lines produce no record.

### kind 6: GUEST

A change in the guest's lifecycle, produced by the VMM. It marks where one run ends and the next begins, so several runs in one boot can be told apart in a dump.

| Field | Contents |
|---|---|
| addr | Event: 1 STARTED, 2 STOPPED_BY_COMMAND, 3 STOPPED_BY_FAULT |
| value | Run number: 1 for the first `guest-start` after boot, incremented on each start |
| pc | STARTED: the entry point. STOPPED_BY_FAULT: the PC of the refused access or SMC. STOPPED_BY_COMMAND: 0. |

STARTED is recorded before the vCPU runs, so it precedes every record of that run. A refused command (`guest-start` while running, `guest-stop` while stopped) produces only its CMD record, so a trace shows it had no effect.

### SMC policy (`vmm/smc_policy.c`)

| Call | Action |
|---|---|
| PSCI (standard service, function < 0x1f) | Emulated by libvmm's virtual PSCI, since the VMM owns the guest's vCPUs. The guest therefore sees libvmm's PSCI version, not the firmware's. |
| `SMCCC_VERSION` (0x80000000) | Forwarded to EL3 |
| Anything else | Unhandled: traced, and the guest is stopped |

## Guarantees and limits

- **Nothing is dropped.** If the ring is full, the producer waits for the tracer to drain it. The guest is stalled for that time and keeps its place in the sequence.
- **A gap or reordering in `seq` is always an error.** The decoder reports it and exits non-zero.
- **Two producers share the ring:** the VMM and `uartrx`. A producer lock in the ring header covers each record from allocating `seq` to publishing it, and the timestamp is taken inside the lock, so `time` never decreases as `seq` increases, across producers.
- **MMIO traps and SMCs are recorded.** Interrupt delivery is not yet traced.
- **No DMA is observed.** A trace does not show memory accesses made by devices.
- **No identifier scrubbing yet.** The records produced so far come from the test harness and contain no device identifiers. Scrubbing will happen in the tracer, before any driver that can see identifiers is observed. For SMCs that includes arguments and results.
- **The tracer is the only writer to the console while the system runs.** Everything the VMM prints, including libvmm's messages and register dumps, goes into a separate console ring (`include/trace/console_ring.h`), and `uartrx` has one of its own. The tracer prints both after each batch of records. The kernel's own debug messages, and libmicrokit's direct `microkit_dbg_puts` calls, still go straight to the UART and can land inside a line. A record damaged that way is reported as an error, never skipped.
- **Console text isn't ordered relative to records.** The tracer drains the trace ring before the console ring each time it wakes, so VMM messages can appear after records that were produced later. Order comes from `seq` only.

## Version history

| Version | Change |
|---|---|
| 0 | MMIO records only (kind 1). Lines `TRH0` / `TRC0`. |
| 1 | Adds SMC_ENTER, SMC_REGS and SMC_EXIT (kinds 2–4). The record and header layouts are unchanged. Lines `TRH1` / `TRC1`. |
| 2 | Adds CMD (kind 5) and a second producer (`uartrx`, producer 1). Layouts unchanged. Lines `TRH2` / `TRC2`. |
| 3 | Adds GUEST (kind 6) and the guest-control verbs 4–6. Layouts unchanged. Lines `TRH3` / `TRC3`. |

## Host commands

`uartrx` is a small typed console on the same UART. It polls the receive FIFO, maps the UART read-only and touches only the receive registers.

- Type a command and press Enter. CR, LF and CRLF all end a line. Backspace works, and what you type is echoed. A `> ` prompt means it's ready.
- Commands: `help`, `ping` (answers `pong`), `trace-dump` (prints the archive), and the guest controls below. Guest commands are run by the VMM; the prompt comes back when the VMM has finished, or after 2 s with a warning.

The VMM no longer starts the guest at boot. It waits for `guest-start`:

| Command | not started | running | stopped (by command, by fault, or failed to start) |
|---|---|---|---|
| `guest-start` | start run 1 | refused: already running | fresh restart as run N+1 |
| `guest-stop` | refused, and prints the state | stop the vCPU | refused, and prints the state |
| `status` | prints the state, run number, and for a fault stop what stopped it (refused SMC, unhandled address, or vCPU exception with its HSR) and its PC. Once a guest has started, it also prints how many times the guest exited to the VMM this run and how long ago the last one was, per kind: memory access, SMC, WFI/WFE, sysreg, virtual-timer injection (VPPI event), vGIC maintenance (the guest's EOIs, from which libvmm acknowledges the timer), and other. The counts are console only; they aren't in the trace. Changes nothing. | | |

Every start is a fresh start, never a resume: guest RAM is zeroed and the image reloaded, the vCPU's EL1 system registers are reset (`vcpu_reset`), and every general register is rewritten. So each run begins from the same state. **Exception:** libvmm cannot reset the virtual GIC, so vGIC state from one run survives into the next. The harness never uses the GIC; this must be fixed before a guest that does.
- Each non-empty line gets a number (1, 2, 3, …) and a CMD record: ACCEPTED, or REJECTED_VERB for an unknown command, which is answered with `unknown command; type help`.
- If the UART reported receive errors while a line was typed, `UARTRX|WARN: receive errors on this line, UERSTAT …` is printed first. Bits: 0x1 overrun, 0x2 parity, 0x4 framing, 0x8 break.

From a script: `tools/sel4-cmd.py trace-dump` types one line.

## Decoding

```
tools/trace-decode.py serial.log --console-tx 0x10870020
```

`--console-tx` reassembles guest output from writes to that UART TX register.
