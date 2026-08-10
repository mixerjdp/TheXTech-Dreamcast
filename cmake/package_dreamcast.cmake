# Sega Dreamcast packaging helpers for TheXTech

if(NOT DREAMCAST)
    return()
endif()

message(STATUS "Dreamcast packaging: ELF + optional CDI via script/dreamcast/build_boot_image.sh")

set_target_properties(thextech PROPERTIES SUFFIX ".elf")

# kos-c++ already links -lkallisti etc. Do not add KOS_LIBS again.

if(DEFINED ENV{KOS_BASE})
    target_include_directories(thextech PRIVATE "$ENV{KOS_BASE}/include" "$ENV{KOS_BASE}/kernel/arch/dreamcast/include")
endif()

add_custom_target(thextech_dc_cdi
    COMMAND ${CMAKE_COMMAND} -E env
            "KOS_BASE=$ENV{KOS_BASE}"
            bash "${CMAKE_SOURCE_DIR}/script/dreamcast/build_boot_image.sh"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Build Dreamcast boot CDI/CHD for Flycast"
)
