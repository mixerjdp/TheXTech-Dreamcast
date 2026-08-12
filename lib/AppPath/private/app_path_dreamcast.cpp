/*
 * TheXTech Dreamcast — AppPath backend (ISO9660 assets, ramdisk + VMU writes)
 *
 * KallistiOS's ramdisk VFS has no mkdir: fs_ramdisk.c leaves the mkdir/rmdir
 * handlers NULL and only ever has a root directory. So every writable root is
 * pointed straight at "/ram/" instead of the usual "settings/", "gamesaves/",
 * ... subfolders, which could never be created.
 *
 * The ramdisk is volatile, so game saves go to a VMU instead. KOS mounts each
 * memory card under /vmu/<port><slot> (vmu_fs_init runs by default under
 * INIT_DEFAULT). That filesystem is also flat, and fs_vmu.c caps names at 12
 * characters — see makeGameSavePath() in src/main/game_save.cpp, which hashes
 * the episode path down to fit.
 *
 * If no card is plugged in we fall back to the ramdisk: the game still saves,
 * it just forgets at power off, which beats refusing to save at all.
 */

#include <cstdio>

#include <dc/maple.h>

#include "app_path_private.h"

static const char *const s_ramRoot = "/ram/";

//! Round-trips a small file so a card that is present but unusable (write
//! protected, full, or an emulator that only pretends to have one) falls back
//! to the ramdisk instead of silently dropping every save.
static bool s_vmuIsWritable(const char *root)
{
    char probe[32];
    std::snprintf(probe, sizeof(probe), "%sTXPROBE", root);

    std::FILE *f = std::fopen(probe, "wb");
    if(!f)
        return false;

    // The VMU allocates in 512-byte blocks; one block is the smallest a file
    // can be anyway.
    static char buf[512] = {0};
    bool ok = std::fwrite(buf, 1, sizeof(buf), f) == sizeof(buf);

    if(std::fclose(f) != 0)
        ok = false;

    if(ok)
    {
        std::FILE *r = std::fopen(probe, "rb");
        ok = (r != nullptr);
        if(r)
            std::fclose(r);
    }

    std::remove(probe);

    return ok;
}

//! First writable memory card, as a KOS VFS path, or the ramdisk if there is
//! none. Resolved once: maple enumeration is settled by the time the game asks.
static const char *s_saveRoot()
{
    static const char *s_cached = nullptr;

    if(!s_cached)
    {
        s_cached = s_ramRoot;

        for(int i = 0; ; i++)
        {
            maple_device_t *dev = maple_enum_type(i, MAPLE_FUNC_MEMCARD);
            if(!dev)
                break;

            // "/vmu/a1/", "/vmu/b2/", ... port is 0-based, unit is 1-based.
            static char s_path[16];
            std::snprintf(s_path, sizeof(s_path), "/vmu/%c%d/",
                          (char)('a' + dev->port), dev->unit);

            if(s_vmuIsWritable(s_path))
            {
                s_cached = s_path;
                break;
            }
        }
    }

    return s_cached;
}

void AppPathP::initDefaultPaths(const std::string & /*userDirName*/)
{
}

std::string AppPathP::appDirectory()
{
    return std::string();
}

std::string AppPathP::userDirectory()
{
    return s_ramRoot;
}

std::string AppPathP::assetsRoot()
{
    // Game assets live on the GD-ROM / CDI ISO9660 volume.
    return "/cd/";
}

AssetsPathType AppPathP::assetsRootType()
{
    return AssetsPathType::Single;
}

// Everything writable stays flat in the ramdisk root; see the note above.
std::string AppPathP::settingsRoot()
{
    return s_ramRoot;
}

std::string AppPathP::gamesavesRoot()
{
    return s_saveRoot();
}

std::string AppPathP::screenshotsRoot()
{
    return s_ramRoot;
}

std::string AppPathP::gifRecsRoot()
{
    return s_ramRoot;
}

std::string AppPathP::logsRoot()
{
    return s_ramRoot;
}

bool AppPathP::portableAvailable()
{
    return false;
}

void AppPathP::syncFS()
{
}
