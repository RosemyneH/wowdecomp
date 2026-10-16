file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")
wowdecomp_filter_platform_sources(PRIVATE_SOURCES)

add_library(ObjectMgrClient STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(ObjectMgrClient
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)
