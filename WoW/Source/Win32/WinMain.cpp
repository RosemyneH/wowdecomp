#if defined(WOWDECOMP_ARCH_32)

#include <windows.h>
#include <cstdlib>
#include <float.h>
#include "Engine/Source/Base/Job/JobSystem.h"
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/Client.h"

// STATIC: 0x00B311E8
static LPVOID lpFiber;

static void InvalidParameterHandler(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t reserved) {
}

static bool ReturnFalse() {
    return false;
}

extern "C" int WinMainCRTStartup();

// VERIFY: eff982d2 auto
// OFFSET: 0x00401000
extern "C" int start() {
    StormRtlInitialize();
    return WinMainCRTStartup();
}

// VERIFY: d518fe65 auto
// OFFSET: 0x00406D70
int main() {
    _set_invalid_parameter_handler(InvalidParameterHandler);
    _clearfp();
    _control87(0x9001F, 0xFFFFFFFF);
    ReturnFalse();

    int result = CommonMain();
    Job::Shutdown();
    return result;
}

// VERIFY: e860ff37 auto
// OFFSET: 0x0040B7D0
static VOID CALLBACK MainFiberProc(LPVOID parameter) {
    *static_cast<int*>(parameter) = main();
    SwitchToFiber(lpFiber);
}

// VERIFY: c31422a4 manual
// OFFSET: 0x0040B7F0
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    lpFiber = ConvertThreadToFiber(nullptr);

    if (!lpFiber) {
        return -1;
    }

    int result = -1;
    LPVOID fiber = CreateFiberEx(0, 0x800000, 0, MainFiberProc, &result);

    if (!fiber) {
        return -1;
    }

    SwitchToFiber(fiber);
    DeleteFiber(fiber);

    return result;
}

#endif
