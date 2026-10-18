#!/bin/sh
# Runs every round, so it uses shell builtins only (read, echo): no process per file.
# Only the sensors the app uses are read (NVMe, Wi-Fi, RAM sensors are not woken up for nothing).
# Output lines:
#   name|label|millidegrees     CPU / iGPU / AMD GPU temperatures
#   fan|driver|number|rpm       fan speed (if the driver reports it; some always report 0)
#   nvidia||millidegrees        NVIDIA temperature   (nvidia||asleep: card asleep, nvidia||skip: skipped this round,
#                               nvidia||fail: nvidia-smi gave no temperature, e.g. driver/library version mismatch)
# If $1 = "gpu", the NVIDIA temperature is read with nvidia-smi (expensive, ~30 ms); otherwise it is skipped.
# nvidia-smi is stopped after 5 s, so a hanging call doesn't hold up every later sensor read.
for h in /sys/class/hwmon/hwmon*; do
  [ -r "$h/name" ] || continue
  read -r n < "$h/name"
  case "$n" in
    coretemp|k10temp|zenpower|acpitz|i915|xe|amdgpu|nouveau)
      for t in "$h"/temp*_input; do
        [ -r "$t" ] || continue
        read -r v < "$t" || continue
        l=""
        [ -r "${t%_input}_label" ] && read -r l < "${t%_input}_label"
        echo "$n|$l|$v"
      done ;;
  esac
  for f in "$h"/fan*_input; do
    [ -r "$f" ] || continue
    read -r v < "$f" || continue
    i=${f##*/fan}
    echo "fan|$n|${i%_input}|$v"
  done
done
# NVIDIA: if the card is asleep, don't call nvidia-smi and wake it up
for d in /sys/bus/pci/devices/*; do
  [ -r "$d/vendor" ] || continue
  read -r ven < "$d/vendor"
  [ "$ven" = "0x10de" ] || continue
  read -r cls < "$d/class"
  case "$cls" in 0x03*) ;; *) continue ;; esac
  rs=""
  [ -r "$d/power/runtime_status" ] && read -r rs < "$d/power/runtime_status"
  if [ "$rs" = "suspended" ]; then
    echo "nvidia||asleep"
  elif [ "$1" != "gpu" ]; then
    echo "nvidia||skip"
  elif command -v nvidia-smi >/dev/null 2>&1; then
    to=""
    command -v timeout >/dev/null 2>&1 && to="timeout -k 2 5"
    t=$($to nvidia-smi --query-gpu=temperature.gpu --format=csv,noheader,nounits 2>/dev/null)
    t=${t%%[!0-9]*}
    if [ -n "$t" ]; then echo "nvidia||${t}000"; else echo "nvidia||fail"; fi
  fi
done
