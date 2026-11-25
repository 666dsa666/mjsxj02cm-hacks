# Known issues

Defects found by code review of `webui/`, `scripts/` and `services/`. Not fixed. **STATIC**
unless stated otherwise.

## Web UI

- **Timezone change fails for zones containing `/`.** The page sends
  `encodeURIComponent(zone)` (`Europe%2FParis`); `cgi-bin/tz` does not URL-decode and its pattern
  stops at `%`, so it receives `Europe`, finds no zone file and returns the zone list. The page does
  not check `ok` and shows "Timezone set to ..." anyway. Only `UTC` can be set from the UI.
- **LED switch does not reflect the real state.** Neither `status` nor `props` updates the LED
  checkbox; it starts unchecked on every page load. Setting the LED is expected to work
  (**PARTIAL**, see [validation.md](validation.md)).
- **Save button saves nothing.** It reloads `status`, `props` and `tz` and reports
  "Settings saved". Each control applies its change immediately when toggled.
- **Recording directories are dead links.** `files` lists only the top level of
  `MIJIA_RECORD_VIDEO`, where recordings are in subdirectories; the page links directories to
  `download`, which only serves regular files ("not found").
- **Uploads are never listed.** Uploads go to `/mnt/sdcard/uploads`; `files` only lists
  `MIJIA_RECORD_VIDEO`.
- **Automatic retry can replay actions.** Requests sent through `jget`/`jfetch` are retried once
  on failure, including client-side timeouts. A slow mode switch (`cloud`, 90 s timeout) or a PTZ move that did reach the
  camera can be executed twice.
- Unsupported items (two-way audio, FTP, FPS) are shown as disabled or "Unsupported".

## Modes and services

- **Boot may end in Cloud mode.** `post_init.sh` starts `mode_enforce.sh` 25 s after boot,
  independently of the SD-card wait loop (up to 180 s). If the SD card is not mounted yet,
  `/mnt/sdcard/hacks/etc/mode` cannot be read, the mode defaults to `cloud`, and the cloud
  services are started.
- **`off` does not stop the cloud stack.** In `mode_enforce.sh` and in `svc rtsp down`, `off`
  only stops framegrabber, RTSP and ONVIF.
- **`svc` bypasses the preferences.** `svc rtsp up` always starts ONVIF regardless of
  `etc/onvif`; `svc onvif up` starts ONVIF in Cloud mode. `svc` is not used by the page but is
  reachable.
- **`ptztoggle` starts ONVIF in any mode.** It restarts `onvif-server` with `sv restart`, which also
  starts it when it was stopped (Cloud mode, or ONVIF option off).
- runit starts every service without a `down` file before `mode_enforce.sh` runs; without `down`
  files, `framegrabber-substream` and `ssh-server` start at boot and RTSP/ONVIF run briefly in
  Cloud mode.

## ONVIF

- `onvif_srvd` advertises `--type H264` while the stream is H.265. Strict clients may fail.
- The ONVIF snapshot URL (`http://<camera-ip>:8554/`) is a placeholder; there is no JPEG endpoint.

## LAN security

All services assume a trusted LAN.

- **No authentication** on the web UI and CGI endpoints; telnet gives a root shell without
  password; ONVIF uses `admin`/`admin`.
- **`download` serves any file under `/mnt/sdcard`**, including `hacks/etc/miio.token` and the
  dropbear configuration. Only `..` and absolute paths are rejected.
- **`upload` has no size limit** and overwrites existing files in `/mnt/sdcard/uploads`; a large
  upload can fill the SD card.
- **State-changing actions use GET** (`restart`, `cloud`, `ptz`, `setprop`, `svc`, ...). Any web
  page opened on the LAN can trigger them (CSRF).
- **Filename XSS.** `files` writes names into JSON without escaping and the page inserts them
  into `innerHTML`; a crafted filename in `MIJIA_RECORD_VIDEO` can inject script into the page.
- No shell injection was found: parameters are restricted by `sed` character classes or
  whitelists, and paths are quoted.
