set(_wowdecomp_video_lua "${CMAKE_SOURCE_DIR}/WoW/Interface/FrameXML/WowDecompVideoOptions.lua")
set(_wowdecomp_video_lua_cpp "${CMAKE_CURRENT_BINARY_DIR}/WowDecompVideoOptionsLua.cpp")
add_custom_command(
    OUTPUT "${_wowdecomp_video_lua_cpp}"
    DEPENDS "${_wowdecomp_video_lua}" "${CMAKE_SOURCE_DIR}/cmake/EmbedLua.cmake"
    COMMAND ${CMAKE_COMMAND} -DINPUT=${_wowdecomp_video_lua} -DOUTPUT=${_wowdecomp_video_lua_cpp} -P ${CMAKE_SOURCE_DIR}/cmake/EmbedLua.cmake
)

file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")
wowdecomp_filter_platform_sources_apple_uses_linux(PRIVATE_SOURCES)
list(APPEND PRIVATE_SOURCES "${_wowdecomp_video_lua_cpp}")

if (NOT WIN32)
    list(FILTER PRIVATE_SOURCES EXCLUDE REGEX "/UILCD/LCD\\.cpp$")
endif ()

add_library(Ui STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(Ui
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

if (WIN32)
    target_link_libraries(Ui
        PRIVATE
            lgLcd
    )
endif ()
