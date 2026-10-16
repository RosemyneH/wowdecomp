add_executable(wasm_addon_host main.cpp)
target_link_libraries(wasm_addon_host PRIVATE WasmAddon)

add_executable(wasm_addon_tests tests.cpp)
target_link_libraries(wasm_addon_tests PRIVATE WasmAddon)

add_executable(wasm_storage_fuzz fuzz.cpp
    ${CMAKE_SOURCE_DIR}/Engine/Source/WasmAddon/WasmStorageFormat.cpp
    ${CMAKE_SOURCE_DIR}/Engine/Source/WasmAddon/WasmManifest.cpp)
target_include_directories(wasm_storage_fuzz PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME wasm_storage_fuzz COMMAND wasm_storage_fuzz --iterations 20000)

add_executable(wasm_addon_bench bench.cpp)
if (UNIX)
    target_link_libraries(wasm_addon_bench PRIVATE "$<LINK_GROUP:RESCAN,WasmAddon,lua,Base,Os,BlizzardCore,Storm>")
else ()
    target_link_libraries(wasm_addon_bench PRIVATE WasmAddon lua Base Os BlizzardCore Storm)
endif ()

enable_testing()
add_test(NAME wasm_addon_tests COMMAND wasm_addon_tests ${CMAKE_SOURCE_DIR}/Addons/Wasm/dist)
