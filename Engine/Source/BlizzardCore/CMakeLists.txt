file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")
wowdecomp_filter_platform_sources_apple_uses_linux(PRIVATE_SOURCES)

add_library(BlizzardCore STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(BlizzardCore
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(BlizzardCore
    PUBLIC
        blast
        lzma
        zlib
)

if(WIN32)
    target_link_libraries(BlizzardCore
        PUBLIC
            wininet
    )
elseif(WOWDECOMP_UNIX)
    find_package(CURL REQUIRED)

    target_link_libraries(BlizzardCore
        PUBLIC
            Threads::Threads
            ${CMAKE_DL_LIBS}
        PRIVATE
            CURL::libcurl
    )
endif()

target_include_directories(BlizzardCore
    PRIVATE
        ${CMAKE_SOURCE_DIR}/Engine/Source/SoundEngine/fmodex/lib/ogg_vorbis/ogg/include
        ${CMAKE_SOURCE_DIR}/Engine/Source/SoundEngine/fmodex/lib/ogg_vorbis/vorbis/include
)
