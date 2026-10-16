#if defined(WOWDECOMP_UNIX)

#include <fenv.h>
#include <ucontext.h>
#include <cstdint>
#include <cstdlib>
#include "Engine/Source/Base/Job/JobSystem.h"
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/Client.h"

static ucontext_t s_mainContext;
static ucontext_t s_fiberContext;
static int32_t s_fiberResult = -1;

static bool ReturnFalse() {
    return false;
}

static int32_t ClientMain() {
    feclearexcept(FE_ALL_EXCEPT);
    fedisableexcept(FE_ALL_EXCEPT);
    fesetround(FE_TONEAREST);
    ReturnFalse();

    Job::Init();
    int32_t result = CommonMain();
    Job::Shutdown();
    return result;
}

static void MainFiberProc() {
    s_fiberResult = ClientMain();
}

int main(int argc, char** argv) {
    StormRtlInitialize();

    constexpr size_t stackSize = 0x800000;
    void* stack = malloc(stackSize);

    if (!stack || getcontext(&s_fiberContext)) {
        return -1;
    }

    s_fiberContext.uc_stack.ss_sp = stack;
    s_fiberContext.uc_stack.ss_size = stackSize;
    s_fiberContext.uc_link = &s_mainContext;
    makecontext(&s_fiberContext, MainFiberProc, 0);

    if (swapcontext(&s_mainContext, &s_fiberContext)) {
        free(stack);
        return -1;
    }

    free(stack);

    return s_fiberResult;
}

#endif
