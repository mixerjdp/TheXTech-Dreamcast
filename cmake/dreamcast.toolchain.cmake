# KallistiOS / Dreamcast CMake toolchain for TheXTech
# Usage:
#   source /opt/toolchains/dc/kos/environ.sh
#   cmake -S . -B build-dc -DCMAKE_TOOLCHAIN_FILE=cmake/dreamcast.toolchain.cmake -DDREAMCAST=ON

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR sh4)

set(DREAMCAST ON CACHE BOOL "Building Dreamcast port" FORCE)

if(NOT DEFINED ENV{KOS_BASE})
    message(FATAL_ERROR "KOS_BASE is not set. Source KallistiOS environ.sh first.")
endif()

set(KOS_BASE "$ENV{KOS_BASE}")
set(KOS_CC_BASE "$ENV{KOS_CC_BASE}")

# Prefer kos wrappers (inject flags). Fall back to raw toolchain binaries.
# On DreamSDK/Windows, compilers are PE and need the .exe suffix for CMake.
if(EXISTS "$ENV{KOS_BASE}/utils/build_wrappers/kos-cc")
    set(_KOS_C  "$ENV{KOS_BASE}/utils/build_wrappers/kos-cc")
    set(_KOS_CXX "$ENV{KOS_BASE}/utils/build_wrappers/kos-c++")
else()
    set(_KOS_C "$ENV{KOS_CC}")
    if(DEFINED ENV{KOS_CXX} AND NOT "$ENV{KOS_CXX}" STREQUAL "")
        set(_KOS_CXX "$ENV{KOS_CXX}")
    else()
        set(_KOS_CXX "$ENV{KOS_CCPLUS}")
    endif()
    if(EXISTS "${_KOS_C}.exe")
        set(_KOS_C "${_KOS_C}.exe")
    endif()
    if(EXISTS "${_KOS_CXX}.exe")
        set(_KOS_CXX "${_KOS_CXX}.exe")
    endif()
endif()

set(CMAKE_C_COMPILER   "${_KOS_C}")
set(CMAKE_CXX_COMPILER "${_KOS_CXX}")
set(CMAKE_ASM_COMPILER "$ENV{KOS_AS}")
set(CMAKE_AR           "$ENV{KOS_AR}")
set(CMAKE_RANLIB       "$ENV{KOS_RANLIB}")
set(CMAKE_STRIP        "$ENV{KOS_STRIP}")
foreach(_t AR RANLIB STRIP ASM_COMPILER)
    if(EXISTS "${CMAKE_${_t}}.exe")
        set(CMAKE_${_t} "${CMAKE_${_t}}.exe")
    endif()
endforeach()

set(CMAKE_FIND_ROOT_PATH "${KOS_BASE}" "${KOS_CC_BASE}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# kos-cc / kos-c++ wrappers already inject KOS_CFLAGS / KOS_LDFLAGS / KOS_LIBS.
# Passing them again via CMAKE_*_FLAGS_INIT duplicates -T linker scripts and breaks link.
if(EXISTS "$ENV{KOS_BASE}/utils/build_wrappers/kos-cc")
    set(CMAKE_C_FLAGS_INIT "")
    set(CMAKE_CXX_FLAGS_INIT "")
    set(CMAKE_EXE_LINKER_FLAGS_INIT "")
else()
    set(CMAKE_C_FLAGS_INIT   "$ENV{KOS_CFLAGS}")
    set(CMAKE_CXX_FLAGS_INIT "$ENV{KOS_CFLAGS} $ENV{KOS_CPPFLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS_INIT "$ENV{KOS_LDFLAGS} $ENV{KOS_LIBS}")
endif()

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_EXECUTABLE_SUFFIX ".elf")
