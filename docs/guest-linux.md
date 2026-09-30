# Linux guest

The Linux guest runs the phone's own GKI kernel, taken from the factory image of the build the phone runs. Vendor modules are built against that kernel's KMI, so it is the kernel every observation of a vendor driver needs.

## Source

| | |
|---|---|
| Factory image | `caiman-bp1a.250505.005-factory-f158d94e.zip` |
| URL | `https://dl.google.com/dl/android/aosp/caiman-bp1a.250505.005-factory-f158d94e.zip` |
| Factory zip sha256 | `f158d94e5e9209ed9b22a4a4d81b1ae70a1631410adf75d2a2a8297d02d7363a` |
| Bootloader in the zip | `ripcurrentpro-15.2-13237002` (same as the phone) |
| Baseband in the zip | `g5400c-241205-250127-b-12971716` (same as the phone) |

Nothing from the factory image is committed. `tools/extract-gki.py build/guest/Image` looks for the zip in `deps/`, downloads it from the URL above if it's missing, checks both checksums, and writes the kernel:

1. Reads `boot.img` from the `image-caiman-bp1a.250505.005.zip` stored inside the factory zip, without extracting the 3.5 GB inner zip.
2. Takes the kernel from `boot.img` (header v4, 4 KiB pages). It's an LZ4 legacy frame, and the ramdisk and cmdline in `boot.img` are empty; ABL supplies them.
3. Decompresses it with `lz4` and checks the result.

## Kernel

| | |
|---|---|
| Version | `6.1.99-android14-11-gd7dac4b14270-ab12946699` (android14-6.1 GKI) |
| Image sha256 | `c822b5f4675020005a028eececa1f6e801e7d1a0cffa2a68cdb8eaa471975530` |
| Image header | arm64 `Image`, text_offset 0, image_size `0x2250000`, flags `0xa` |

From its embedded config (`IKCFG_ST`):

| Option | Value | Consequence |
|---|---|---|
| `SERIAL_SAMSUNG`, `SERIAL_SAMSUNG_CONSOLE`, `SERIAL_EARLYCON` | y | `earlycon=exynos4210,mmio32,0x10870000`, which the stock cmdline also uses, prints through the UART the VMM emulates |
| `BLK_DEV_INITRD`, `RD_GZIP`, `RD_LZ4` | y | the guest can boot our own initramfs |
| `DEVTMPFS` | not set | the initramfs must contain `/dev/console` itself |
| `ARM_GIC_V3`, `ARM_PSCI_FW` | y | works with libvmm's vGICv3 and emulated PSCI |
| `MODULE_SIG_FORCE` | not set | vendor modules can be loaded |
| `VIRTIO_MMIO`, `VIRTIO_CONSOLE` | not set | no virtio console without a module |

## Building

`make GUEST=linux` builds a `build/boot.img` whose VMM boots this kernel. `make` alone still builds the bare-metal harness guest. Each guest has its own build directory (`build/vmm-harness`, `build/vmm-linux`), and both write the same `build/boot.img`, so the image on disk is whichever was built last.

The Linux VM is described in `guests/linux/`:

