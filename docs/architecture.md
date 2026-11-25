# Architecture

The vendor media pipeline is kept: `fetch_av` and the hardware encoder run in every mode. The
RTSP stack reads the encoder output through the vendor FIFO/socket interfaces. The camera has
64 MB of RAM, so the Mi Home cloud stack and the RTSP stack are run one at a time.

```
U-Boot (MStar #)
  -> kernel 3.18.30
    -> rootfs (stock 4.3.7 + telnetd + S60service)
      -> /etc/init.d/S60service: runlevel, telnetd, /mnt/data/scripts/post_init.sh
        -> runsvdir /mnt/data/hacks/service        (runit: RTSP stack, web UI)
        -> mode_enforce.sh after 25 s               (Cloud / Local / off)
      -> perp /etc/perp                             (vendor services: fetch_av, miio_*)
```

## Boot scripts

| File | Installed at | Role |
|---|---|---|
| [`rootfs/S60service`](../rootfs/S60service) | `/etc/init.d/S60service` (rootfs) | starts the runlevel, `telnetd` (root, no password, port 23), then `post_init.sh` if executable |
| [`scripts/post_init.sh`](../scripts/post_init.sh) | `/mnt/data/scripts/post_init.sh` | waits up to 180 s (90 x 2 s) for `/mnt/sdcard/hacks/bin/runsvdir`, starts `runsvdir /mnt/data/hacks/service`; independently starts `mode_enforce.sh` 25 s after boot (**PARTIAL**) |
| [`scripts/mode_enforce.sh`](../scripts/mode_enforce.sh) | `/mnt/data/scripts/mode_enforce.sh` | applies the stored mode (see below) (**PARTIAL**) |
| [`scripts/ptz`](../scripts/ptz) | `/mnt/data/scripts/ptz` | unpacks the `control` motor tool to `/tmp/control` and runs it with the position file `/mnt/data/control.db` (**UNTESTED**) |

## Services

### runit (`/mnt/data/hacks/service`)

Definitions in [`services/`](../services/).

| Service | Command |
|---|---|
| `framegrabber-mainstream` | `/mnt/data/bin/framegrabber -f /mnt/data/etc/framegrabber/rtsp_mainstream -c 0` |
| `framegrabber-substream` | same with `rtsp_substream`, `-c 1` |
| `rtsp-server` | `rtspserver -c /mnt/sdcard/hacks/etc/config.json` (main and sub FIFOs), port 8554, path `/mainstream` |
| `onvif-server` | `onvif_srvd` unpacked from `onvif.xz` to `/tmp`, port 5000, `admin`/`admin`, Profile S, `--type H264`, PTZ arguments unless `ptz_enabled` is `off` |
| `webui` | busybox `httpd -p 8080 -h /mnt/sdcard/hacks/www -f` |
| `ssh-server` | dropbear with [`config/ssh-server/config.json`](../config/ssh-server/config.json) |

`runsvdir` starts every service directory that has no `down` file. The mode scripts only manage
`framegrabber-mainstream`, `rtsp-server` and `onvif-server`; `framegrabber-substream` and
`ssh-server` should have a `down` file to stay stopped. Until `mode_enforce.sh` runs, the RTSP
services may start in Cloud mode as well.

The dropbear template ships with the password placeholder `CHANGE_ME`; set a password before
enabling `ssh-server`.

### perp (vendor)

Vendor services are supervised by `perp` (`/etc/perp` points to `/mnt/data/etc/perp`):

```
perpctl -b /etc/perp down <service>
perpctl -b /etc/perp up   <service>
```

Observed services: `fetch_av`, `miio_agent`, `miio_algo`, `miio_client`, `miio_client_helper`,
`miio_cloud`, `miio_devicekit`, `miio_miss`, `miio_nas`, `miio_ota`, `miio_qrcode`, `miio_record`,
`miio_sdcard`, `miio_stream`, `mortoxd`. `fetch_av` must stay up in every mode.

## Modes

Stored in `/mnt/sdcard/hacks/etc/mode`. Missing or empty means `cloud`.

