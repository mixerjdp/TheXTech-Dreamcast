#!/usr/bin/env bash
# Minimal C++ boot reproducer: builds a KOS C++ program with a configurable
# .rodata / .bss footprint, packages it and reports whether it booted.
#
#   ROM_KW=<K words of .rodata>  BSS_KW=<K words of .bss>  bash probe_cpp.sh
#
# Prints "BOOT OK" or "BOOT FAIL" plus the section sizes.
set -eo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build-dreamcast"
D="$OUT/cppdemo"
VM="${VM:-juan@192.168.31.128}"
DCBASH='/i/sw/dc-dev/DreamSDK/usr/bin/bash.exe'

ROM_KW="${ROM_KW:-900}"
BSS_KW="${BSS_KW:-1050}"

mkdir -p "$D"
cat > "$D/main.cpp" <<EOF
#include <kos.h>
#include <dc/video.h>
#include <string>
#include <vector>
#include <map>
#include <stdexcept>

#if ${BSS_KW} > 0
unsigned int g_bulk[${BSS_KW}*1024];
#endif
#if ${ROM_KW} > 0
const volatile unsigned int g_rom[${ROM_KW}*1024] = {1,2,3};
#endif

static void paint(unsigned short c)
{
    vid_set_mode(DM_640x480, PM_RGB565);
    for(int i = 0; i < 640*480; ++i)
        vram_s[i] = c;
}

struct Global {
    std::vector<std::string> v;
    std::map<std::string,int> m;
    Global() { v.push_back("hola"); m["dc"] = 1; }
};
static Global g_global;

__attribute__((constructor(101)))
static void early_ctor() { paint(0x001F); }

int main(int, char**)
{
#if ${BSS_KW} > 0
    if(g_bulk[0] == 0xFFFFFFFFu) return 1;
#endif
#if ${ROM_KW} > 0
    if(g_rom[${ROM_KW}*1024-1] == 0xFFFFFFFFu) return 1;
#endif
    try { throw std::runtime_error("probe"); }
    catch(const std::exception &e) { if(e.what()[0] == 'X') return 2; }
    if(g_global.v.empty()) return 3;
    paint(0x07E0);
    for(;;) timer_spin_sleep(100);
    return 0;
}
EOF

DREAMSDK_HOME='I:\sw\dc-dev\DreamSDK' "$DCBASH" -l -c '
set -e
cd /i/sw/TheXTech-main/build-dreamcast/cppdemo
rm -f main.o cppdemo.elf
kos-c++ -c main.cpp -o main.o 2>/dev/null
kos-c++ -o cppdemo.elf main.o 2>/dev/null
sh-elf-size cppdemo.elf | tail -1
O=/i/sw/TheXTech-main/build-dreamcast
sh-elf-objcopy -R .stack -O binary cppdemo.elf $O/cpp.bin
rm -rf $O/cpproot && mkdir -p $O/cpproot
/opt/toolchains/dc/kos/utils/scramble/scramble.exe $O/cpp.bin $O/cpproot/1ST_READ.BIN
echo ok > $O/cpproot/DUMMY.TXT
mkisofs -C 0,11702 -V THEXTECH -G $O/IP.BIN -r -J -l -o $O/cpp.iso $O/cpproot 2>/dev/null
rm -f $O/thextech_cpp.cdi
cdi4dc $O/cpp.iso $O/thextech_cpp.cdi >/dev/null 2>&1
' 2>&1 | grep -E '^\s+[0-9]+' || true

scp -o BatchMode=yes -q "$OUT/thextech_cpp.cdi" "$VM:~/roms/dreamcast/"
RES=$(ssh -o BatchMode=yes "$VM" "ROM=~/roms/dreamcast/thextech_cpp.cdi MARKS='8' bash /tmp/vm_test_engine.sh" 2>&1)

if echo "$RES" | grep -q "SH4 exception"; then
    echo "BOOT FAIL  (rodata=${ROM_KW}KW bss=${BSS_KW}KW)"
else
    echo "BOOT OK    (rodata=${ROM_KW}KW bss=${BSS_KW}KW)"
fi
