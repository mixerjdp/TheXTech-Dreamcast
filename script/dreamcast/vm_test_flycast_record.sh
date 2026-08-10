#!/usr/bin/env bash
set +e
pkill -9 -f 'retroarch.*thextech' 2>/dev/null || true
CFG=/tmp/thextech_ra.cfg
LOG=/tmp/thextech_flycast_test5.log
REC=/tmp/thextech_ra_record.mkv
rm -f "$LOG" "$REC" /tmp/thextech_ra_record*
cat > "$CFG" <<EOF
audio_enable = "false"
audio_driver = "null"
video_fullscreen = "false"
video_window_width = "640"
video_window_height = "480"
quit_press_twice = "false"
video_record_quality = "2"
video_record_scale_factor = "1"
EOF

export DISPLAY=:0
export WAYLAND_DISPLAY=wayland-0
export XDG_RUNTIME_DIR=/run/user/1000

timeout 18s retroarch \
  -L "$HOME/.config/retroarch/cores/flycast_libretro.so" \
  "$HOME/roms/dreamcast/thextech_dc_boot.cdi" \
  --appendconfig "$CFG" \
  --record "$REC" \
  --verbose >"$LOG" 2>&1
echo "exit=$?"
ls -la /tmp/thextech_ra_record* 2>/dev/null
grep -E 'Content ran|REIOS|record|Recording|Game ID|BOOT|ERROR|Failed' "$LOG" | tail -40
# Extract a mid-frame if mkv exists
if [ -f "$REC" ]; then
  ffmpeg -y -ss 00:00:03 -i "$REC" -frames:v 1 /tmp/thextech_ra_frame.png >/tmp/ffrec.log 2>&1
  ls -la /tmp/thextech_ra_frame.png 2>/dev/null
fi
