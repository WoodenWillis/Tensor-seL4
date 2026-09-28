# Tensor-seL4

Porting the Google Pixel 9 Pro (`caiman`, Tensor G4 / `zumapro`) to seL4 Microkit, then using seL4 as a hypervisor to run isolated VMs on the real hardware. Not an emulator, not QEMU—the actual phone.

## Why?

Because Android vendor stacks are a black box. The proprietary blobs talk straight to the hardware and we're basically supposed to take their word for whatever they're doing.

This project flips that around. The vendor stack runs *inside* a VM, and seL4 sits outside it watching everything it does:

- every MMIO access
- every interrupt
- every SMC
- every DMA setup

From outside the VM the blob can't see us and it can't lie to us. As far as it knows it's just running on the phone like normal.

## Goal

Give the open source community a controlled, reproducible environment to study what the vendor stack actually does on real Tensor hardware—not what it reports about itself.

## Target

| | |
|---|---|
| Device | Google Pixel 9 Pro |
| Codename | `caiman` |
| SoC | Tensor G4 (`zumapro`) |
| Kernel | seL4 Microkit |
| Role | seL4 as hypervisor, vendor stack in a VM |

## How it works

Pretty simple on paper:

1. seL4 Microkit runs on the Pixel 9 Pro hardware
2. seL4 acts as the hypervisor
3. the proprietary Android vendor stack runs in an isolated VM on top
4. every MMIO access, interrupt, SMC and DMA setup it performs gets watched from outside the VM

The on paper part is doing a lot of heavy lifting

## Contributing

If you're into this kind of thing—reverse engineering, seL4, Tensor, or just wanting to know what's really running on your phone—issues and PRs are welcome.
