#include "VideoOptions.h"
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "WoW/Source/Glue/CWOWClientStatus.h"

void CGVideoOptions::InstallExtendedViewUiPatch() {
    if (!SFile::FileExistsEx("Interface\\FrameXML\\WowDecompVideoOptions.lua", 1)) {
        return;
    }

    CWOWClientStatus status("Logs\\FrameXML.log");
    FrameScript_ExecuteFile("Interface\\FrameXML\\WowDecompVideoOptions.lua", nullptr, nullptr, &status);
}
