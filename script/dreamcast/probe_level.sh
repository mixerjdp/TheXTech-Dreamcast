#!/usr/bin/env bash
# Bisect which C++ feature breaks Dreamcast boot.
#   LEVEL=0 plain C++                     1 +attribute ctor
#   LEVEL=2 +global object w/ ctor        3 +std containers in static init
#   LEVEL=4 +throw/catch                  5 std used only inside main()
#   LEVEL=6 global ctor calls malloc()    7 global std::string only
set -eo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build-dreamcast"
D="$OUT/cppdemo"
VM="${VM:-juan@192.168.31.128}"
DCBASH='/i/sw/dc-dev/DreamSDK/usr/bin/bash.exe'
L="${LEVEL:-0}"

mkdir -p "$D"
cat > "$D/main.cpp" <<EOF
#include <kos.h>
#include <dc/video.h>
#if ${L} == 3 || ${L} == 4 || ${L} == 5
#include <string>
#include <vector>
#include <map>
#endif
#if ${L} == 4
#include <stdexcept>
#endif
#if ${L} == 6
#include <stdlib.h>
#endif
#if ${L} == 7
#include <string>
#endif

static void paint(unsigned short c)
{
    vid_set_mode(DM_640x480, PM_RGB565);
    for(int i = 0; i < 640*480; ++i)
        vram_s[i] = c;
}

#if ${L} >= 1
__attribute__((constructor(101)))
static void early_ctor() { paint(0x001F); }
#endif

#if ${L} == 2 || ${L} == 3 || ${L} == 4
struct Simple { int x; Simple(); };
Simple::Simple() { x = 42; }        // out-of-line: forces a dynamic initialiser
static Simple g_simple;
#endif

#if ${L} == 3 || ${L} == 4
struct Heavy {
    std::vector<std::string> v;
    std::map<std::string,int> m;
    Heavy() { v.push_back("hola"); m["dc"] = 1; }
};
static Heavy g_heavy;
#endif

#if ${L} == 6
struct Alloc { void *p; Alloc(); };
Alloc::Alloc() { p = malloc(4096); }   // heap use during static init, no libstdc++
static Alloc g_alloc;
#endif

#if ${L} == 7
static std::string g_str("hello from static init");
#endif

int main(int, char**)
{
#if ${L} == 2 || ${L} == 3 || ${L} == 4
    if(g_simple.x != 42) return 1;
#endif
#if ${L} == 3 || ${L} == 4
    if(g_heavy.v.empty()) return 2;
#endif
#if ${L} == 4
    try { throw std::runtime_error("probe"); }
    catch(const std::exception &e) { if(e.what()[0] == 'X') return 3; }
#endif
#if ${L} == 5
    { std::vector<std::string> v; v.push_back("hola");
      std::map<std::string,int> m; m["dc"] = 1;
      if(v.empty() || m.empty()) return 4; }
#endif
#if ${L} == 6
    if(!g_alloc.p) return 5;
#endif
#if ${L} == 7
    if(g_str.empty()) return 6;
#endif
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
O=/i/sw/TheXTech-main/build-dreamcast
sh-elf-objcopy -R .stack -O binary cppdemo.elf $O/cpp.bin
rm -rf $O/cpproot && mkdir -p $O/cpproot
/opt/toolchains/dc/kos/utils/scramble/scramble.exe $O/cpp.bin $O/cpproot/1ST_READ.BIN
echo ok > $O/cpproot/DUMMY.TXT
mkisofs -C 0,11702 -V THEXTECH -G $O/IP.BIN -r -J -l -o $O/cpp.iso $O/cpproot 2>/dev/null
rm -f $O/thextech_cpp.cdi
cdi4dc $O/cpp.iso $O/thextech_cpp.cdi >/dev/null 2>&1
' >/dev/null 2>&1

scp -o BatchMode=yes -q "$OUT/thextech_cpp.cdi" "$VM:~/roms/dreamcast/"
RES=$(ssh -o BatchMode=yes "$VM" "ROM=~/roms/dreamcast/thextech_cpp.cdi MARKS='8' bash /tmp/vm_test_engine.sh" 2>&1)

if echo "$RES" | grep -q "SH4 exception"; then
    echo "LEVEL ${L}: BOOT FAIL"
else
    echo "LEVEL ${L}: BOOT OK"
fi
