#!/usr/bin/env bash
set +e
pkill -9 -f 'retroarch.*thextech' 2>/dev/null || true
CFG=/tmp/thextech_ra.cfg
LOG=/tmp/thextech_flycast_test6.log
SHOT=/tmp/thextech_flycast_shots
rm -rf "$SHOT" "$LOG"
mkdir -p "$SHOT"
cat > "$CFG" <<EOF
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

retroarch \
  -L "$HOME/.config/retroarch/cores/flycast_libretro.so" \
  "$HOME/roms/dreamcast/thextech_dc_boot.cdi" \
  --appendconfig "$CFG" \
  --verbose >"$LOG" 2>&1 &
RAPID=$!
echo "started=$RAPID"
sleep 10

# Ask RetroArch to take a screenshot via network command
printf 'SCREENSHOT\n' | nc -u -w1 127.0.0.1 55355
sleep 1
printf 'SCREENSHOT\n' | nc -u -w1 127.0.0.1 55355
sleep 1

kill -TERM "$RAPID" 2>/dev/null
sleep 2
kill -KILL "$RAPID" 2>/dev/null
wait "$RAPID" 2>/dev/null

echo "=== shots ==="
ls -la "$SHOT"
echo "=== runtime ==="
grep -E 'Content ran|REIOS|Game ID|Screenshot|NETWORK' "$LOG" | tail -30
