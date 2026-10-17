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

find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
    pkg_check_modules(LIBJXL IMPORTED_TARGET libjxl)
endif ()

if (TARGET PkgConfig::LIBJXL)
    target_compile_definitions(Images PRIVATE WOW_HAS_JXL=1)
    target_link_libraries(Images PRIVATE PkgConfig::LIBJXL)
endif ()
