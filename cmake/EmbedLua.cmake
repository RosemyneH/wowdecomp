if(NOT INPUT OR NOT OUTPUT)
    message(FATAL_ERROR "EmbedLua.cmake requires INPUT and OUTPUT")
endif()

file(READ "${INPUT}" _lua)
set(_chunk_size 12000)
string(LENGTH "${_lua}" _len)
file(WRITE "${OUTPUT}" "extern \"C\" const char kWowDecompVideoOptionsLua[] =\n")

set(_pos 0)
while(_pos LESS _len)
    math(EXPR _remain "${_len} - ${_pos}")
    if(_remain GREATER _chunk_size)
        string(SUBSTRING "${_lua}" ${_pos} ${_chunk_size} _chunk)
        math(EXPR _pos "${_pos} + ${_chunk_size}")
    else()
        string(SUBSTRING "${_lua}" ${_pos} ${_remain} _chunk)
        set(_pos ${_len})
    endif()
    # ʕ •ᴥ•ʔ✿ Chunks concatenate with no added newline. A break inside a quote is an unfinished string in Lua 5.1 ✿ ʕ •ᴥ•ʔ
    file(APPEND "${OUTPUT}" "R\"WOWDECOMP(${_chunk})WOWDECOMP\"\n")
endwhile()

file(APPEND "${OUTPUT}" ";\n")
