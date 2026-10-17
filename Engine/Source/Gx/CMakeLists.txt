file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")
wowdecomp_filter_platform_sources_apple_uses_linux(PRIVATE_SOURCES)

if (NOT WIN32 OR WOWDECOMP_WIN_ARM64)
    list(FILTER PRIVATE_SOURCES EXCLUDE REGEX "/CGxDevice(D3d|D3d9Ex|OpenGL)/")
endif ()

if (NOT WOWDECOMP_ENABLE_VULKAN)
    list(FILTER PRIVATE_SOURCES EXCLUDE REGEX "/CGxDeviceVulkan/")
elseif (WIN32)
    list(FILTER PRIVATE_SOURCES EXCLUDE REGEX "/CGxDeviceVulkan/Unix/")
endif ()

add_library(Gx STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(Gx
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

if (WIN32 AND NOT WOWDECOMP_WIN_ARM64)
    target_link_libraries(Gx
        PRIVATE
            d3d9
            nvapi
            opengl32
    )
endif ()

if (WOWDECOMP_ENABLE_VULKAN)
    # ʕ •ᴥ•ʔ✿ SPIR-V is committed under Shaders/Generated; glslc is only needed to regenerate it ✿ ʕ •ᴥ•ʔ
    set(VK_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/CGxDeviceVulkan/Shaders)
    set(VK_SHADER_GENERATED_DIR ${VK_SHADER_SOURCE_DIR}/Generated)
    target_include_directories(Gx PRIVATE ${VK_SHADER_GENERATED_DIR})

    find_program(WOWDECOMP_GLSLC NAMES glslc)
    if (WOWDECOMP_GLSLC)
        add_custom_target(GxVulkanShadersRegenerate
            COMMAND ${CMAKE_COMMAND} -DGLSLC=${WOWDECOMP_GLSLC} -P ${CMAKE_SOURCE_DIR}/cmake/RegenerateShaders.cmake
            VERBATIM)
    endif ()

    target_link_libraries(Gx
        PUBLIC
            volk
    )
endif ()

if (WOWDECOMP_LINUX)
    target_link_libraries(Gx PUBLIC SDL3::SDL3)
elseif (WOWDECOMP_UNIX)
    target_link_libraries(Gx PUBLIC SDL2::SDL2)
endif ()

if (WOWDECOMP_APPLE)
    target_link_libraries(Gx
        PUBLIC
            ${METAL_FRAMEWORK}
            ${QUARTZCORE_FRAMEWORK}
    )
endif ()
