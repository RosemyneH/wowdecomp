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
            SDL2::SDL2
            Threads::Threads
            ${CMAKE_DL_LIBS}
    )

    target_compile_definitions(Os PUBLIC WOWDECOMP_SDL2=1)

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
