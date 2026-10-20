#!/bin/sh
# Keyboard light detection (read only; runs when the Keyboard tab opens, the permission changes, or after a write
# error while the light is looked for again: then at most once a minute).
# Everything here is readable without root. Nothing is written.
# Output lines (fields separated by "|"):
#   dmi|vendor|product|version|family|board     laptop maker and model (/sys/class/dmi/id)
#   kernel|release
#   led|name|driver|path                        a keyboard light the kernel exposes (LED class)
#   v|name|file|value                           a readable value of that light
#   w|name|file                                 a file of that light we may write to (permission granted)
#   x|name|file                                 a file that exists (kbd_rgb_mode is write-only, so it can't be read)
#   zone|name|driver|path                       an HP four-zone colour file: /sys/devices/platform/hp-wmi/rgb_zones/zoneNN
#                                               (older copies of the hp-wmi-fan-and-backlight-control module, 0.0.2;
#                                               0.0.4+ uses *::kbd_zoned_backlight-* LEDs instead). Hex RRGGBB, no brightness.
#                                               Its v/w lines use the file name "color".
#   usb|vendor|product|name                     USB devices of makers whose RGB keyboards are USB devices (ASUS, ITE/Lenovo)
#   rule|etc|current|old|legacy  rule|lib       our udev permission rule; legacy = an older, less strict version
#                                               (world-writable 0666, or running a shell); old = as strict, but under
#                                               the old name (Ccenter, before 0.4.0: 90-ccenter.rules)
# Testing without the hardware: SOLWIND_SYS=<folder> makes it read a fake /sys tree from that folder instead (the app
# then also writes into that fake tree, never to the real lights).
sys=${SOLWIND_SYS:-}/sys
dmi=$sys/class/dmi/id
r() { v=""; [ -r "$1" ] && read -r v < "$1"; printf '%s' "$v"; }
echo "dmi|$(r $dmi/sys_vendor)|$(r $dmi/product_name)|$(r $dmi/product_version)|$(r $dmi/product_family)|$(r $dmi/board_name)"
echo "kernel|$(r /proc/sys/kernel/osrelease)"

for d in "$sys"/class/leds/*; do
  n=${d##*/}
  case "$n" in
    *::kbd_backlight|*::kbd_backlight_*|*::kbd_zoned_backlight-*) ;;
    *) continue ;;
  esac
  [ -e "$d/brightness" ] || continue
  drv=""
  [ -e "$d/device/driver" ] && drv=$(readlink -f "$d/device/driver") && drv=${drv##*/}
  echo "led|$n|$drv|$d"
  for f in brightness max_brightness multi_index multi_intensity multi_max_intensity; do
    [ -r "$d/$f" ] && { read -r v < "$d/$f"; echo "v|$n|$f|$v"; }
  done
  for f in brightness multi_intensity kbd_rgb_mode; do
    [ -e "$d/$f" ] && echo "x|$n|$f"
    [ -w "$d/$f" ] && echo "w|$n|$f"
  done
done

# HP four-zone keyboards with the out-of-tree hp-wmi module: colour files on the hp-wmi device itself (not LED class).
# Only this exact device is looked at (Alienware uses the same rgb_zones name with a different format).
z=$sys/devices/platform/hp-wmi
if [ -d "$z/rgb_zones" ]; then
  drv=""
  [ -e "$z/driver" ] && drv=$(readlink -f "$z/driver") && drv=${drv##*/}
  for f in "$z"/rgb_zones/zone*; do
    [ -e "$f" ] || continue
    n=hp-wmi/rgb_zones/${f##*/}
    echo "zone|$n|$drv|$f"
    echo "v|$n|color|$(r "$f")"
    [ -w "$f" ] && echo "w|$n|color"
  done
fi

for u in "$sys"/bus/usb/devices/*; do
  [ -r "$u/idVendor" ] || continue
  read -r vid < "$u/idVendor"
  case "$vid" in 0b05|048d) ;; *) continue ;; esac
  echo "usb|$vid|$(r "$u/idProduct")|$(r "$u/product")"
done

f=/etc/udev/rules.d/90-solwind.rules
o=/etc/udev/rules.d/90-ccenter.rules
if grep -q -s -e 'chmod 0666' -e '/bin/sh' "$f" "$o"; then echo "rule|etc|legacy"
elif [ -f "$o" ]; then echo "rule|etc|old"
elif [ -f "$f" ]; then echo "rule|etc|current"; fi
if [ -f /usr/lib/udev/rules.d/90-solwind.rules ] || [ -f /usr/lib/udev/rules.d/90-ccenter.rules ]; then echo "rule|lib"; fi
true
