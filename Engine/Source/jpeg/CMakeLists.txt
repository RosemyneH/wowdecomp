file(GLOB PRIVATE_SOURCES CONFIGURE_DEPENDS "*.c" "*.cpp")
wowdecomp_filter_platform_sources(PRIVATE_SOURCES)

set_source_files_properties(${PRIVATE_SOURCES} PROPERTIES LANGUAGE CXX)

add_library(jpeg STATIC
    ${PRIVATE_SOURCES}
)

# ʕ •ᴥ•ʔ✿ Vendored C compiled as C++ (above); Clang makes C++17 'register' an error ✿ ʕ •ᴥ•ʔ
target_compile_options(jpeg PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-register>)

target_include_directories(jpeg
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)
