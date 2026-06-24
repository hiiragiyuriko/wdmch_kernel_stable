# WD My Cloud Home (RTD1295 "Monarch") — Linux 6.18 bring-up

This branch ports the low-level support needed to boot mainline Linux 6.18 on
the Western Digital My Cloud Home NAS (Realtek RTD1295, "Monarch" platform),
replacing the abandoned Realtek 4.9.330 vendor kernel.

## Strategy

Mainline already carries the RTD1295 core support (CPU/PSCI, GIC-400, arch
timer, `osc27M` fixed clock, dw-apb UART, reset-simple, watchdog). Rather than
forward-porting the large Realtek 4.9 downstream patch set, this work builds on
that upstream base and adds only what the board needs, starting with a serial
console and an externally loaded initramfs. Higher-level peripherals (clock
controller, GMAC, SATA, USB, regulators/PMIC) are layered on in later commits.

## What is in this series

1. `arm64: add CONFIG_ARM64_TEXT_OFFSET for fixed Image load offset`
   The vendor U-Boot reads the legacy `text_offset` field of the arm64 Image
   header and expects the fixed value **0x280000**. Mainline writes 0, so the
   kernel would be loaded at the wrong address and never start. The option
   restores a configurable offset and, when set, clears the PHYS_BASE header
   flag so the loader honours the fixed offset — reproducing the stock header
   (`text_offset=0x280000`, `flags` bit 3 = 0).

2. `arm64: dts: realtek: add WD My Cloud Home (RTD1295 Monarch) board`
   `rtd1295-wd-monarch.dts`: 1 GiB RAM, UART0 (ttyS0) console, earlycon, raised
   loglevel, printk timestamps, and the ramdisk window at 0x02200000.

3. `arm64: configs: add rtd1295_wd_defconfig bring-up config`
   Lean config tuned to keep the Image small (~8 MiB) so it fits below the
   ramdisk, with verbose timestamped logging.

## Memory / boot layout (from the 4.9 vendor DTS)

| Region            | Address       | Notes                                  |
|-------------------|---------------|----------------------------------------|
| Kernel Image      | 0x00280000    | `text_offset`, U-Boot `booti`          |
| ACPU firmware     | 0x01b00000    | reserved (`/memreserve/`)              |
| RPC ringbuf       | 0x01ffe000    | reserved                               |
| **Ramdisk**       | 0x02200000    | `rescue.root.sata.cpio.gz_pad.img`, 4 MiB |
| DRAM size         | 0x40000000    | 1 GiB                                   |

## Build

```sh
export CROSS_COMPILE=aarch64-linux-gnu- ARCH=arm64
./build-wdmch.sh
```

Outputs (in `../out`):

* `sata.uImage` — raw arm64 `Image` + 512 KiB zero pad (despite the name it is
  **not** a U-Boot uImage; it is a bare Image, matching the vendor convention).
* `rescue.sata.dtb` — board dtb padded to 1 MiB.
* `rescue.root.sata.cpio.gz_pad.img` — static busybox initramfs, gzip, padded
  to 4 MiB; U-Boot loads it to 0x02200000.

## Console

```
earlycon=uart8250,mmio32,0x98007800 console=ttyS0,115200n8 \
    loglevel=8 ignore_loglevel printk.time=1 init=/init
```

UART0 lives in the `iso` syscon at 0x98007000 + 0x800 = **0x98007800**.
