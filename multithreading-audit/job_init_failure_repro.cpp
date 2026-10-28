#include "Engine/Source/Base/Job/JobSystem.h"
#include "Engine/Source/Base/Profiler.h"

#include <atomic>
#include <cstdio>
#include <system_error>

Profiler::Scope::Scope(uint32_t zone) : id(zone), start(0), child(0) {}
Profiler::Scope::~Scope() {}

void Run(Job::Range range, void* user) {
    static_cast<std::atomic<unsigned>*>(user)->fetch_add(range.end - range.begin);
}

int main() {
    try {
        Job::Init();
    } catch (const std::system_error&) {
        if (Job::WorkerCount() != 0) return 1;
        std::atomic<unsigned> count{0};
        Job::Start(19, Run, &count);
        Job::Wait();
        if (count != 19) return 1;
        Job::Shutdown();
        Job::Init();
        if (Job::WorkerCount() == 0) return 1;
        count = 0;
        Job::ParallelFor(19, Run, &count);
        Job::Shutdown();
        return count == 19 && Job::WorkerCount() == 0 ? 0 : 1;
    }
    Job::Shutdown();
    std::fprintf(stderr, "injection did not trigger (fewer than two workers)\n");
    return 77;
}
