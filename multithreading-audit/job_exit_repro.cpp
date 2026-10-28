#include "Engine/Source/Base/Job/JobSystem.h"
#include "Engine/Source/Base/Profiler.h"

#include <cstdlib>
#include <exception>
#include <cstring>

Profiler::Scope::Scope(uint32_t zone) : id(zone), start(0), child(0) {}
Profiler::Scope::~Scope() {}

int main(int argc, char** argv) {
    std::set_terminate([] { std::_Exit(42); });
    Job::Init();
    if (argc > 1 && (std::strcmp(argv[1], "shutdown") == 0 ||
                     std::strcmp(argv[1], "shutdown_exit") == 0)) Job::Shutdown();
    if (argc > 1 && (std::strcmp(argv[1], "exit") == 0 ||
                     std::strcmp(argv[1], "shutdown_exit") == 0)) std::exit(0);
    return 0;
}
