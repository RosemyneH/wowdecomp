include(FetchContent)

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL_TESTS OFF CACHE BOOL "" FORCE)

if (SDL3_ROOT AND EXISTS "${SDL3_ROOT}/CMakeLists.txt")
    add_subdirectory(${SDL3_ROOT} ${CMAKE_BINARY_DIR}/_sdl3)
elseif (NOT TARGET SDL3::SDL3)
    FetchContent_Declare(SDL3
        URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.16.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(SDL3)
endif ()

if (NOT TARGET CURL::libcurl)
    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
    set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
    set(HTTP_ONLY ON CACHE BOOL "" FORCE)
    set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
    set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
    set(CURL_USE_PKGCONFIG OFF CACHE BOOL "" FORCE)
    set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(PICKY_COMPILER OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(curl
        URL https://github.com/curl/curl/releases/download/curl-8_12_1/curl-8.12.1.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(curl)

    if (NOT TARGET CURL::libcurl AND TARGET libcurl)
        add_library(CURL::libcurl ALIAS libcurl)
    elseif (NOT TARGET CURL::libcurl AND TARGET libcurl_static)
        add_library(CURL::libcurl ALIAS libcurl_static)
    endif ()
endif ()

if (NOT TARGET SDL3::SDL3)
    message(FATAL_ERROR "Android build did not produce SDL3::SDL3")
endif ()
if (NOT TARGET CURL::libcurl)
    message(FATAL_ERROR "Android build did not produce CURL::libcurl")
endif ()
