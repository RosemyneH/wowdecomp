#include "VideoOptions.h"
#include "WowDecompVideoOptionsLua.h"
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "WoW/Source/Glue/CWOWClientStatus.h"

void CGVideoOptions::InstallExtendedViewUiPatch() {
    CWOWClientStatus status("Logs\\FrameXML.log");

    if (SFile::FileExistsEx("Interface\\FrameXML\\WowDecompVideoOptions.lua", 1)) {
        FrameScript_ExecuteFile("Interface\\FrameXML\\WowDecompVideoOptions.lua", nullptr, nullptr, &status);
        return;
    }

    FrameScript_Execute(kWowDecompVideoOptionsLua, "Interface\\FrameXML\\WowDecompVideoOptions.lua", nullptr);
}
