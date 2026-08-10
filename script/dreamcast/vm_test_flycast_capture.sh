#!/usr/bin/env bash
set +e
CFG=/tmp/thextech_ra.cfg
LOG=/tmp/thextech_flycast_test4.log
SHOT=/tmp/thextech_flycast_shots
VID=/tmp/thextech_flycast_shots/clip.mp4
rm -rf "$SHOT" "$LOG"
mkdir -p "$SHOT"
cat > "$CFG" <<EOF
video_driver = "gl"
video_fullscreen = "true"
audio_enable = "false"
audio_driver = "null"
quit_press_twice = "false"
screenshot_directory = "$SHOT"
log_verbosity = "true"
EOF

export DISPLAY=:0
export WAYLAND_DISPLAY=wayland-0
export XDG_RUNTIME_DIR=/run/user/1000
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus

retroarch -L "$HOME/.config/retroarch/cores/flycast_libretro.so" \
  "$HOME/roms/dreamcast/thextech_dc_boot.cdi" \
  --verbose --appendconfig "$CFG" >"$LOG" 2>&1 &
RAPID=$!
echo "started=$RAPID"
sleep 8

# Record a short clip / single frame from Xwayland
ffmpeg -y -f x11grab -video_size 1920x1080 -i :0 -frames:v 1 "$SHOT/frame.png" >"$SHOT/ffmpeg1.log" 2>&1
ffmpeg -y -f x11grab -video_size 1920x1080 -i :0 -t 2 -c:v libx264 -pix_fmt yuv420p -an "$VID" >"$SHOT/ffmpeg2.log" 2>&1

# Also try org.gnome.Shell.Screenshot if available
gdbus call --session --dest org.gnome.Shell.Screenshot \
  --object-path /org/gnome/Shell/Screenshot \
  --method org.gnome.Shell.Screenshot.Screenshot false true "$SHOT/gnome.png" \
  >"$SHOT/gdbus.log" 2>&1 || true

kill -TERM "$RAPID" 2>/dev/null
sleep 2
kill -KILL "$RAPID" 2>/dev/null
wait "$RAPID" 2>/dev/null

echo "=== shots ==="
ls -la "$SHOT"
echo "=== runtime ==="
grep -E 'Content ran|REIOS|Game ID|BOOT' "$LOG" | tail -20
echo "=== ffmpeg1 ==="
tail -8 "$SHOT/ffmpeg1.log"
echo "=== gdbus ==="
cat "$SHOT/gdbus.log" 2>/dev/null
