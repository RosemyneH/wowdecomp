execute_process(COMMAND "${REPRO}" ${MODE} RESULT_VARIABLE result)
if (NOT "${result}" STREQUAL "42")
    message(FATAL_ERROR "Expected terminate handler exit 42, got: ${result}")
endif ()
