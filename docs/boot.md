# Boot

## Handoff state

| Property            | Value | Confirmed by                     |
|---------------------|-------|----------------------------------|
| Exception level     | EL2   | the maintainer's experience      |

## Booting a test image

`fastboot boot <boot.img>` boots an image from RAM without writing any
partition. It only works on an unlocked bootloader.

## Recovery

Hold power and volume down for 30 seconds. The phone returns to the bootloader
menu.
