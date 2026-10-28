#include "Engine/Source/Base/Job/JobSystem.h"
#include "Engine/Source/Base/Profiler.h"

#include <atomic>
#include <cstdlib>
#include <exception>
#include <cstring>
#include <cstdio>
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

void Throw(Job::Range, void*) {
    throw 1;
}

struct CallerThrowJob {
    std::thread::id caller = std::this_thread::get_id();
    std::atomic<bool> release{false};
};

void ThrowOnCaller(Job::Range, void* user) {
    auto& job = *static_cast<CallerThrowJob*>(user);
    if (std::this_thread::get_id() == job.caller) throw 1;
    while (!job.release.load()) std::this_thread::yield();
}

int main(int argc, char** argv) {
    std::set_terminate([] { std::_Exit(42); });
    if (argc > 1 && std::strcmp(argv[1], "fallback_exception") == 0) {
        try { Job::ParallelFor(1, Throw, nullptr); } catch (int) { return 0; }
        return 1;
    }
    Job::Init();
    if (argc > 1 && std::strcmp(argv[1], "caller_exception") == 0) {
        CallerThrowJob job;
        try { Job::ParallelFor(Job::WorkerCount() + 1, ThrowOnCaller, &job); }
        catch (int) {
            job.release.store(true);
            std::puts("caller exception caught");
            std::fflush(stdout);
            Job::Wait();
            Job::Shutdown();
            return 1;
        }
        Job::Shutdown();
        return 1;
    }
    if (argc > 1 && std::strcmp(argv[1], "worker_exception") == 0) {
        Job::Start(1, Throw, nullptr);
        for (;;) std::this_thread::yield();
    }
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
