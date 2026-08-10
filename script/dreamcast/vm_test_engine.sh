#!/usr/bin/env bash
# Smoke-test the Dreamcast engine CDI under RetroArch + Flycast.
# Meant to run ON the test VM (juan@192.168.31.128), which has an X session.
#
# Takes a series of screenshots so we can see how far boot/asset loading got,
# then reports what the core logged.
set +e

ROM="${ROM:-$HOME/roms/dreamcast/thextech_dc.cdi}"
CORE="$HOME/.config/retroarch/cores/flycast_libretro.so"
CFG=/tmp/thextech_engine_ra.cfg
LOG=/tmp/thextech_engine.log
SHOT=/tmp/thextech_engine_shots
# Screenshot moments, in seconds from launch
MARKS="${MARKS:-12 25 40 60 80}"

pkill -9 -f 'retroarch.*thextech' 2>/dev/null
rm -rf "$SHOT" "$LOG"
mkdir -p "$SHOT"

cat > "$CFG" <<EOF
# Absolute path: the core does not expand "~", and without this it silently
# falls back to the REIOS HLE BIOS.
system_directory = "$HOME/.config/retroarch/system"
audio_enable = "false"
audio_driver = "null"
video_fullscreen = "false"
video_window_width = "640"
video_window_height = "480"
quit_press_twice = "false"
screenshot_directory = "$SHOT"
network_cmd_enable = "true"
network_cmd_port = "55355"
EOF

export DISPLAY=:0
export WAYLAND_DISPLAY=wayland-0
export XDG_RUNTIME_DIR=/run/user/1000

retroarch -L "$CORE" "$ROM" --appendconfig "$CFG" --verbose >"$LOG" 2>&1 &
RAPID=$!
echo "started pid=$RAPID rom=$ROM"

prev=0
for m in $MARKS; do
  sleep $((m - prev))
  prev=$m
  if ! kill -0 "$RAPID" 2>/dev/null; then
    echo "!! retroarch exited before t=${m}s"
    break
  fi
  printf 'SCREENSHOT\n' | nc -u -w1 127.0.0.1 55355 >/dev/null 2>&1
  echo "screenshot requested at t=${m}s"
done

sleep 2
kill -TERM "$RAPID" 2>/dev/null
sleep 2
kill -KILL "$RAPID" 2>/dev/null
wait "$RAPID" 2>/dev/null

echo "=== screenshots ==="
ls -la "$SHOT"
echo "=== core/runtime lines ==="
grep -aE 'Content ran|REIOS|Game ID|Screenshot|error|Error|ERROR|fail|Fail|exception|Exception|SH4|abort' "$LOG" | tail -40
