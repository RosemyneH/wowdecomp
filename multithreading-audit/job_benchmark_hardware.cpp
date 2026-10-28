// Linux/GNU-linker benchmark-only hardware-concurrency override. This changes
// the pool sizing input, not the scheduler, affinity or host CPU availability.
#include <cerrno>
#include <cstdio>
#include <cstdlib>

extern "C" unsigned __real__ZNSt6thread20hardware_concurrencyEv() noexcept;
extern "C" unsigned __wrap__ZNSt6thread20hardware_concurrencyEv() noexcept {
    const char* setting = std::getenv("WOW_JOB_BENCH_WORKERS");
    if (!setting) return __real__ZNSt6thread20hardware_concurrencyEv();
    char* end = nullptr;
    errno = 0;
    unsigned long workers = std::strtoul(setting, &end, 10);
    if (errno || !*setting || *end || workers < 1 || workers > 256) {
        std::fputs("WOW_JOB_BENCH_WORKERS must be in [1,256]\n", stderr);
        std::exit(2);
    }
    return static_cast<unsigned>(workers + 2);
}
