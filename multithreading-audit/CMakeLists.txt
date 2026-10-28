find_package(Threads REQUIRED)

add_executable(job_lifecycle_test
    job_lifecycle_test.cpp
    ${CMAKE_SOURCE_DIR}/Engine/Source/Base/Job/JobSystem.cpp
)
target_include_directories(job_lifecycle_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(job_lifecycle_test PRIVATE Threads::Threads)
add_test(NAME job_lifecycle_test COMMAND job_lifecycle_test)
set_tests_properties(job_lifecycle_test PROPERTIES TIMEOUT 60)
