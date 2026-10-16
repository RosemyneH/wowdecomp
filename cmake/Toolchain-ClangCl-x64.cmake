# ʕ •ᴥ•ʔ✿ clang-cl x86-64 MSVC verification toolchain; sysroot from `xwin splat` (XWIN_ROOT) ✿ ʕ •ᴥ•ʔ
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if (NOT DEFINED XWIN_ROOT)
    set(XWIN_ROOT "/tmp/xwin" CACHE PATH "xwin splat output directory")
endif ()

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING "")

set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_LINKER lld-link)
set(CMAKE_RC_COMPILER llvm-rc)
set(CMAKE_AR llvm-lib)
set(CMAKE_MT llvm-mt)

add_compile_options(
    -imsvc${XWIN_ROOT}/crt/include
    -imsvc${XWIN_ROOT}/sdk/include/ucrt
    -imsvc${XWIN_ROOT}/sdk/include/um
    -imsvc${XWIN_ROOT}/sdk/include/shared
)
add_link_options(
    /libpath:${XWIN_ROOT}/crt/lib/x86_64
    /libpath:${XWIN_ROOT}/sdk/lib/um/x86_64
    /libpath:${XWIN_ROOT}/sdk/lib/ucrt/x86_64
)
