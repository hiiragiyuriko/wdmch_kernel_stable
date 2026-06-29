#!/bin/bash
# Build the WD My Cloud Home (RTD1295 Monarch) kernel artifacts.
#
# Produces, under $OUT (default ../out):
#   sata.uImage       raw arm64 Image + 512 KiB zero pad (text_offset=0x280000,
#                     loaded by U-Boot at physical 0x00280000)
#   rescue.sata.dtb   rtd1295-wd-monarch dtb, padded to 1 MiB
#
# And installs the loadable modules under $MODOUT (default ../modules):
#   $MODOUT/lib/modules/<kver>/...
#
# It does NOT build a busybox initramfs and does NOT touch ../rootfs.
#
# Requires the aarch64-linux-gnu- cross toolchain.
set -e

export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-

KDIR=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$KDIR/../out}
MODOUT=${MODOUT:-$KDIR/../modules}
DTB_PAD=$((1024 * 1024))

mkdir -p "$OUT"
cd "$KDIR"

echo "==> Configuring"
make rtd1295_wd_defconfig

echo "==> Building Image + dtbs + modules"
make -j"$(nproc)" Image dtbs modules DTC_FLAGS="-p 8192"

echo "==> Packaging sata.uImage (Image + 512K pad)"
cp arch/arm64/boot/Image "$OUT/sata.uImage"
dd if=/dev/zero of="$OUT/sata.uImage" conv=notrunc oflag=append bs=1k count=512 status=none

echo "==> Packaging rescue.sata.dtb (padded to 1M)"
cp arch/arm64/boot/dts/realtek/rtd1295-wd-monarch.dtb "$OUT/rescue.sata.dtb"
truncate -s "$DTB_PAD" "$OUT/rescue.sata.dtb"

echo "==> Installing modules to $MODOUT"
rm -rf "$MODOUT/lib/modules"
mkdir -p "$MODOUT"
make -j"$(nproc)" INSTALL_MOD_PATH="$MODOUT" INSTALL_MOD_STRIP=1 modules_install

echo "==> Done."
echo "Artifacts in $OUT:"
ls -la "$OUT"/sata.uImage "$OUT"/rescue.sata.dtb
echo "    text_offset: $(od -A n -j8 -N8 -t x1 "$OUT/sata.uImage")"
echo "    flags:       $(od -A n -j24 -N8 -t x1 "$OUT/sata.uImage")"
echo "Modules in $MODOUT:"
ls -d "$MODOUT"/lib/modules/*/ 2>/dev/null
find "$MODOUT" -name '*.ko' | wc -l | sed 's/^/    .ko count: /'
