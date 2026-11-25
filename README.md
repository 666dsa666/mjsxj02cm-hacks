# mjsxj02cm-hacks

Custom rootfs, RTSP/ONVIF streaming and a LAN web control panel for the Xiaomi Mijia 360° 1080p
camera, model **MJSXJ02CM** (MiIO model `chuangmi.camera.ipc009`, IPC009), on stock firmware
`4.3.7_0417`.

## Status

Tested on one camera (MJSXJ02CM / IPC009, firmware 4.3.7_0417). Other hardware revisions or
firmware versions may differ. The first installation requires UART access (test pads) and a
U-Boot flash of the rootfs partition; everything after that lives on the SD card and the data
partition.

Status tags used in the documentation: **HW** (proven on the camera from logs), **PARTIAL**
(indirect, or documented without a raw capture), **STATIC** (derived from files/binaries only),
**UNTESTED** (no evidence). Per-item status: [docs/validation.md](docs/validation.md).

## What works

| Area | Status |
|---|---|
| Custom rootfs (stock 4.3.7 + `telnetd` + boot hook) flashed through U-Boot | HW |
| RTSP, H.265 1920x1088, `rtsp://<camera-ip>:8554/mainstream` (Local mode) | PARTIAL |
| ONVIF Profile S + PTZ (`onvif_srvd`, port 5000, `admin`/`admin`) (Local mode) | PARTIAL |
| Exclusive Cloud (Mi Home) / Local (RTSP/ONVIF) mode switch | PARTIAL |
| Web control panel on port 8080 | HW for `status`, `snapshot`, `files`; other endpoints PARTIAL or UNTESTED ([validation](docs/validation.md)); see [known issues](docs/known-issues.md) |
| Local MiIO RPC client `miio_rpc` | HW, Cloud mode only |
| Image settings through MiIO RPC from the web UI (watermark, WDR, full color, flip, night mode) | PARTIAL, Cloud mode only |
| LED (GPIO 78), PTZ | PARTIAL, both modes |
| Gimbal calibration | UNTESTED |
| Audio in RTSP, live video in the browser, firmware flashing from the web UI | not implemented |

The web UI has several functional defects (timezone, LED state, Save button, file manager) and
no authentication. Read [docs/known-issues.md](docs/known-issues.md) before relying on it.

## Quick start

