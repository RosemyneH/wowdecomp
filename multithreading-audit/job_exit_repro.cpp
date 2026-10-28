#include "Engine/Source/Base/Job/JobSystem.h"
#include "Engine/Source/Base/Profiler.h"

#include <atomic>
#include <cstdlib>
#include <exception>
#include <cstring>
#include <thread>

Profiler::Scope::Scope(uint32_t zone) : id(zone), start(0), child(0) {}
Profiler::Scope::~Scope() {}

struct BlockedJob {
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
};

void Block(Job::Range, void* user) {
    auto& job = *static_cast<BlockedJob*>(user);
    job.entered.store(true);
    while (!job.release.load()) std::this_thread::yield();
}

int main(int argc, char** argv) {
    std::set_terminate([] { std::_Exit(42); });
    Job::Init();
    if (argc > 1 && (std::strcmp(argv[1], "active_exit") == 0 ||
                     std::strcmp(argv[1], "active_shutdown_exit") == 0)) {
        auto* job = new BlockedJob;
        Job::Start(1, Block, job);
        while (!job->entered.load()) std::this_thread::yield();
        if (std::strcmp(argv[1], "active_shutdown_exit") == 0) {
            job->release.store(true);
            Job::Shutdown();
            delete job;
        }
        std::exit(0);
    }
    if (argc > 1 && (std::strcmp(argv[1], "shutdown") == 0 ||
                     std::strcmp(argv[1], "shutdown_exit") == 0)) Job::Shutdown();
    if (argc > 1 && (std::strcmp(argv[1], "exit") == 0 ||
                     std::strcmp(argv[1], "shutdown_exit") == 0)) std::exit(0);
    return 0;
}