| Mode | `mode_enforce.sh` and `cgi-bin/cloud` do |
|---|---|
| `local` | stop `miio_cloud miio_miss miio_nas miio_algo miio_record miio_devicekit miio_agent miio_client`; start `framegrabber-mainstream`, `rtsp-server`; start or stop `onvif-server` according to `etc/onvif` |
| `cloud` | stop `framegrabber-mainstream`, `rtsp-server`, `onvif-server`; start the cloud services above (skip `miio_nas` if `etc/nas` is `off`) |
| `off` | stop `framegrabber-mainstream`, `rtsp-server`, `onvif-server`; the cloud services are **not** stopped |

Service starts are paced (1-2 s); a switch from the web UI takes about 15-25 s. Local, Cloud and
Local switches and a reboot in Local mode are documented without a raw log. **PARTIAL**

The web UI reports the mode from running processes (`cgi-bin/status`): `cloud` if `miio_client`
runs, `local` if `rtspserver` runs, `mixed` if both, `off` if neither. `mixed` is only expected
transiently during a switch.

Optional services, persisted on the SD card and applied by mode switches and at boot:

| File | Mode | Effect |
|---|---|---|
| `/mnt/sdcard/hacks/etc/onvif` (`on`/`off`, default `on`) | Local | start/stop `onvif-server` (about 1.6 MB RSS) |
| `/mnt/sdcard/hacks/etc/nas` (`on`/`off`, default `on`) | Cloud | start/stop `miio_nas` (about 1-2 MB RSS) |
| `/mnt/sdcard/hacks/etc/ptz_enabled` (`on`/`off`, default `on`) | both | PTZ CGI and ONVIF PTZ |

Mode-handling defects are listed in [known-issues.md](known-issues.md).

## Local MiIO RPC

Image settings (watermark, WDR, full color, flip, night mode, LED property) exist only in the
running vendor pipeline and are reachable through the MiIO protocol on UDP 54321. `miio_client`
must run, so these settings work in Cloud mode only.

[`src/miio_rpc_static.c`](../src/miio_rpc_static.c) is a self-contained client (MD5,
AES-128-CBC, UDP; no external libraries):

```
miio_rpc <method> <params-json> [token-file]
miio_rpc get_prop '["watermark","wdr","light","flip","full_color","night_mode"]'
miio_rpc set_wdr '["off"]'
miio_rpc set_night_mode '["1"]'
```

- Token file: argument 3, else `$MIIO_TOKEN_FILE`, else `/mnt/sdcard/hacks/etc/miio.token`
  (first 16 bytes are used).
- Target: `$MIIO_HOST`, else the first non-loopback IPv4 address of the camera, else
  `127.0.0.1`. The hello exchange fails on `127.0.0.1`; the LAN address must be used. **HW**
- Build: `arm-linux-gnueabihf-gcc -Os -static -o miio_rpc miio_rpc_static.c`, then
  `arm-linux-gnueabihf-strip miio_rpc` (static glibc, about 375 KB).

Protocol:

- hello: `21 31 00 20` followed by 28 bytes `ff`; the reply carries the device id (bytes 4-11)
  and stamp (bytes 12-15).
- `key = md5(token)`, `iv = md5(key + token)`, AES-128-CBC, PKCS#7 padding.
- request: 32-byte header (magic, total length BE16, device id, stamp + 1 BE32, checksum)
  followed by the encrypted JSON `{"id":1,"method":...,"params":...}`. The checksum field is
  `md5` of the whole packet computed with the token in that field.

| Property | Values | Meaning |
|---|---|---|
| `watermark` | `on`/`off` | timestamp overlay |
| `wdr` | `on`/`off` | wide dynamic range |
| `flip` | `on`/`off` | rotate 180° |
| `full_color` | `on`/`off` | full color in low light |
| `light` | `on`/`off` | status LED |
| `night_mode` | `0`/`1`/`2` | auto / on / off |

### Token