1. Read the hardware notes ([docs/hardware.md](docs/hardware.md)) and take a full SPI dump with
   a programmer before any write. The U-Boot readback cannot be saved without a U-Boot write
   command, and the telnet dump is only possible after flashing; both pre-flash methods are
   **UNTESTED** ([docs/flashing.md](docs/flashing.md#backup-first)).
2. Obtain the stock 4.3.7 rootfs `rootfs_stock_437.bin` (no public download; see
   [docs/flashing.md](docs/flashing.md#stock-rootfs-source)) and the tsunglung `rootfs_new.bin`.
3. Build `custom_rootfs_v2_min.img` with [`rootfs/build_v2.sh`](rootfs/build_v2.sh) and flash it
   from U-Boot (offset `0x260000`, length `0x750000`).
4. Boot, connect with `telnet <camera-ip>` (root, no password).
5. Install the runtime files (binaries, `services/`, `scripts/`, `webui/`, `config/`) as
   described in [docs/architecture.md](docs/architecture.md#installation), and select Local mode
   (the default mode is Cloud, without RTSP/ONVIF).
6. Open `http://<camera-ip>:8080/`.

## Safety

- Writing the wrong offset or length in U-Boot can brick the camera. Keep UART access and a
  stock rootfs image available. Never write the rootfs with `dd` while it is mounted.
- The SD-card recovery of firmware 4.3.7 only accepts signed `tf_update.img` files; old
  `tf_recovery.img` images are rejected. Recovery is a manual U-Boot rewrite
  ([docs/flashing.md](docs/flashing.md#recovery)).
- Telnet (root, no password), the web UI (no authentication) and ONVIF (`admin`/`admin`) are
  LAN-only services. Do not expose the camera to untrusted networks.

## Glossary

| Term | Meaning |
|---|---|
| MiIO | Xiaomi local device protocol (encrypted JSON over UDP 54321), used by Mi Home and by `miio_rpc` |
| OTA | over-the-air firmware update package delivered by the Mi Home cloud |
| runit | small service supervisor (`runsvdir`, `runsv`, `sv`) used for the services added by this project |
| perp, `perpctl` | vendor service supervisor and its control command (`/etc/perp`) |
| `fetch_av` | vendor process that drives the sensor and hardware encoder; required in every mode |
| FIFO | named pipe (`mkfifo`) through which `framegrabber` passes encoded video to `rtspserver` |
| `sf` | U-Boot SPI flash command (`sf probe`, `sf read`, `sf erase`, `sf write`) |
| `fatload` | U-Boot command that loads a file from a FAT partition into RAM |
| `mmc rescan` | U-Boot command that (re)initialises the SD card |
| WDR | wide dynamic range image mode |
| ONVIF Profile S | ONVIF profile for video streaming and PTZ control of IP cameras |

## Documentation

| Document | Content |
|---|---|
| [docs/hardware.md](docs/hardware.md) | SoC, flash, partition map, UART pads and wiring, U-Boot environment |
| [docs/flashing.md](docs/flashing.md) | Backup, U-Boot flash, readback, recovery |
| [docs/firmware-history.md](docs/firmware-history.md) | Firmware versions, downgrade block, signed SD update, earlier hacks |
| [docs/architecture.md](docs/architecture.md) | Boot chain, services, Cloud/Local modes, MiIO RPC, installation, memory budget |
| [docs/webui-api.md](docs/webui-api.md) | Web UI and CGI endpoints as implemented |
| [docs/troubleshooting.md](docs/troubleshooting.md) | Symptoms and fixes |
| [docs/known-issues.md](docs/known-issues.md) | Functional defects and LAN security issues |
| [docs/validation.md](docs/validation.md) | Validation status of each component (HW / PARTIAL / UNTESTED) |

## Repository layout

```
webui/             web UI served by busybox httpd (index.html + cgi-bin/*)
scripts/           /mnt/data/scripts: post_init.sh, mode_enforce.sh, ptz
services/          runit service definitions (/mnt/data/hacks/service/*/run)
config/            /mnt/sdcard/hacks/etc/config.json, dropbear config template
rootfs/            custom rootfs recipe (build_v2.sh) and the S60service boot hook
src/               miio_rpc_static.c (local MiIO RPC client)
tools/             scan_token_memory.py (MiIO token recovery; edit the camera address on line 9)
docs/              documentation and screenshots
```

Binaries (rootfs images, `rtspserver`, `framegrabber`, `onvif_srvd`, `control`, runit, dropbear)
are not distributed.

## Credits

- IPC009 hack bundle (`ipc009_hack.zip`, distributed with
  [tsunglung/xiaomi-360-hacks](https://github.com/tsunglung/xiaomi-360-hacks)): framegrabber,
  Live555-based `rtspserver`, runit, dropbear service layout.
- [tsunglung/xiaomi-360-hacks](https://github.com/tsunglung/xiaomi-360-hacks): UART/U-Boot rootfs
  method for MJSXJ02CM, `telnetd` and `S60service` used in the custom rootfs.
- [j0se/xiaomi-360-1080p-hacks](https://github.com/j0se/xiaomi-360-1080p-hacks): original
  pre-4.0.9 SD-card hack.
- [belkone gist](https://gist.github.com/belkone/fb63376a9b6b6ebe969e9c8f96c0defb): SPI-flasher
  rootfs patch method.
- [telmomarques/mija-framegrabber](https://github.com/telmomarques/mija-framegrabber)
  (framegrabber lineage), Live555, busybox, runit, dropbear.
- [zry98/MJSXJ05CM-ONVIF](https://github.com/zry98/MJSXJ05CM-ONVIF): `onvif_srvd` build used for
  `onvif.xz`.
- [aslafy-z/xiaomi-1080-360-motor-control](https://github.com/aslafy-z/xiaomi-1080-360-motor-control):
  `control` PTZ tool used for `control.xz`.
- grablya95/Xiaomi-360-MJSXJ05CM and the MJSXJ05CM audio projects (zry98, cmiguelcabral,
  crckmc): UI and audio-architecture reference; not deployed here (different SoC).

## License

Code: MIT. Documentation: CC BY 4.0. See [LICENSE](LICENSE). Vendor firmware and third-party
binaries are not included and remain the property of their owners.
