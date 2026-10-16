file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")
wowdecomp_filter_platform_sources(PRIVATE_SOURCES)

add_library(pcreposix STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(pcreposix
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(pcreposix
    PRIVATE
        pcre
)
