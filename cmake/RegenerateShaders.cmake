# ʕ •ᴥ•ʔ✿ Single owner of the shader compile flags; used by the Gx regenerate target and CI drift check ✿ ʕ •ᴥ•ʔ
if (NOT GLSLC)
    message(FATAL_ERROR "GLSLC not set")
endif ()
file(READ "${CMAKE_CURRENT_LIST_DIR}/../Engine/Source/Gx/CGxDeviceVulkan/Shaders/glslc.version" EXPECTED_VERSION)
string(STRIP "${EXPECTED_VERSION}" EXPECTED_VERSION)
execute_process(COMMAND ${GLSLC} --version OUTPUT_VARIABLE VERSION_OUT ERROR_VARIABLE VERSION_OUT)
string(FIND "${VERSION_OUT}" "${EXPECTED_VERSION}" VERSION_AT)
file(READ "${CMAKE_CURRENT_LIST_DIR}/../Engine/Source/Gx/CGxDeviceVulkan/Shaders/spirv.version" EXPECTED_SPIRV)
string(STRIP "${EXPECTED_SPIRV}" EXPECTED_SPIRV)
string(FIND "${VERSION_OUT}" "${EXPECTED_SPIRV}" SPIRV_AT)
if (VERSION_AT EQUAL -1 OR SPIRV_AT EQUAL -1)
    message(FATAL_ERROR "glslc version mismatch: need ${EXPECTED_VERSION}, got: ${VERSION_OUT}")
endif ()
set(SHADER_DIR "${CMAKE_CURRENT_LIST_DIR}/../Engine/Source/Gx/CGxDeviceVulkan/Shaders")
foreach (SHADER Ffp.vert Ffp.frag FfpTerrain.frag Fxaa.vert Fxaa.frag Atmosphere.frag)
    execute_process(
        COMMAND ${GLSLC} -O --target-env=vulkan1.1 -mfmt=c -I ${SHADER_DIR}
            -o ${SHADER_DIR}/Generated/${SHADER}.inc ${SHADER_DIR}/${SHADER}
        RESULT_VARIABLE RESULT)
    if (RESULT)
        message(FATAL_ERROR "glslc failed for ${SHADER}")
    endif ()
endforeach ()
