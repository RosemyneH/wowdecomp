file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")
wowdecomp_filter_platform_sources_apple_uses_linux(PRIVATE_SOURCES)

add_library(ComSat STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(ComSat
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)
