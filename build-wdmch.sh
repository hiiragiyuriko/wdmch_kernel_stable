#!/bin/bash
# Build + package the WD My Cloud Home (RTD1295 Monarch) bring-up artifacts.
#
# Produces, under $OUT (default ../out):
#   sata.uImage                      raw arm64 Image + 512 KiB zero pad
#                                    (text_offset=0x280000, loaded by U-Boot
#                                     at physical 0x00280000)
#   rescue.sata.dtb                  rtd1295-wd-monarch dtb, padded to 1 MiB
#   rescue.root.sata.cpio.gz_pad.img busybox initramfs, gzip, padded to 4 MiB
#                                    (loaded by U-Boot at 0x02200000)
#
# Requires: aarch64-linux-gnu- toolchain, and a prebuilt static arm64 busybox
# at $BUSYBOX (default ../build/busybox-1.36.1/busybox).
set -e

export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-

KDIR=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$KDIR/../out}
BUSYBOX=${BUSYBOX:-$KDIR/../build/busybox-1.36.1/busybox}
INITRD_PAD=$((4 * 1024 * 1024))   # ROOTFS_NORMAL_SIZE 0x400000
DTB_PAD=$((1024 * 1024))

mkdir -p "$OUT"
cd "$KDIR"

echo "==> Configuring"
make rtd1295_wd_defconfig

echo "==> Building Image + dtbs"
make -j"$(nproc)" Image dtbs DTC_FLAGS="-p 8192"

echo "==> Packaging sata.uImage (Image + 512K pad)"
cp arch/arm64/boot/Image "$OUT/sata.uImage"
dd if=/dev/zero of="$OUT/sata.uImage" conv=notrunc oflag=append bs=1k count=512 status=none

echo "==> Packaging rescue.sata.dtb (padded to 1M)"
cp arch/arm64/boot/dts/realtek/rtd1295-wd-monarch.dtb "$OUT/rescue.sata.dtb"
truncate -s "$DTB_PAD" "$OUT/rescue.sata.dtb"

echo "==> Building busybox initramfs"
[ -x "$BUSYBOX" ] || { echo "missing static arm64 busybox at $BUSYBOX"; exit 1; }
SPEC=$(mktemp)
{
  echo "dir /dev 755 0 0"
  echo "nod /dev/console 600 0 0 c 5 1"
  echo "nod /dev/null 666 0 0 c 1 3"
  echo "nod /dev/zero 666 0 0 c 1 5"
  echo "nod /dev/tty 666 0 0 c 5 0"
  echo "nod /dev/ttyS0 660 0 0 c 4 64"
  echo "nod /dev/mem 640 0 0 c 1 1"
  for d in bin sbin etc proc sys tmp mnt root usr usr/bin usr/sbin; do
    echo "dir /$d 755 0 0"
  done
  echo "file /bin/busybox $BUSYBOX 755 0 0"
  echo "file /init $KDIR/../build/initramfs/root/init 755 0 0"
  # applet symlinks
  for app in $(qemu-aarch64-static "$BUSYBOX" --list-full); do
    [ "$app" = "bin/busybox" ] && continue
    echo "slink /$app /bin/busybox 777 0 0"
  done
} > "$SPEC"
usr/gen_init_cpio "$SPEC" > "$OUT/.initramfs.cpio"
gzip -9 -n -c "$OUT/.initramfs.cpio" > "$OUT/rescue.root.sata.cpio.gz_pad.img"
truncate -s "$INITRD_PAD" "$OUT/rescue.root.sata.cpio.gz_pad.img"
rm -f "$OUT/.initramfs.cpio" "$SPEC"

echo "==> Done. Artifacts in $OUT:"
ls -la "$OUT"
echo "==> sata.uImage header:"
echo "    text_offset: $(od -A n -j8 -N8 -t x1 "$OUT/sata.uImage")"
echo "    flags:       $(od -A n -j24 -N8 -t x1 "$OUT/sata.uImage")"
