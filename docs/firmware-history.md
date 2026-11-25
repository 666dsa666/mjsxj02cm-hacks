# Firmware history

## Versions

| Version | Relevance |
|---|---|
| `3.4.2_0062` | target of the original SD-card downgrade hack (j0se) |
| `3.5.3_0224` | official `tf_recovery.img` package `IPC009_3.5.3_0224` |
| `4.0.9_0409` | U-Boot and RSA signing changes; downgrades to older firmware are refused from this version. The U-Boot of the tested 4.3.7 unit carries the build tag `jenkins-ipc009_4.0.9_0408-4` ([hardware.md](hardware.md#summary)); the tag is a U-Boot build name, distinct from the firmware version string |
| `4.3.7_0417` | stock firmware of the tested unit: Buildroot 2017.08, built 2023-11-20 |

The running version is written by the firmware itself to `log/diagnosis.txt` on the SD card
(`CHUANGMI_VERSION=...`, together with `/etc/os-release`, the service list and mounts).

## Original SD-card hack

[j0se/xiaomi-360-1080p-hacks](https://github.com/j0se/xiaomi-360-1080p-hacks) (originally by
telmomarques) works in two steps: a `tf_recovery.img` on the SD card downgrades the camera to
`3.4.2_0062`, then a boot script runs the hack from the SD card.

From `4.0.9_0409` the downgrade is refused, so this hack cannot be used on 4.3.7. Reports:
[HACF forum](https://forum.hacf.fr/t/integration-de-la-camera-xiaomi-1080p-360/733),
[hassbian forum](https://bbs.hassbian.com/forum.php?mod=viewthread&tid=11825).

## Signed SD update

With a card inserted, U-Boot of 4.3.7 looks for `tf_update.img` (not `tf_recovery.img`) and
verifies its signature with libsodium. **HW**

```
read file start
reading tf_update.img
read len = 0, actlen = 16318544
data check start
Verifying singature using libsodium
...
Final...Failed
data check NG
```

(`singature` is verbatim from the U-Boot log, sic.)

An unsigned or old image (tested: `tf_recovery.img` 3.5.3) fails the check; the camera then boots
normally. In userspace, `S96rename_firmware` renames `tf_update.img`, `tf_all.img` and
`tf_all_recovery.img` to `*.bak` unless `/tmp/manu_test/update.txt` exists (factory mode).
**STATIC**

Consequence: there is no automatic SD-card restore on 4.3.7. See
[flashing.md](flashing.md#recovery) for the manual U-Boot procedure.

## tsunglung UART/U-Boot mod

[tsunglung/xiaomi-360-hacks](https://github.com/tsunglung/xiaomi-360-hacks) supports MJSXJ02CM
without downgrading: stop U-Boot over UART and write a prebuilt rootfs from the SD card. It
provides telnet, ONVIF, RTSP and a web configuration page, plus the IPC009 hack bundle
(`ipc009_hack.zip`) used by this project.

Payload `mjsxj02cm/rootfs_new.bin` (**STATIC**):

| Field | Value |
|---|---|
| Size | 6,856,704 B (`0x68A000`) |
| MD5 | `b150897a77f716cb577cf51a82baeb3f` |
| Format | SquashFS 4.0, xz, 128 KiB blocks |
| `bytes_used` | 6,854,408 B (`0x689708`) |

**Erase-length warning.** The tsunglung README uses `sf erase 0x260000 0x674000` and
`sf write ... 0x674000`. `0x674000` is 6,766,592 B, about 88 KB less than the filesystem in
`rootfs_new.bin`; the tail of the filesystem is then written to unerased flash or not written
at all, which corrupts the last blocks. The ROOTFS partition is `0x750000`; erase and write that
length (see [flashing.md](flashing.md#flash-the-rootfs)).

This project does not flash `rootfs_new.bin` as a whole (its userspace dates from 2020). It
takes only `telnetd` and `S60service` from it and adds them to the stock 4.3.7 rootfs
(`custom_rootfs_v2_min.img`).
