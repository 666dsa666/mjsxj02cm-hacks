import sys
import time
import warnings

warnings.filterwarnings("ignore")
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
import telnetlib

tn = telnetlib.Telnet("192.0.2.1", 23, timeout=15)
time.sleep(1.2)
try:
    tn.read_until(b"login:", 12)
except Exception:
    pass
tn.write(b"root\r")
time.sleep(1.2)
try:
    tn.read_until(b"# ", 12)
except Exception:
    pass
try:
    tn.read_very_eager()
except Exception:
    pass


def cmd(c, wait=40.0):
    tn.write((c + "; echo -n __E; echo _ND\r").encode())
    out = b""
    end = time.time() + wait
    while time.time() < end:
        try:
            out += tn.read_very_eager()
        except Exception:
            pass
        if b"__E_ND" in out:
            break
        time.sleep(0.1)
    return out.decode("latin-1", "replace")


print(cmd("PID=$(pidof miio_client); echo PID=$PID; "
          "for line in $(cat /proc/$PID/maps | grep ' rw' | awk '{print $1}'); do "
          "start=${line%-*}; end=${line#*-}; "
          "s=$((0x$start)); e=$((0x$end)); len=$((e-s)); "
          "[ $len -gt 0 ] && dd if=/proc/$PID/mem bs=4096 skip=$((s/4096)) count=$((len/4096+1)) 2>/dev/null | strings -n 16; "
          "done | grep -aE '^.{16}$' | sort -u; "
          "echo ===HEX===; "
          "for line in $(cat /proc/$PID/maps | grep ' rw' | awk '{print $1}'); do "
          "start=${line%-*}; end=${line#*-}; s=$((0x$start)); e=$((0x$end)); len=$((e-s)); "
          "[ $len -gt 0 ] && dd if=/proc/$PID/mem bs=4096 skip=$((s/4096)) count=$((len/4096+1)) 2>/dev/null | strings -n 32; "
          "done | grep -aE '^[0-9a-f]{32}$' | sort -u", 90)[-2500:])
tn.close()
