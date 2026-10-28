# Retained for scripts using the historical entry point: caller exceptions must
# terminate promptly rather than leave an unjoinable pooled batch.
set(MODE caller_exception)
include("${CMAKE_CURRENT_LIST_DIR}/check_job_exit.cmake")
