# Hardware

## Summary

| Item | Value | Source |
|---|---|---|
| Model | Xiaomi Mijia 360° 1080p **MJSXJ02CM**, model type IPC009, MiIO model `chuangmi.camera.ipc009` | HW |
| SoC | MStar **MSC000A** (machine string `INFINITY3 MSC000A-S03A-64M`), single core | HW (boot log) |
| CPU | ARMv7 **Cortex-A7**, CPU ID `0x410fc075` | HW (boot log) |
| RAM | 64 MB, no swap | HW |
| Flash | 16 MB SPI NOR, Winbond **W25Q128**, JEDEC ID `EF 40 18` | HW |
| Wi-Fi | MediaTek **MT7601U** (`wlan0`), 2.4 GHz, station mode | HW |
| Video | 1920x1088, H.265 main stream | HW |
| Kernel | Linux 3.18.30 (MStar BSP) | HW |
| Userland | Buildroot 2017.08, glibc 2.25, busybox | HW |
| Bootloader | U-Boot 2015.01, banner `U-Boot 2015.01 (Sep 17 2020 - 11:43:25), Build: jenkins-ipc009_4.0.9_0408-4`, prompt `MStar #`. The build tag names the U-Boot build, not the running firmware version (`4.3.7_0417`) | HW (boot log) |
| Status LED | GPIO 78 (`/sys/class/gpio/gpio78/value`, `1` = on) | PARTIAL ([validation.md](validation.md)) |

## Flash layout

Partition table reported by the kernel at boot. **HW**

| MTD | Name | Offset | Size | Content |
|---|---|---|---|---|
| mtd0 | BOOT | `0x000000` | `0x050000` | U-Boot |
| mtd1 | KERNEL | `0x050000` | `0x210000` | kernel |
| mtd2 | ROOTFS | `0x260000` | `0x750000` | SquashFS 4.0, xz, 128 KiB blocks, read-only |
| mtd3 | DATA | `0x9B0000` | `0x630000` | JFFS2, mounted on `/mnt/data` (pairing, vendor binaries and libraries, hacks) |
| mtd4 | CONFIG | `0xFE0000` | `0x010000` | device configuration |
| mtd5 | FACTORY | `0xFF0000` | `0x010000` | factory data (device identity, keys) |

`0x750000` = 7,667,712 bytes = 117 erase blocks of 64 KiB. DATA, CONFIG and FACTORY are
device-specific and cannot be recreated from vendor images: back them up before any write
(see [flashing.md](flashing.md#backup-first)).

## U-Boot environment and boot arguments

| Item | Value |
|---|---|
| `bootargs` | `console=ttyS0,115200n8r init=/linuxrc rootfstype=squashfs root=/dev/mtdblock2 rw` |
| `env_offset` | `0x4F000` |
| Stored environment | bad CRC on the tested unit, U-Boot falls back to its built-in default environment |
| `bootdelay` | `0` |

With `bootdelay=0` the prompt can only be caught by sending `Enter` continuously from before
power-on (see [flashing.md](flashing.md#entering-u-boot)). **HW**

## UART

The UART test pads are next to the SPI flash on the main board. Row of four pads, silkscreen
`TP`, `TN`, `TXO`, `RXO` (letter O); the UART uses `TXO` and `RXO`. Ground: any pad labelled
`GND` (one is just below the flash chip). Settings: 115200 8N1, no flow control, 3.3 V logic.

| Adapter | Camera pad | Note |
|---|---|---|
| `TXD` | `RXO` | adapter transmit to camera receive |
| `RXD` | `TXO` | adapter receive from camera transmit |
| `GND` | `GND` | common ground |

Do not connect the adapter's 3V3/5V pin: the camera is powered from its own supply. A CP2102
(3.3 V) adapter works. Pogo pins or a test clip on the pads are sufficient; no soldering is
required.

On stock firmware the kernel console only prints the application log; it provides no shell
(no prompt, no echo). Interactive access is U-Boot only until the custom rootfs adds `telnetd`.

## Stock network exposure

- In pairing (AP) mode the camera runs an open access point with DHCP on `192.168.14.0/24`. A
  scan of TCP ports 23, 80, 443, 554, 5555, 8080, 8899, 9000, 4321, 5000, 6668 and 10000 found no
  open TCP service; only the MiIO provisioning protocol (UDP) is present. **HW**
- In station mode the stock firmware exposes MiIO on UDP 54321.
