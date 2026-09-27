# Serial

The Pixel 9 Pro has no exposed UART header. The debug UART (`uart@10870000`) is
routed out over the USB-C port's SBU pins.

## Hardware

- Adapter: USB-Cereal, 0xDA, schematic revision 1 (2023-02-15), Apache-2.0.
  Design files: https://github.com/oxda/usb-cereal
- Line settings: 3000000 8N1.
- S1 position: a (1.8 V).
- Orientation: the adapter only works plugged in with its LEDs facing up.
- Host device node: `/dev/ttyUSB0`.

## Attaching

From the Nix dev shell:
```
picocom -b 3000000 /dev/ttyUSB0
```

From the schematic:

- The adapter is a USB-C pass-through (J2 plug to J1 receptacle) that taps the
  SBU pair. The SBU lines reach J1 through 0 Ω links (R1, R2).
- SBU1 (A8) and SBU2 (B8) go through an ESDR0502N (U4) to the RXD and TXD pins
  of a Silicon Labs CP2102N USB-UART bridge (U3).
- The CP2102N is on its own USB-C port (J3) facing the host, with 5.1 kΩ
  pull-downs on CC1 and CC2 (R18, R22).
- The CP2102N I/O rail (`VSBU`, into VIO) comes from a TPS77401 LDO (U2). Switch
  S1 selects its output: position b is 3.3 V, position a is 1.8 V.
- LEDs: D1 (green) on `TXLED`, D2 (yellow) on `RXLED`.

## Enabling UART output

1. Reboot the phone to the bootloader.
2. Unlock the bootloader, if it is not already unlocked:
   ```
   fastboot flashing unlock
   ```
3. Enable UART:
   ```
   fastboot oem uart enable
   ```
4. Set the baud rate:
   ```
   fastboot oem uart config 3000000
   ```

With UART enabled, both ABL and Android log to it.

The setting persists across reboots, reflashes and relocking the bootloader.
`fastboot oem` commands are refused on a locked device, so disabling UART on a
relocked phone requires unlocking it again first.

The DTS property `samsung,dbg-uart-baud = <115200>` does not reflect the rate in
use. The line runs at 3000000.

## Not yet recorded

- Which of SBU1/SBU2 carries the phone's TX.
