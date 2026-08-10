/*
 * TheXTech Dreamcast — AppPath backend (ISO9660 assets, ramdisk for writes)
 *
 * KallistiOS's ramdisk VFS has no mkdir: fs_ramdisk.c leaves the mkdir/rmdir
 * handlers NULL and only ever has a root directory. So every writable root is
 * pointed straight at "/ram/" instead of the usual "settings/", "gamesaves/",
 * ... subfolders, which could never be created.
 *
 * Note this storage is volatile — it is gone at power off. Persisting saves
 * would mean writing to a VMU through KOS's vmufs.
 */

#include "app_path_private.h"

static const char *const s_ramRoot = "/ram/";

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
    return s_ramRoot;
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
