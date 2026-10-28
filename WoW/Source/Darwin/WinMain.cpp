#if defined(WOWDECOMP_APPLE)

#include <fenv.h>
#include <cstdint>
#include <cstdlib>
#include "Engine/Source/Base/Job/JobSystem.h"
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/Client.h"

static int32_t ClientMain() {
    feclearexcept(FE_ALL_EXCEPT);
    fesetround(FE_TONEAREST);

    int32_t result = CommonMain();
    Job::Shutdown();
    return result;
}

int main(int argc, char** argv) {
    StormRtlInitialize();

    // ʕ •ᴥ•ʔ✿ Cocoa/SDL require the main thread; its stack is sized via -Wl,-stack_size,0x800000 ✿ ʕ •ᴥ•ʔ
    return ClientMain();
}

#endif
