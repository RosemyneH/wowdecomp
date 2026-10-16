file(GLOB CLIENT_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/WoW/Source/*.cpp")

if (WOWDECOMP_LINUX)
    file(GLOB CLIENT_PLATFORM_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/WoW/Source/Linux/*.cpp")
    set(WOWCLIENT_OUTPUT_NAME WowClient64)
    set(WOWCLIENT_BIN_DIR ${CMAKE_SOURCE_DIR}/bin/Linux)
elseif (WOWDECOMP_APPLE)
    file(GLOB CLIENT_PLATFORM_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/WoW/Source/Darwin/*.cpp" "${CMAKE_SOURCE_DIR}/WoW/Source/Linux/BotCheck.cpp")
    set(WOWCLIENT_OUTPUT_NAME WowClient64)
    set(WOWCLIENT_BIN_DIR ${CMAKE_SOURCE_DIR}/bin/Mac)
elseif (WOWDECOMP_ARCH_64)
    file(GLOB CLIENT_PLATFORM_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/WoW/Source/Win64/*.cpp")
    set(WOWCLIENT_OUTPUT_NAME WowClient64)
    set(WOWCLIENT_BIN_DIR ${CMAKE_SOURCE_DIR}/bin/Win)
else ()
    file(GLOB CLIENT_PLATFORM_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/WoW/Source/Win32/*.cpp")
    set(WOWCLIENT_OUTPUT_NAME WowClient32)
    set(WOWCLIENT_BIN_DIR ${CMAKE_SOURCE_DIR}/bin/Win)
endif ()

set(WOWCLIENT_OUTPUT_OVERRIDE "" CACHE STRING "WowClient executable name instead of WowClient64/WowClient32 (re/tools/build sets Waystone / WaystoneDev)")
if (WOWCLIENT_OUTPUT_OVERRIDE)
    set(WOWCLIENT_OUTPUT_NAME ${WOWCLIENT_OUTPUT_OVERRIDE})
endif ()

option(WOWCLIENT_CRASH_LOG "Crash logger with console window and WowClient_crash.log (re/tools/build release mode turns it off)" ON)
set(CLIENT_TOOL_SOURCES "")
if (WOWCLIENT_CRASH_LOG AND WIN32)
    list(APPEND CLIENT_TOOL_SOURCES crash_log.cpp)
endif ()

if (WIN32)
    add_executable(WowClient WIN32 ${CLIENT_SOURCES} ${CLIENT_PLATFORM_SOURCES} ${CLIENT_TOOL_SOURCES})
else ()
    add_executable(WowClient ${CLIENT_SOURCES} ${CLIENT_PLATFORM_SOURCES} ${CLIENT_TOOL_SOURCES})
endif ()

if (WOWDECOMP_APPLE)
    # ʕ •ᴥ•ʔ✿ Darwin runs the client on the main thread (no ucontext fiber), so guarantee its stack ✿ ʕ •ᴥ•ʔ
    target_link_options(WowClient PRIVATE "-Wl,-stack_size,0x800000")
endif ()

set_target_properties(WowClient PROPERTIES
    OUTPUT_NAME ${WOWCLIENT_OUTPUT_NAME}
    RUNTIME_OUTPUT_DIRECTORY ${WOWCLIENT_BIN_DIR}
)

foreach (config ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
    string(TOUPPER ${config} config)
    set_target_properties(WowClient PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} ${WOWCLIENT_BIN_DIR})
endforeach ()

target_include_directories(WowClient
    PRIVATE
        ${CMAKE_SOURCE_DIR}
)

if (WIN32)
    target_link_options(WowClient PRIVATE "/INCREMENTAL:NO" "/ENTRY:start")
    target_link_libraries(WowClient PRIVATE dbghelp)
endif ()

set(WOWCLIENT_LIBS
        BLPFile
        Base
        BlizzardCore
        ComSat
        ConsoleBase
        Event
        FFX
        Frame
        FrameScript
        WasmAddon
        lua
        FrameXML
        Gx
        Gxu
        Images
        Model2
        Scrn
        Services
        ShaderEffect
        Tempest
        SoundEngine
        SteelseriesMouse
        Storm
        WoWCommon
        Battlenet
        Console
        ConsoleDevice
        DB
        Game
        Glue
        Magic
        Minigame
        Net
        Object
        ObjectAlloc
        ObjectMgrClient
        SoundInterface
        UIUtil
        Ui
        WorldClient
        LiquidSystem
        WowServices
        WowSvcs
        Os
)

if (WIN32 OR WOWDECOMP_APPLE)
    target_link_libraries(WowClient PRIVATE ${WOWCLIENT_LIBS})
else ()
    list(JOIN WOWCLIENT_LIBS "," WOWCLIENT_LIBS_GROUP)
    target_link_libraries(WowClient PRIVATE "$<LINK_GROUP:RESCAN,${WOWCLIENT_LIBS_GROUP}>")
endif ()
