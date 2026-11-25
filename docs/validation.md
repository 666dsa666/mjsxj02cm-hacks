# Validation status

Scope: one MJSXJ02CM / IPC009 camera on stock firmware `4.3.7_0417`. Other documents use the same
tags; this table is the reference.

Legend:

- **HW**: proven on the camera from logs.
- **PARTIAL**: indirect evidence, or documented as working without a raw capture.
- **STATIC**: derived from files/binaries only, not run on the camera.
- **UNTESTED**: no evidence.

| Item | Status | Evidence |
|---|---|---|
| Rootfs flash from U-Boot (`fatload`, `sf erase`, `sf write` at `0x260000`, `reset`) | HW | 7,667,712 B loaded, erase/write OK, custom rootfs boots |
| Post-flash readback (`sf read` + `md5sum`) | UNTESTED | not performed during the original flash |
| `miio_rpc` (local MiIO RPC client) | HW | `{"result":["OK"]}` replies for light and motor requests |
| `scripts/post_init.sh` | PARTIAL | boot behaviour documented, no raw log |
| `scripts/mode_enforce.sh` (Local / Cloud switch, reboot in Local mode) | PARTIAL | Local, Cloud, Local switches and reboot documented, no raw log |
| `scripts/ptz` wrapper | UNTESTED | no evidence |
| RTSP on port 8554 (Local mode) | PARTIAL | documented as working, no raw capture |
| ONVIF, including PTZ (Local mode) | PARTIAL | documented as working, no raw capture |
| MiIO token extraction (`tools/scan_token_memory.py`) | PARTIAL | token accepted by `miio_rpc`, extraction not logged |
| CGI `status` | HW | rendered in the web UI |
| CGI `snapshot` | HW | rendered in the web UI |
| CGI `files` | HW | rendered in the web UI |
| CGI `props` | PARTIAL | property access proven through MiIO, not through the CGI |
| CGI `setprop` | PARTIAL | property access proven through MiIO, not through the CGI |
| CGI `led` | PARTIAL | LED switched through the MiIO `light` property; the CGI writes GPIO 78, which was not logged |
| CGI `ptz` | PARTIAL | motor movement obtained through MiIO; the CGI uses the `control` tool, which was not logged |
| CGI `cloud` | PARTIAL | mode switch documented, not logged through the CGI |
| CGI `download` | PARTIAL | links rendered, no successful download logged |
| CGI `opt`, `svc`, `ptztoggle`, `calibrate`, `tz`, `upload`, `restart` | UNTESTED | no evidence |
| Restore to stock rootfs ([flashing.md](flashing.md#recovery)) | UNTESTED | no evidence |
