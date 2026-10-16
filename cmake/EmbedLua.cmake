if(NOT INPUT OR NOT OUTPUT)
    message(FATAL_ERROR "EmbedLua.cmake requires INPUT and OUTPUT")
endif()

file(READ "${INPUT}" _lua)
file(WRITE "${OUTPUT}" "extern \"C\" const char kWowDecompVideoOptionsLua[] = R\"WOWDECOMP(\n")
file(APPEND "${OUTPUT}" "${_lua}")
file(APPEND "${OUTPUT}" ")WOWDECOMP\";\n")
