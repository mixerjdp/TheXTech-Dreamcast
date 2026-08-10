#!/usr/bin/env bash
set +e
CFG=/tmp/thextech_ra.cfg
LOG=/tmp/thextech_flycast_test3.log
SHOT=/tmp/thextech_flycast_shots
rm -rf "$SHOT" "$LOG"
mkdir -p "$SHOT"
cat > "$CFG" <<EOF
video_driver = "gl"
video_fullscreen = "false"
video_windowed_fullscreen = "false"
video_window_width = "800"
video_window_height = "600"
audio_enable = "false"
audio_driver = "null"
quit_press_twice = "false"
screenshot_directory = "$SHOT"
log_verbosity = "true"
EOF

sed -i 's/^reicast_dump_unique_textures.*/reicast_dump_unique_textures = "disabled"/' \
  "$HOME/.config/retroarch/config/Flycast/Flycast.opt" 2>/dev/null || true

export DISPLAY=:0
export XDG_RUNTIME_DIR=/run/user/1000
unset WAYLAND_DISPLAY

retroarch -L "$HOME/.config/retroarch/cores/flycast_libretro.so" \
  "$HOME/roms/dreamcast/thextech_dc_boot.cdi" \
  --verbose --appendconfig "$CFG" >"$LOG" 2>&1 &
RAPID=$!
echo "started=$RAPID"

for _ in $(seq 1 10); do
  sleep 1
  if ! kill -0 "$RAPID" 2>/dev/null; then
    echo died_early
    break
  fi
done

import -window root "$SHOT/root.png" 2>"$SHOT/import.err" || true
xwininfo -root -tree 2>/dev/null | grep -iE 'retro|flycast|thextech' | head -10 > "$SHOT/windows.txt" || true
WID=$(awk '/[Rr]etro|[Ff]lycast/{print $1; exit}' "$SHOT/windows.txt")
if [ -n "$WID" ]; then
  import -window "$WID" "$SHOT/win.png" 2>>"$SHOT/import.err" || true
fi

kill -TERM "$RAPID" 2>/dev/null
sleep 2
kill -KILL "$RAPID" 2>/dev/null
wait "$RAPID" 2>/dev/null

echo "=== shots ==="
ls -la "$SHOT"
echo "=== runtime ==="
grep -E 'Content ran|REIOS|Game ID|ERROR|Segmentation|crash' "$LOG" | tail -30
echo "=== done ==="
