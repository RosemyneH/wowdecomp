if (CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
    add_library(XvidDecoder SHARED
        XvidDecoderWoA.cpp
        XvidDecoder.def
    )
else ()
    add_library(XvidDecoder SHARED
        XvidDecoder.cpp
        XvidDecoder.def
    )

    target_link_libraries(XvidDecoder
        PRIVATE
            ${CMAKE_SOURCE_DIR}/ThirdParty/xvidcore/lib/x64/libxvidcore.lib
    )
endif ()

set_target_properties(XvidDecoder PROPERTIES
    OUTPUT_NAME XvidDecoder
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/Win
)

foreach (config ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
    string(TOUPPER ${config} config)
    set_target_properties(XvidDecoder PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} ${CMAKE_SOURCE_DIR}/bin/Win)
endforeach ()

target_include_directories(XvidDecoder
    PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/ThirdParty/xvidcore/include
)
