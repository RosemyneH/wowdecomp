file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")
wowdecomp_filter_platform_sources(PRIVATE_SOURCES)

add_library(Base STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(Base
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

if (WOWDECOMP_UNIX)
    target_link_libraries(Base PRIVATE Threads::Threads)
endif ()

if (WOWDECOMP_LINUX)
    target_link_libraries(Base PRIVATE rt)
endif ()
