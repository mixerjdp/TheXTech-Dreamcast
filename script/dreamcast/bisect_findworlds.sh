#!/usr/bin/env bash
# One cycle of the FindWorlds() bisection.
#
#   STOP=8 bash script/dreamcast/bisect_findworlds.sh
#
# Builds with the halt at checkpoint STOP (see DC_STEP in menu_main.cpp), ships
# the image to the VM and reports whether that point was reached.
#
#   VERDE  -> execution got to the checkpoint
#   NEGRO  -> it crashed before reaching it
#
# All runs come from the same source with only STOP changed, so results are
# directly comparable — the mistake that invalidated the earlier attempt.
set -eo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build-dreamcast"
VM="${VM:-juan@192.168.31.128}"
DCBASH='/i/sw/dc-dev/DreamSDK/usr/bin/bash.exe'
STOP="${STOP:?usa STOP=<n>}"

echo "==> Building with stop point $STOP"
DREAMSDK_HOME='I:\sw\dc-dev\DreamSDK' "$DCBASH" -l -c "
cmake -S /i/sw/TheXTech-main -B /i/sw/TheXTech-main/build-dc \
  -DTHEXTECH_DC_BOOT_PROBE=ON -DTHEXTECH_DC_TEST_FINDWORLDS=ON \
  -DTHEXTECH_DC_STOP_AT=$STOP >/dev/null 2>&1
cmake --build /i/sw/TheXTech-main/build-dc -j12" > "$OUT/bisect.log" 2>&1

if grep -q "error:" "$OUT/bisect.log"; then
    echo "!! build falló"
    grep -A3 "error:" "$OUT/bisect.log" | head -10
    exit 1
fi

DREAMSDK_HOME='I:\sw\dc-dev\DreamSDK' "$DCBASH" -l -c \
    'bash /i/sw/TheXTech-main/script/dreamcast/build_engine_image.sh' >/dev/null 2>&1

scp -o BatchMode=yes -q "$OUT/thextech_dc.cdi" "$VM:~/roms/dreamcast/"

rm -rf "$OUT/shots" && mkdir -p "$OUT/shots"
ssh -o BatchMode=yes "$VM" \
    "ROM=~/roms/dreamcast/thextech_dc.cdi MARKS='${MARKS:-30 45}' bash /tmp/vm_test_engine.sh" \
    2>&1 | grep -E "Content ran" | tail -1
scp -o BatchMode=yes -q "$VM:/tmp/thextech_engine_shots/*.png" "$OUT/shots/" 2>/dev/null || true

python - "$STOP" <<'PY'
from PIL import Image
from collections import Counter
import glob, sys

stop = sys.argv[1]
verde = False
for p in sorted(glob.glob('build-dreamcast/shots/*.png')):
    im = Image.open(p).convert('RGB')
    r, g, b = Counter(im.get_flattened_data()).most_common(1)[0][0]
    if g > 150 and r < 100 and b < 100:
        verde = True

print(f'PUNTO {stop}: ' + ('ALCANZADO (verde)' if verde else 'NO alcanzado (murio antes)'))
PY
