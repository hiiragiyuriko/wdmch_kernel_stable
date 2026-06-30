# Mainline Linux for the WD My Cloud Home (RTD1295 "Monarch")

Mainline **Linux 6.18** for the Western Digital My Cloud Home single-bay NAS —
the Realtek **RTD1295** ("Monarch") board — ported off WD's vendor
`kernel-4.9.330` tree. It boots a stock **Debian arm64** rootfs from SATA or USB.

> 🤖 **Vibe-coded with Claude Opus 4.8.** Just for fun. **Use at your own risk.**

---

## What this is

The My Cloud Home ships a Realtek RTD1295 (4× Cortex-A53, 1 GiB DDR) running an
ancient, never-to-be-updated 4.9 vendor kernel. This tree rebases the
board-specific bits onto mainline's existing RTD1295 support and adds the
Monarch-specific pieces, so you can run a modern kernel and a real distro on it.

It's a hobby port. Nothing here is official, supported, or blessed by WD,
Realtek, Debian, or anyone else.

## Hardware status

| Subsystem | State |
|---|---|
| SMP (4× Cortex-A53) | ✅ |
| Clocks / reset | ✅ |
| Serial console (8250, IRQ-driven) | ✅ |
| Ethernet — `r8169soc` GMAC | ✅ 1 Gbps + LEDs |
| SATA / AHCI | ✅ 6 Gbps |
| USB — dwc3 host (xHCI) | ✅ cold-init, no U-Boot `usb start` needed |
| I²C + g2227 PMIC + regulators | ✅ |
| Pin control / GPIO | ✅ |
| CPU DVFS (cpufreq-dt) | ✅ |
| Thermal (sensor + cpufreq cooling) | ✅ |
| Power domains (GPU/VE/NAT off) | ✅ |
| Watchdog / reboot | ✅ |
| PWM front LED | ✅ |
| SPI-NOR boot flash (MTD) | ✅ |
| RTC | ⚠️ keeps no time in hardware — see quirks |

**Not ported** (on purpose): GPU 3D / video engines / HDMI / audio (it's a NAS),
PCIe (disabled on this board), eMMC (holds the original WD OS).

## Building

Two defconfigs, same Monarch hardware support:

* `rtd1295_wd_defconfig` — lean all-builtin rescue / bring-up kernel.
* `rtd1295_wd_debian_defconfig` — the run-Debian kernel (Debian arm64 config +
  this board, single-platform, ~2000 modules).

```sh
export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
cd linux
DEFCONFIG=rtd1295_wd_debian_defconfig OUT=../out MODOUT=../modules ./build-wdmch.sh
```

Outputs under `out/`: `sata.uImage` (raw `Image` + pad) and `rescue.sata.dtb`.
Loadable modules land in `modules/lib/modules/<kver>/`.

**Fixed constraint:** `text_offset = 0x280000` — U-Boot loads the `Image` at
physical `0x00280000` and the header must match.

## Quirks worth knowing

* **The RTC doesn't tick.** The SoC RTC register file is alive (you can read and
  write it) but its hardware counter never advances — WD's own driver sidesteps
  this by storing time in flash. Use `fake-hwclock` + `systemd-timesyncd` (NTP)
  and you'll never notice.
* **USB now self-initializes.** Earlier the dwc3 port only worked if U-Boot had
  run `usb start`; the kernel now does the clock / reset / SRAM-power bring-up
  itself, so USB works booting from SATA too.

## ⚠️ Disclaimer

This is an unofficial, for-fun port, largely written by an AI pair-programmer.
It pokes undocumented SoC registers reverse-engineered from vendor code.

* **No warranty. No support. Use entirely at your own risk.**
* It can brick your device, eat your data, or do nothing useful at all.
* You are responsible for your own backups and for not erasing the boot flash.
* Not affiliated with or endorsed by Western Digital, Realtek, or Debian.

If it breaks, you get to keep both pieces. 🙂
