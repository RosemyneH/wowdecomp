file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")
wowdecomp_filter_platform_sources(PRIVATE_SOURCES)

add_library(Os STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(Os
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

if(WIN32)
    target_link_libraries(Os
        PUBLIC
            imm32
            wininet
            ws2_32
            dinput8
            dxguid
            version
    )
elseif(WOWDECOMP_UNIX)
    target_link_libraries(Os
        PUBLIC
            Threads::Threads
            ${CMAKE_DL_LIBS}
    )

    if (WOWDECOMP_LINUX OR WOWDECOMP_ANDROID)
        target_link_libraries(Os PUBLIC SDL3::SDL3)
        target_compile_definitions(Os PUBLIC WOWDECOMP_SDL3=1)
    else ()
        target_link_libraries(Os PUBLIC SDL2::SDL2)
        target_compile_definitions(Os PUBLIC WOWDECOMP_SDL2=1)
    endif ()

    if (WOWDECOMP_ANDROID)
        target_link_libraries(Os PUBLIC log android)
    endif ()

    if (WOWDECOMP_LINUX)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
        pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
        target_sources(Os PRIVATE Linux/protocol/xdg-shell-protocol.c Linux/protocol/xdg-decoration-protocol.c Linux/protocol/linux-dmabuf-protocol.c Linux/protocol/relative-pointer-protocol.c Linux/protocol/pointer-constraints-protocol.c)
        target_link_libraries(Os PUBLIC PkgConfig::WAYLAND_SERVER PkgConfig::XKBCOMMON)
    endif ()

    if (WOWDECOMP_APPLE)
        target_link_libraries(Os
            PUBLIC
                ${COCOA_FRAMEWORK}
                ${IOKIT_FRAMEWORK}
                ${COREFOUNDATION_FRAMEWORK}
                ${COREGRAPHICS_FRAMEWORK}
        )
    endif ()
endif()
