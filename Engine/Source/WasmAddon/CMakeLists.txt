include(${CMAKE_SOURCE_DIR}/cmake/Wasmtime.cmake)

file(GLOB PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")
wowdecomp_filter_platform_sources(PRIVATE_SOURCES)

add_library(WasmAddon STATIC ${PRIVATE_SOURCES})

target_include_directories(WasmAddon PUBLIC ${CMAKE_SOURCE_DIR})

if (MINGW)
    # ʕ ◕ᴥ◕ ʔ✿ Self-contained MinGW binaries: no libgcc/libstdc++ DLLs needed at runtime ✿ ʕ ◕ᴥ◕ ʔ
    target_link_options(WasmAddon PUBLIC -static-libgcc -static-libstdc++)
endif ()

if (WASMTIME_FOUND)
    target_link_libraries(WasmAddon PUBLIC Wasmtime::Wasmtime)
    target_compile_definitions(WasmAddon PRIVATE WOWDECOMP_HAS_WASMTIME=1)
endif ()
