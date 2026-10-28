find_package(Threads REQUIRED)

add_executable(job_exit_repro
    job_exit_repro.cpp
    ${CMAKE_SOURCE_DIR}/Engine/Source/Base/Job/JobSystem.cpp
)
target_include_directories(job_exit_repro PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(job_exit_repro PRIVATE Threads::Threads)
add_test(NAME job_exit_shutdown COMMAND job_exit_repro shutdown)
add_test(NAME job_exit_without_shutdown COMMAND ${CMAKE_COMMAND}
    -DREPRO=$<TARGET_FILE:job_exit_repro>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_job_exit.cmake)
add_test(NAME job_exit_via_std_exit COMMAND ${CMAKE_COMMAND}
    -DREPRO=$<TARGET_FILE:job_exit_repro> -DMODE=exit
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_job_exit.cmake)
set_tests_properties(job_exit_shutdown job_exit_without_shutdown job_exit_via_std_exit PROPERTIES TIMEOUT 60)

add_executable(job_lifecycle_test
    job_lifecycle_test.cpp
    ${CMAKE_SOURCE_DIR}/Engine/Source/Base/Job/JobSystem.cpp
)
target_include_directories(job_lifecycle_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(job_lifecycle_test PRIVATE Threads::Threads)
add_test(NAME job_lifecycle_test COMMAND job_lifecycle_test)
set_tests_properties(job_lifecycle_test PROPERTIES TIMEOUT 60)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID)
    add_library(job_init_failure_inject SHARED job_init_failure_inject.cpp)
    target_link_libraries(job_init_failure_inject PRIVATE ${CMAKE_DL_LIBS})
    add_executable(job_init_failure_repro
        job_init_failure_repro.cpp
        ${CMAKE_SOURCE_DIR}/Engine/Source/Base/Job/JobSystem.cpp
    )
    target_include_directories(job_init_failure_repro PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(job_init_failure_repro PRIVATE Threads::Threads)
    foreach(attempt 1 2 4 8)
        add_test(NAME job_init_failure_${attempt} COMMAND ${CMAKE_COMMAND} -E env
            LD_PRELOAD=$<TARGET_FILE:job_init_failure_inject> JOB_FAIL_CREATE_ATTEMPT=${attempt}
            $<TARGET_FILE:job_init_failure_repro>)
        set_tests_properties(job_init_failure_${attempt} PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
    endforeach()
endif ()

add_executable(emergency_mem_test
    emergency_mem_test.cpp
    ${CMAKE_SOURCE_DIR}/Engine/Source/Gx/CGxDevice/EmergencyMem.cpp
)
target_include_directories(emergency_mem_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME emergency_mem_test COMMAND emergency_mem_test)
set_tests_properties(emergency_mem_test PROPERTIES TIMEOUT 60)
