#!/usr/bin/env bash
# Fast debug loop: package the current engine ELF into a *minimal* CDI (no
# assets), ship it to the test VM and report how far it booted.
#
# Run from the host's Git Bash, not the DreamSDK shell.
set -eo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build-dreamcast"
VM="${VM:-juan@192.168.31.128}"
DCBASH='/i/sw/dc-dev/DreamSDK/usr/bin/bash.exe'

echo "==> Packaging minimal CDI"
DREAMSDK_HOME='I:\sw\dc-dev\DreamSDK' "$DCBASH" -l -c '
set -e
O=/i/sw/TheXTech-main/build-dreamcast
sh-elf-objcopy -R .stack -O binary /i/sw/TheXTech-main/build-dc/output/bin/thextech.elf $O/thextech.bin
rm -rf $O/minroot && mkdir -p $O/minroot
/opt/toolchains/dc/kos/utils/scramble/scramble.exe $O/thextech.bin $O/minroot/1ST_READ.BIN
echo probe > $O/minroot/DUMMY.TXT
mkisofs -C 0,11702 -V THEXTECH -G $O/IP.BIN -r -J -l -o $O/min.iso $O/minroot 2>/dev/null
rm -f $O/thextech_min.cdi
cdi4dc $O/min.iso $O/thextech_min.cdi >/dev/null 2>&1
' 2>&1 | grep -vE 'dreamsdk|Exception|Kallisti|Foundation|Msys|Configuration|^---|^$' || true

ls -lh "$OUT/thextech_min.cdi"

echo "==> Shipping to $VM"
scp -o BatchMode=yes -q "$OUT/thextech_min.cdi" "$VM:~/roms/dreamcast/"

echo "==> Running"
ssh -o BatchMode=yes "$VM" "ROM=~/roms/dreamcast/thextech_min.cdi MARKS='${MARKS:-12 30}' bash /tmp/vm_test_engine.sh" \
  | grep -E 'started|screenshot|Content ran|SH4|Fatal|\.png'

rm -rf "$OUT/shots" && mkdir -p "$OUT/shots"
scp -o BatchMode=yes -q "$VM:/tmp/thextech_engine_shots/*.png" "$OUT/shots/" 2>/dev/null || echo "(no screenshots)"
ls "$OUT/shots/" 2>/dev/null
