#include "Engine/Source/Base/Job/JobSystem.h"
#include "Engine/Source/Base/Profiler.h"

#include <cstring>

Profiler::Scope::Scope(uint32_t zone) : id(zone), start(0), child(0) {}
Profiler::Scope::~Scope() {}

void Empty(Job::Range, void*) {}

void Nested(Job::Range, void* user) {
    const char* mode = static_cast<const char*>(user);
    if (std::strcmp(mode, "wait") == 0) {
        Job::Wait();
    } else if (std::strcmp(mode, "start") == 0) {
        Job::Start(1, Empty, nullptr);
    } else {
        Job::ParallelFor(1, Empty, nullptr);
    }
}

int main(int argc, char** argv) {
    if (argc != 2 || (std::strcmp(argv[1], "wait") != 0 &&
                      std::strcmp(argv[1], "start") != 0 &&
                      std::strcmp(argv[1], "parallel") != 0)) {
        return 2;
    }
    Job::Init();
    Job::ParallelFor(1, Nested, argv[1]);
    Job::Shutdown();
}
