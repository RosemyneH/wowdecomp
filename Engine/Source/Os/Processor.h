#ifndef ENGINE_SOURCE_OS_PROCESSOR_H
#define ENGINE_SOURCE_OS_PROCESSOR_H

#include <cstdint>

uint32_t OsGetProcessorFeatures();
uint32_t OsGetProcessorFeaturesEx(int32_t& vendor);
uint64_t OsGetProcessorTicksPerSecond();

// ʕ •ᴥ•ʔ✿ AVX2 and FMA, and the OS has YMM state on. Separate from the 2010 feature mask. ✿ ʕ •ᴥ•ʔ
int32_t OsHasAvx2();

#endif
