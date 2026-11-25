# Troubleshooting

| Symptom | Cause / fix |
|---|---|
| No `MStar #` prompt, kernel boots | `bootdelay` is 0: start sending `Enter` before power-on ([flashing.md](flashing.md#entering-u-boot)) |
| SD card image ignored at boot | 4.3.7 only accepts a signed `tf_update.img`; use the U-Boot procedure ([flashing.md](flashing.md#recovery)) |
| `SQUASHFS error`, `xz decompression failed` | rootfs written from Linux or with a short erase length; rewrite from U-Boot with erase + write of `0x750000` |
| Custom rootfs does not boot (status LED stays orange, **UNTESTED** as an indicator) | image built on a filesystem without UNIX metadata, or wrong offset/length; rebuild with `rootfs/build_v2.sh` on ext4 as root, verify the readback MD5 |
| No services after reboot | SD card not mounted yet: `post_init.sh` waits up to 180 s for `runsvdir`; check `/tmp/hacks.log` |
| Camera comes back in Cloud mode after reboot | `mode_enforce.sh` ran before the SD card was mounted ([known-issues.md](known-issues.md#modes-and-services)); run `sh /mnt/data/scripts/mode_enforce.sh` again |
| UI stays on "Loading..." | `curl http://<camera-ip>:8080/cgi-bin/status`; hard-refresh the browser |
| CGI slow, load spikes | do not parse `netstat` (about 26 s per call on this camera); `status` reads `/proc/net/tcp` (hex ports `216A` = 8554, `1388` = 5000, `0016` = 22) |
| VLC cannot connect | camera in Cloud mode; RTSP only runs in Local mode |
| Video settings shown as unavailable | `miio_client` not running (Local mode); switch to Cloud |
| `miio_rpc` fails (`hello failed`, `no reply`) | wrong target address (must be the LAN address, see `MIIO_HOST`), missing or wrong token, `miio_client` stopped |
| Settings fail after re-pairing | the MiIO token changed; extract it again ([architecture.md](architecture.md#token)) |
| `exit: Illegal number` in scripts | CRLF line endings; convert to LF (`sed -i 's/\r$//' <file>`) |
| Web UI unchanged after update | restart the `webui` service after replacing `/mnt/sdcard/hacks/www` |
| SSH or ONVIF unreachable | service stopped by the current mode or by its option |
