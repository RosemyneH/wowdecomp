file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")
wowdecomp_filter_platform_sources(PRIVATE_SOURCES)

add_library(Images STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(Images
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(Images
    PRIVATE
        jpeg
)
