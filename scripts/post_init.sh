#!/bin/sh
# Wait for the SD, start the custom services under runit, then enforce the
# persisted Cloud/Local mode so both stacks never run together (RAM safety).
(
  i=0
  while [ $i -lt 90 ]; do
    if [ -x /mnt/sdcard/hacks/bin/runsvdir ] && [ -d /mnt/data/hacks/service ]; then
      if ! pidof runsvdir >/dev/null 2>&1; then
        mkdir -p /mnt/data/etc/framegrabber /mnt/data/etc/dropbear /mnt/sdcard/hacks/etc
        PATH=/mnt/sdcard/hacks/bin:/mnt/data/bin:$PATH
        export PATH
        nohup /mnt/sdcard/hacks/bin/runsvdir /mnt/data/hacks/service >/tmp/hacks.log 2>&1 &
      fi
      break
    fi
    sleep 2
    i=$((i+1))
  done
) >/dev/null 2>&1 &
( sleep 25; sh /mnt/data/scripts/mode_enforce.sh ) >/dev/null 2>&1 &
exit 0
