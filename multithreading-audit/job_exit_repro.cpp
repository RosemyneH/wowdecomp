#include "Engine/Source/Base/Job/JobSystem.h"
#include "Engine/Source/Base/Profiler.h"

#include <cstdlib>
#include <exception>

Profiler::Scope::Scope(uint32_t zone) : id(zone), start(0), child(0) {}
Profiler::Scope::~Scope() {}

int main(int argc, char**) {
    std::set_terminate([] { std::_Exit(42); });
    Job::Init();
    if (argc > 1) Job::Shutdown();
    return 0;
}
