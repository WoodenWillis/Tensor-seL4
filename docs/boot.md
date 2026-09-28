# Boot

## Handoff state

| Property            | Value        | Confirmed by                                                                |
|---------------------|--------------|-----------------------------------------------------------------------------|
| Exception level     | EL2          | the maintainer's experience                                                 |
| Kernel load address | `0x80000000` | loader log `relocating from 0x0000000080001000` with payload at `+0x1000`, `fastboot boot`, BP1A.250505.005 |

## Building

    nix --extra-experimental-features 'nix-command flakes' develop -c make

produces `build/boot.img`.

## Booting a test image

`fastboot boot <boot.img>` boots an image from RAM without writing any
partition. It only works on an unlocked bootloader.

## Recovery

Hold power and volume down for 30 seconds. The phone returns to the bootloader
menu.
