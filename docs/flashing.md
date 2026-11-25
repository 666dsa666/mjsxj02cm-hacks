# Flashing

Only the ROOTFS partition (mtd2, offset `0x260000`, length `0x750000`) is rewritten. U-Boot,
kernel, DATA, CONFIG and FACTORY are not touched. Partition map: [hardware.md](hardware.md#flash-layout).

## Entering U-Boot

1. Wire the UART ([hardware.md](hardware.md#uart)), open a terminal at 115200 8N1.
2. Start sending `Enter` repeatedly **before** applying power, and keep sending while the camera
   powers up. `bootdelay` is `0`: starting after power-on is too late and the kernel boots.
3. The prompt is `MStar #`. **HW**

## Backup first

DATA, CONFIG and FACTORY hold device-specific data (pairing, identity, keys). The stock rootfs
can be regenerated from the stock OTA package if a copy is available (see
[Recovery](#recovery)); the other partitions cannot.

Neither pre-flash backup method below has been run on the tested unit, and the only backup
that was taken (telnet dump) is possible only after the custom rootfs is flashed. A full SPI
dump with a programmer is the recommended backup before the first write.

1. **Full SPI dump with a CH341A programmer** (recommended) and a SOIC8 clip on the W25Q128
   (power the camera off, do not power it from the programmer and the supply at the same time).
   Read twice, compare both reads, keep two copies. The dump is also an independent check of
   the partition map. **UNTESTED**
2. **U-Boot readback of the current rootfs**: `sf probe 0`, then
   `sf read 0x21000000 0x260000 0x750000`. This only loads the partition into RAM. Saving the
   buffer to the SD card requires a write command in U-Boot (for example `fatwrite`), which may
   not be compiled in; without it this method produces no backup. **UNTESTED**

After the custom rootfs boots, dump every partition from a telnet shell (read-only operation):

```sh
for i in 0 1 2 3 4 5; do dd if=/dev/mtdblock$i of=/mnt/sdcard/mtd$i.bin bs=64k; done
md5sum /mnt/sdcard/mtd*.bin
```

**UNTESTED** as a single command. Copy the files off the SD card and keep them private: mtd3-mtd5
contain device secrets.

## Images

| Image | Content | Size | MD5 |
|---|---|---|---|
| `custom_rootfs_v2_min.img` | stock 4.3.7 rootfs + `/bin/telnetd` + `/etc/init.d/S60service`, padded to `0x750000` | 7,667,712 B | `4e56a6e250f542e12f4e908651bae74f` |
| `rootfs_stock_437.bin` (from the OTA package, see [Recovery](#recovery)) | vendor rootfs, extracted at exactly `0x750000` | 7,667,712 B | `5b01aba1e745ecbe765ea0093172887d` |

`custom_rootfs_v2_min.img` is produced by [`rootfs/build_v2.sh`](../rootfs/build_v2.sh):

```sh
sudo STOCK_IMG=/path/to/rootfs_stock_437.bin TS_IMG=/path/to/rootfs_new.bin bash rootfs/build_v2.sh
```

- run as root on a native Linux filesystem (ext4). Building on a filesystem that does not keep
  UNIX modes, owners and device nodes (for example a Windows drive mounted in WSL) produces an
  image with wrong permissions, no setuid `busybox` and no `/dev/console`; such an image did not
  boot. **HW**
- inputs: `STOCK_IMG` = `rootfs_stock_437.bin` (above), `TS_IMG` = tsunglung `rootfs_new.bin`
  (source of `telnetd` and `S60service`, see [firmware-history.md](firmware-history.md#tsunglung-uartu-boot-mod)).
  Without the variables the script reads `rootfs_stock_437.bin` and `rootfs_new.bin` from its
  own directory (`rootfs/`).
- `mksquashfs -comp xz -b 131072 -noappend -no-sparse -no-progress -mkfs-time <reference>`;
  the superblock flags (`0x00c0`) match the stock image and the build is reproducible.
- the MD5 above was obtained with squashfs-tools 4.5 (`mksquashfs version 4.5 (2021/07/22)`,
  recorded in `BUILD_LOG.txt`). `-mkfs-time` requires squashfs-tools 4.5 or later. The xz
  library version was not recorded; another squashfs-tools or xz version can produce a valid
  image with a different MD5.
- the script also builds `custom_rootfs_v2.img` (full tsunglung rootfs, rebuilt) and a padded copy
  of the original tsunglung image. Only `custom_rootfs_v2_min.img` is used by this project.
- outputs (`*.img`, `*.squashfs`, `*.md5`, `BUILD_LOG.txt`) are written next to the script; do
  not commit them.

The hook [`rootfs/S60service`](../rootfs/S60service) (from the tsunglung rootfs) starts the
normal runlevel, then `telnetd` (root, no password), then `/mnt/data/scripts/post_init.sh` if it
is executable.

## Flash the rootfs

Copy the image to the root of a FAT32 microSD card (single partition), insert it, enter U-Boot:

```
mmc rescan
fatload mmc 0:1 0x21000000 custom_rootfs_v2_min.img
sf probe 0
sf erase 0x260000 0x750000
sf write 0x21000000 0x260000 0x750000
```

**HW**

Rules:

- Length is `0x750000` for both erase and write, and the file must be exactly `0x750000` bytes.
- Always `sf erase` before `sf write`: NOR programming can only clear bits, writing over an
  unerased area corrupts the data.
- Never write at `0x210000` (that is the rootfs offset inside the OTA package, not in flash) or
  `0x050000` (kernel).
- Do not power off during erase/write. If interrupted, repeat erase and write.

## Readback

Before `reset`:

```
sf read 0x22000000 0x260000 0x750000
md5sum 0x22000000 0x750000
```

The result must equal the image MD5. `md5sum` is only available if compiled into this U-Boot.
Recommended; this readback was not performed during the original flash. **UNTESTED**

Then `reset`. A good boot shows on UART `VFS: Mounted root (squashfs filesystem) readonly on
device 31:2`, the `linuxrc` start and the init scripts; port 23/tcp opens after 10-30 s.

## Do not

- Do not `dd` an image to `/dev/mtdblock2` from the running system: the partition is mounted and
  partial or interrupted writes produced `SQUASHFS error` / `xz decompression failed`. **HW**
- Do not use the erase/write length `0x674000` from the tsunglung README: it is shorter than the
  partition and shorter than the tsunglung image itself
  ([firmware-history.md](firmware-history.md#tsunglung-uartu-boot-mod)).

## Recovery

**SD card recovery is not available on 4.3.7.** U-Boot only reads a signed `tf_update.img` and
rejects unsigned or old images such as `tf_recovery.img` 3.5.3
([firmware-history.md](firmware-history.md#signed-sd-update)). Recovery is a manual U-Boot rewrite.

### Stock rootfs source

The stock 4.3.7 rootfs is the slice `0x210000`-`0x95FFFF` of the vendor OTA package
`upd_ota_437.bin` (16,318,464 B, MD5 `8e253901852455ca9845b82bd70bdca8`) (**STATIC**):

```sh
dd if=upd_ota_437.bin of=rootfs_stock_437.bin bs=4096 skip=528 count=1872
md5sum rootfs_stock_437.bin    # 5b01aba1e745ecbe765ea0093172887d
```

There is no public download for this package: the Mi Home firmware API does not offer 4.3.7
(it serves older stable versions), and the vendor CDN paths used for older `tf_recovery.img`
packages return 404 for 4.0.9 and 4.3.7. The copy used here was captured from an OTA update
offered to a camera. Without such a copy, obtain the stock rootfs from your own camera: the
region `0x260000`-`0x9AFFFF` of a full SPI dump taken before any write (see
[Backup first](#backup-first)).

Flash it with the same U-Boot sequence as above (offset `0x260000`, length `0x750000`). If a full
SPI dump exists, the matching region of that dump can be written instead. Restoring the stock
rootfs is **UNTESTED**.

UART access to U-Boot remains the recovery path for any rootfs failure. A damaged U-Boot or
kernel partition requires an SPI programmer and a full dump.

To remove only the runtime hacks (keep the custom rootfs), stop the runit services first, then
delete the installed files:

```sh
PATH=/mnt/sdcard/hacks/bin:$PATH sv -w 10 down /mnt/data/hacks/service/*
killall runsvdir runsv
rm -rf /mnt/data/hacks
rm -f /mnt/data/scripts/post_init.sh /mnt/data/scripts/mode_enforce.sh /mnt/data/scripts/ptz
rm -f /mnt/data/bin/framegrabber /mnt/data/control.db
rm -rf /mnt/data/etc/framegrabber /mnt/data/etc/dropbear
rm -rf /mnt/sdcard/hacks
reboot
```

**UNTESTED**. `telnetd` stays active as long as the custom rootfs is installed.
