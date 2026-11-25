#!/bin/sh
# Enforce a single running mode at boot / after changes.
# Modes: local (RTSP/ONVIF only) | cloud (Mi Home only) | off
# fetch_av is never stopped.
# Optional services respect persisted prefs:
#   /mnt/sdcard/hacks/etc/onvif = on|off (Local)
#   /mnt/sdcard/hacks/etc/nas   = on|off (Cloud)
PATH=/mnt/sdcard/hacks/bin:$PATH
export PATH
MODE=$(cat /mnt/sdcard/hacks/etc/mode 2>/dev/null)
[ -z "$MODE" ] && MODE=cloud
ONVIF_PREF=$(cat /mnt/sdcard/hacks/etc/onvif 2>/dev/null)
[ -z "$ONVIF_PREF" ] && ONVIF_PREF=on
NAS_PREF=$(cat /mnt/sdcard/hacks/etc/nas 2>/dev/null)
[ -z "$NAS_PREF" ] && NAS_PREF=on
CLOUD="miio_cloud miio_miss miio_nas miio_algo miio_record miio_devicekit miio_agent miio_client"
RTSP="framegrabber-mainstream rtsp-server"
case "$MODE" in
  local)
    for s in $CLOUD; do perpctl -b /etc/perp down "$s" >/dev/null 2>&1; sleep 1; done
    for s in $RTSP; do sv up "/mnt/data/hacks/service/$s" >/dev/null 2>&1; sleep 1; done
    if [ "$ONVIF_PREF" = on ]; then sv up /mnt/data/hacks/service/onvif-server >/dev/null 2>&1; else sv down /mnt/data/hacks/service/onvif-server >/dev/null 2>&1; fi
    ;;
  cloud)
    for s in $RTSP onvif-server; do sv down "/mnt/data/hacks/service/$s" >/dev/null 2>&1; sleep 1; done
    for s in $CLOUD; do
      [ "$s" = miio_nas ] && [ "$NAS_PREF" = off ] && continue
      perpctl -b /etc/perp up "$s" >/dev/null 2>&1; sleep 2
    done
    ;;
  off)
    for s in $RTSP onvif-server; do sv down "/mnt/data/hacks/service/$s" >/dev/null 2>&1; done
    ;;
esac