| | |
|---|---|
| Guest RAM | 256 MiB at GPA `0x80000000`, kernel at its start (text_offset 0) |
| Initramfs | GPA `0x8d000000`: `/dev` (dir), `/dev/console` (c 5,1), `/dev/kmsg` (c 1,11) and `/init`, built by `tools/mkcpio.py` |
| DTB | GPA `0x8f000000`, from `caiman-vm.dts.S`, with the initrd bounds filled in from the cpio size |
| Devices in the DTB | one CPU, PSCI (smc), GICv3 (libvmm vGIC at the physical GIC's addresses), arch timer |
| UART | no DT node; only `earlycon=exynos4210,mmio32,0x10870000` prints, through the VMM's UART emulation |
| cmdline | `earlycon=exynos4210,mmio32,0x10870000 keep_bootcon rdinit=/init loglevel=8 nokaslr arm64.nosve arm64.nosme arm64.nomte arm64.nopauth` |

### Differences from the real CPU

The guest reads the real ID registers (seL4 doesn't trap them), but it doesn't get every feature they advertise:

| Feature | Real CPU | Guest | How | Found by |
|---|---|---|---|---|
| SVE | `ID_AA64PFR0_EL1.SVE` = 1 (`0x1201111123111111`) | none | `arm64.nosve`: in this kernel `init_cpu_features()` probes `ZCR_EL1` only if the override-applied `ID_AA64PFR0_EL1` shows SVE (`cpufeature.c`, `idreg-override.c` at `d7dac4b14270`) | Without it the guest traps with EC 0x19 (HSR `0x66000000`) on `msr ZCR_EL1` at `0xffffffc0080180cc`. `CPTR_EL2.TZ` is set while the guest runs: seL4 changes only `CPTR_EL2.TFP`, and in the hypervisor configuration the loader leaves `CPTR_EL2` as ABL set it. seL4 doesn't save or restore SVE state, so giving guests SVE would be a kernel change |
| MTE | `ID_AA64PFR1_EL1.MTE` ≥ 2; the guest logs `detected: Memory Tagging Extension` and `Asymmetric MTE Tag Check Fault` | none | `arm64.nomte` (`id_aa64pfr1.mte=0`): both capabilities match through `has_cpuid_feature()`, and the local-CPU read `__read_sysreg_by_encoding()` applies the override too (`cpufeature.c` at `d7dac4b14270`) | Without it the guest traps with EC 0x18 (HSR `0x623c0520`) on `msr GCR_EL1` in `mte_cpu_setup()` at `0xffffffc00803c74c`. seL4's `HCR_VCPU` doesn't set `HCR_EL2.ATA`, so the MTE control registers trap, and seL4 switches no MTE state. The stock kernel on the phone does enable MTE at this point, so a vendor driver in the guest runs without it |
| SME | `ID_AA64PFR1_EL1.SME` = 0 (guest log `SYS_ID_AA64PFR1_EL1[27:24]: already set to 0`) | none | `arm64.nosme` (`id_aa64pfr1.sme=0`), which `arm64.nosve` also implies. Nothing to hide on this CPU; the flag states the intent | Not observed as a trap; added with the other feature flags |
| Pointer authentication | `ID_AA64ISAR1_EL1`/`ISAR2_EL1` advertise it; guest log `detected: Address authentication (architected QARMA3 algorithm)` | none | `arm64.nopauth` (`id_aa64isar1.{gpi,gpa,api,apa}=0 id_aa64isar2.{gpa3,apa3}=0`) | Not observed as a trap; added before the guest reached it. seL4's `HCR_VCPU` doesn't set `HCR_EL2.API` or `HCR_EL2.APK`, so the guest's PAuth instructions and key registers would trap. The stock kernel uses PAuth, so this is a real difference from the phone |
| PSCI | TF-A reports 1.1 | libvmm reports 1.2 | libvmm emulates PSCI for its vCPUs | Guest log `psci: PSCIv1.2 detected in firmware` |

`/init` (`guests/linux/init.c`) is freestanding and uses raw syscalls: it writes `init: hello from userspace` to `/dev/kmsg` and then blocks in `ppoll` forever, so the guest stays running until `guest-stop`. If opening or writing `/dev/kmsg` fails, it exits, so the kernel panics with `Attempted to kill init!` and prints the error rather than going quiet.

It writes to `/dev/kmsg` and not to fd 1 because the kernel's built-in `CONFIG_CMDLINE` (extended, not replaced, by ours) starts with `console=ttynull`. The boot log shows `printk: console [ttynull0] enabled`, so `/dev/console` is the null tty and anything written to it is discarded. The earlycon bootconsole that prints the kernel log (`keep_bootcon`) has no tty. A line written to `/dev/kmsg` becomes a kernel log record and goes out through the bootconsole like any `printk`.

Once `/init` blocks, the kernel has nothing to run and sits in `cpu_do_idle()` (`dsb sy; wfi` at `0xffffffc008fe8c80` in this Image). Every `wfi` traps to the VMM (seL4 sets `HCR_EL2.TWI`/`TWE` for guests), and libvmm answers without advancing the PC, so an idle guest keeps trapping WFI until an interrupt is pending.

The guest has no device mappings except its RAM. Every other access, and every SMC, traps to the VMM. Whatever the VMM doesn't handle stops the guest and is reported by `status`.
