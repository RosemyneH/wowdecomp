execute_process(COMMAND "${REPRO}" caller_exception
    TIMEOUT 3 RESULT_VARIABLE result OUTPUT_VARIABLE output)
if (NOT "${result}" MATCHES "timeout" OR NOT "${output}" MATCHES "caller exception caught")
    message(FATAL_ERROR "Expected caught caller exception followed by timeout; result: ${result}, output: ${output}")
endif ()
