file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")
wowdecomp_filter_platform_sources(PRIVATE_SOURCES)

add_library(Model2 STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(Model2
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

# ʕ •ᴥ•ʔ✿ AVX2 skinning is its own TU. Win32 keeps /arch:IA32 for the x87 layout build. ✿ ʕ •ᴥ•ʔ
set(WOWDECOMP_AVX2_SKIN 0)
if (NOT MSVC OR CMAKE_SIZEOF_VOID_P EQUAL 8)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i[3-6]86|x86)$")
        set(WOWDECOMP_AVX2_SKIN 1)
    endif ()
endif ()

if (WOWDECOMP_AVX2_SKIN)
    target_compile_definitions(Model2 PRIVATE WOWDECOMP_AVX2_SKIN=1)
    if (MSVC)
        set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/M2SkinAvx2.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
    else ()
        set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/M2SkinAvx2.cpp PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma")
    endif ()
endif ()
