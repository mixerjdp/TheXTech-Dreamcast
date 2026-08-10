#!/usr/bin/env bash
# Build Dreamcast boot demo + CDI/CHD for Flycast
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEMO="$ROOT/platforms/dreamcast/boot_demo"
OUT="$ROOT/build-dreamcast"
mkdir -p "$OUT"

if [[ -z "${KOS_BASE:-}" ]]; then
  if [[ -f /opt/toolchains/dc/kos/environ.sh ]]; then
    # shellcheck disable=SC1091
    source /opt/toolchains/dc/kos/environ.sh
  else
    echo "KOS_BASE not set. Source KallistiOS environ.sh first." >&2
    exit 1
  fi
fi

echo "==> Building boot demo"
make -C "$DEMO" clean all
cp -f "$DEMO/thextech_dc_boot.elf" "$OUT/"

CDI="$OUT/thextech_dc_boot.cdi"
GDI_DIR="$OUT/thextech_dc_boot_gdi"
CHD="$OUT/thextech_dc_boot.chd"

echo "==> Creating CDI"
if command -v mkdcdisc >/dev/null 2>&1; then
  mkdcdisc -e "$OUT/thextech_dc_boot.elf" -o "$CDI" -n "TheXTech DC" -a "Wohlsoft/Port"
elif [[ -x "$KOS_BASE/utils/cdi4dc/cdi4dc" ]]; then
  # Fallback: scramble + mkisofs + cdi4dc
  SCRAMBLE="$KOS_BASE/utils/scramble/scramble"
  TMP="$OUT/cdroot"
  rm -rf "$TMP"
  mkdir -p "$TMP"
  sh-elf-objcopy -R .stack -O binary "$OUT/thextech_dc_boot.elf" "$OUT/temp.bin"
  "$SCRAMBLE" "$OUT/temp.bin" "$TMP/1ST_READ.BIN"
  # Minimal IP.BIN from KOS if present
  if [[ -f "$KOS_BASE/utils/makeip/IP.BIN" ]]; then
    IPBIN="$KOS_BASE/utils/makeip/IP.BIN"
  else
    make -C "$KOS_BASE/utils/makeip" || true
    IPBIN="$KOS_BASE/utils/makeip/IP.BIN"
  fi
  mkisofs -C 0,11702 -V THEXTECHDC -G "$IPBIN" -r -J -l -o "$OUT/thextech_dc_boot.iso" "$TMP"
  "$KOS_BASE/utils/cdi4dc/cdi4dc" "$OUT/thextech_dc_boot.iso" "$CDI"
else
  echo "Neither mkdcdisc nor cdi4dc found. ELF is at $OUT/thextech_dc_boot.elf" >&2
  exit 2
fi

echo "==> CDI ready: $CDI"

if command -v chdman >/dev/null 2>&1; then
  echo "==> Creating CHD"
  chdman createcd -f -i "$CDI" -o "$CHD"
  echo "==> CHD ready: $CHD"
else
  echo "chdman not found; skipping CHD (Flycast can load the CDI directly)."
fi

# Optional crude GDI sidecar notes for tools that expect a folder
mkdir -p "$GDI_DIR"
cp -f "$CDI" "$GDI_DIR/"
cat > "$GDI_DIR/README.txt" <<EOF
Flycast accepts CDI directly. Prefer:
  $CDI
or:
  $CHD
EOF

echo "Done."
echo "Flycast: open $CDI"
