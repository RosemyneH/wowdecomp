file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")
wowdecomp_filter_platform_sources(PRIVATE_SOURCES)

add_library(FFX STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(FFX
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)
