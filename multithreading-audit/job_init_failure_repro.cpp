#include "Engine/Source/Base/Job/JobSystem.h"
#include "Engine/Source/Base/Profiler.h"

#include <cstdio>
#include <system_error>

Profiler::Scope::Scope(uint32_t zone) : id(zone), start(0), child(0) {}
Profiler::Scope::~Scope() {}

void Run(Job::Range, void*) {}

int main() {
    try {
        Job::Init();
    } catch (const std::system_error&) {
        std::fprintf(stderr, "thread creation failed; reported workers: %u\n", Job::WorkerCount());
        Job::Start(1, Run, nullptr);
        Job::Wait();
        Job::Shutdown();
        return 0;
    }
    Job::Shutdown();
    std::fprintf(stderr, "injection did not trigger\n");
    return 2;
}
