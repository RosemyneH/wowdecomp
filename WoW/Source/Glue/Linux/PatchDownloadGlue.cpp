#if defined(WOWDECOMP_UNIX)

#include "WoW/Source/Glue/CGlueMgr.h"
#include "Engine/Source/BlizzardCore/Source/System/Process/Process.h"

// OFFSET: 0x00462190
bool CGlueMgr::PatchDownloadExecuteCommand(const char* path, const char* arguments) {
    return Blizzard::Process::LaunchChildProcessWithArguments(path, arguments, nullptr, nullptr);
}

#endif