The token is device-specific and changes when the camera is re-paired. It can be read from the
memory of `miio_client` while the cloud stack runs, with
[`tools/scan_token_memory.py`](../tools/scan_token_memory.py) (Python with `telnetlib`, removed
in Python 3.13). The camera address is hard-coded: replace the placeholder `192.0.2.1` on line 9
before running it. The script prints 16-character strings and 32-character hex strings found in
writable mappings; the token is the 16-character string whose hex encoding also appears in the
second list. The extracted token is accepted by
`miio_rpc`; the extraction run itself was not logged (**PARTIAL**). Store it as 16 raw bytes:

```sh
printf '%s' '<16-character-token>' > /mnt/sdcard/hacks/etc/miio.token
```

The file is readable through `cgi-bin/download` ([known-issues.md](known-issues.md#lan-security)).

## LED and PTZ

- LED: `cgi-bin/led` writes `/sys/class/gpio/gpio78/value`, intended for both modes. The LED
  was switched through the MiIO `light` property; the GPIO path through the CGI was not logged.
  **PARTIAL**
- PTZ: `control` motor tool (built from
  [aslafy-z/xiaomi-1080-360-motor-control](https://github.com/aslafy-z/xiaomi-1080-360-motor-control),
  loads the vendor libraries from `/mnt/data/lib`), `ptz left|right|up|down <steps>`, `ptz stop`,
  `ptz get`, `ptz calibrate`. The axes are inverted on this camera: the web CGI and the ONVIF PTZ
  arguments swap left/right and up/down. Motor movement was obtained through MiIO; the
  `control` path through the CGI was not logged. **PARTIAL** Calibration sweeps the full range
  (reported pan 172 / 86, tilt 40 / 20) and takes a few seconds. **UNTESTED**
- `/mnt/data/control.db` is the position file of `control` (JSON with `current_x`, `current_y`,
  `initialized`). It is written by the tool on the camera and is not a vendor file. If it is
  missing, run `/mnt/data/scripts/ptz ptz calibrate` once. **UNTESTED**

## Runtime paths

| Path | Content |
|---|---|
| `/mnt/sdcard/hacks/bin` | `rtspserver`, `control.xz`, `onvif.xz`, `miio_rpc`, `runsvdir`, `runsv`, `sv`, `runit`, `dropbear` |
| `/mnt/data/control.db` | PTZ position file of `control` |
| `/mnt/data/bin`, `/mnt/data/lib` | `framegrabber` and vendor libraries |
| `/mnt/sdcard/hacks/etc` | `config.json`, `miio.token`, `mode`, `onvif`, `nas`, `ptz_enabled` |
| `/mnt/sdcard/hacks/ssh-server/config` | dropbear `config.json` |
| `/mnt/sdcard/hacks/www` | web UI ([`webui/`](../webui/)) |
| `/mnt/data/hacks/service` | runit service directories ([`services/`](../services/)) |
| `/mnt/data/scripts` | `post_init.sh`, `mode_enforce.sh`, `ptz` |
| `/mnt/data/etc/framegrabber` | FIFOs `rtsp_mainstream`, `rtsp_substream` |
| `/mnt/data/etc/TZ` | symlink to the active zoneinfo file |

## Installation

Copy the files to a FAT32 microSD card on a PC, insert the card, then install from a telnet shell
after the custom rootfs boots (the card is mounted on `/mnt/sdcard`). Binaries are not part of
this repository:

| File | Source |
|---|---|
| `framegrabber` | `ipc009_hack.zip`: `hacks/framegrabber/bin/ipc009/framegrabber` |
| `rtspserver` | `ipc009_hack.zip`: `hacks/rtsp-server/bin/rtspserver` |
| `runit`, `runsvdir`, `runsv`, `sv` | `ipc009_hack.zip`: `hacks/runit/bin/` |
| `dropbear` | `ipc009_hack.zip`: `hacks/ssh-server/bin/dropbear` |
| `onvif.xz` | `onvif_srvd` built statically from [zry98/MJSXJ05CM-ONVIF](https://github.com/zry98/MJSXJ05CM-ONVIF), compressed with `xz` |
| `control.xz` | `control` built for `armv7-unknown-linux-gnueabihf` from [aslafy-z/xiaomi-1080-360-motor-control](https://github.com/aslafy-z/xiaomi-1080-360-motor-control), compressed with `xz` |
| `miio_rpc` | built from [`src/miio_rpc_static.c`](../src/miio_rpc_static.c) (above) |

`ipc009_hack.zip` also contains a web configuration server, a websocket stream server and an
SFTP server; they are not used.

1. Create the target directories:
   ```sh
   mkdir -p /mnt/sdcard/hacks/bin /mnt/sdcard/hacks/etc /mnt/sdcard/hacks/ssh-server/config \
            /mnt/sdcard/hacks/www /mnt/data/bin /mnt/data/scripts /mnt/data/hacks/service
   ```
2. Copy the binaries to `/mnt/sdcard/hacks/bin` and `framegrabber` to `/mnt/data/bin`.
3. Copy [`config/config.json`](../config/config.json) to `/mnt/sdcard/hacks/etc/`, the dropbear
   template to `/mnt/sdcard/hacks/ssh-server/config/`.
4. Copy each `services/<name>/run` to `/mnt/data/hacks/service/<name>/run` (create each
   `<name>` directory); add a `down` file to `framegrabber-substream` and `ssh-server`.
5. Copy `scripts/*` to `/mnt/data/scripts/`.
6. Copy the content of `webui/` to `/mnt/sdcard/hacks/www/`.
7. `chmod +x` all scripts, `run` files and `cgi-bin/*`. Files must have LF line endings; CRLF
   causes `exit: Illegal number` errors.
8. Store the MiIO token (above).
9. Select Local mode, then reboot. The default mode is `cloud`, in which RTSP and ONVIF do not
   run. Use the mode selector of the web UI after the reboot, or before it:
   ```sh
   printf local > /mnt/sdcard/hacks/etc/mode
   ```

To update the web UI from a PC, serve a tarball over HTTP and on the camera:

```sh
cd /tmp
wget -q http://<pc-ip>:<port>/webui.tgz -O webui.tgz
rm -rf /mnt/sdcard/hacks/www && mkdir -p /mnt/sdcard/hacks/www
gzip -dc webui.tgz | tar -x -C /mnt/sdcard/hacks/www
chmod +x /mnt/sdcard/hacks/www/cgi-bin/*
PATH=/mnt/sdcard/hacks/bin:$PATH sv -w 5 restart /mnt/data/hacks/service/webui
```

busybox `httpd` changes into its document root at start; restart `webui` after replacing the
directory.

## Memory budget

Measured on the camera. **HW**

The two columns are different metrics. `mem` in `cgi-bin/status` is
`(MemTotal - MemAvailable) / MemTotal` from `/proc/meminfo`. The command used for the "Used RAM"
column was not recorded; the two values cannot be converted into each other. "Free" in the text
below refers to the free memory reported on the camera, not to `MemAvailable`.

| Mode | Used RAM | `mem` in status |
|---|---|---|
| Local | about 58 MB | 76 % |
| Cloud | about 60 MB | 82 % |

Process RSS in Local mode: `fetch_av` about 6 MB, `rtspserver` 2.1 MB, `onvif_srvd` 1.6 MB,
`framegrabber` 1.4 MB, `httpd` 1.4 MB. Cloud additions: `miio_algo` 3.3 MB, `miio_client`
3.0 MB, `miio_miss` 2.8 MB, `miio_record` 2.5 MB, `miio_nas` 2.0 MB, `miio_devicekit` 1.9 MB.

Design decisions that follow:

- Cloud and Local are exclusive.
- No audio in RTSP. ALSA devices, the `shbf` bus and `libboardav` exist, but a capture/G.711
  bridge and an audio-capable RTSP server would add several MB on a device with 1-2 MB free and
  no swap. Audio remains available in the Mi Home app (Cloud mode).
- No live preview in the browser. Browsers do not play RTSP/H.265, and the hardware JPEG channel
  is only initialised by the vendor snapshot flow. The UI shows the last snapshot written by the
  vendor stack (`MIJIA_RECORD_VIDEO/tmp.jpeg`) and links to the RTSP URL for VLC.
- No firmware flashing from the web UI; flashing goes through U-Boot.
