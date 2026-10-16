file(GLOB PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")

if (WOWDECOMP_LINUX)
    file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS "Linux/*.cpp")
    set(WOW_BIN_DIR ${CMAKE_SOURCE_DIR}/bin/Linux)
elseif (WOWDECOMP_APPLE)
    file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS "Darwin/*.cpp" "Linux/BotCheck.cpp")
    set(WOW_BIN_DIR ${CMAKE_SOURCE_DIR}/bin/Mac)
elseif (WOWDECOMP_ARCH_64)
    file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS "Win64/*.cpp")
    set(WOW_BIN_DIR ${CMAKE_SOURCE_DIR}/bin/Win)
else ()
    file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS "Win32/*.cpp")
    set(WOW_BIN_DIR ${CMAKE_SOURCE_DIR}/bin/Win)
endif ()

if (WIN32)
    add_executable(Wow WIN32 ${PRIVATE_SOURCES} ${PLATFORM_SOURCES})
else ()
    add_executable(Wow ${PRIVATE_SOURCES} ${PLATFORM_SOURCES})
endif ()

if (WOWDECOMP_ARCH_64)
    set(WOW_OUTPUT_NAME Wow64)
else ()
    set(WOW_OUTPUT_NAME Wow32)
endif ()

set_target_properties(Wow PROPERTIES
    OUTPUT_NAME ${WOW_OUTPUT_NAME}
    RUNTIME_OUTPUT_DIRECTORY ${WOW_BIN_DIR}
)

foreach (config ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
    string(TOUPPER ${config} config)
    set_target_properties(Wow PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} ${WOW_BIN_DIR})
endforeach ()

target_include_directories(Wow
    PRIVATE
        ${CMAKE_SOURCE_DIR}
)

if (WIN32)
    target_link_options(Wow PRIVATE "/INCREMENTAL:NO" "/ENTRY:start")
endif ()

set(WOW_LIBS
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
        LiquidSystem
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
        WowServices
        WowSvcs
        Os
)

if (WIN32 OR WOWDECOMP_APPLE)
    target_link_libraries(Wow PRIVATE ${WOW_LIBS})
else ()
    list(JOIN WOW_LIBS "," WOW_LIBS_GROUP)
    target_link_libraries(Wow PRIVATE "$<LINK_GROUP:RESCAN,${WOW_LIBS_GROUP}>")
endif ()
