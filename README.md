# Tensor-seL4

Porting the Google Pixel 9 Pro (`caiman`, Tensor G4 / `zumapro`) to seL4 Microkit, then using seL4 as a hypervisor to run isolated VMs on the phone's hardware. 

## Why do this?

Because we should be able to audit and modify what is running on our devices.

By isolating the vendor's proprietary blobs and running them with seL4 as hypervisor we can intercept:

- every MMIO access
- every interrupt
- every SMC
- every DMA setup

As far as it knows it's just running on the phone like normal.

## Goal

Give the open source community a controlled, reproducible environment to study blob's behaviors for reverse engineering purposes.

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

## Contributing

If you're into this kind of thing—reverse engineering, seL4, Tensor, or just wanting to know what's really running on your phone—issues and PRs are welcome.
