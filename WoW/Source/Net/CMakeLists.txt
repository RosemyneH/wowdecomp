file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")
wowdecomp_filter_platform_sources(PRIVATE_SOURCES)
add_library(Net STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(Net
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

if (WIN32)
    target_link_libraries(Net
        PUBLIC
            ws2_32
            wsock32
    )
endif ()
