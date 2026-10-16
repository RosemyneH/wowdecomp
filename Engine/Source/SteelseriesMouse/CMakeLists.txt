file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")
wowdecomp_filter_platform_sources_apple_uses_linux(PRIVATE_SOURCES)

add_library(SteelseriesMouse STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(SteelseriesMouse
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

if (WIN32)
    target_link_libraries(SteelseriesMouse
        PUBLIC
            hid
            setupapi
    )
endif ()
