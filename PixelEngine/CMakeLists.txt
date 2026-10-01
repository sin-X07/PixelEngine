set(CMAKE_MAKE_PROGRAM "D:/mingw/x86_64-15.2.0-release-win32-seh-ucrt-rt_v13-rev0/mingw64/bin/mingw32-make.exe" CACHE FILEPATH "" FORCE)

# Disable RC language before any add_subdirectory calls
if(MINGW OR MSYS)
  set(CMAKE_RC_COMPILER "")
  set(CMAKE_RC_COMPILER_WORKS TRUE)
endif()

cmake_minimum_required(VERSION 3.16)
project(PixelGameEngine LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)

# Build SDL2 only as static library (no RC file needed)
set(SDL2_DISABLE_SDL2MAIN OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "Build SDL2 as static library" FORCE)

# ---- SDL2: use local source ----
set(SDL2_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../build/_deps/sdl2-src")
set(SDL2_BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/sdl2-build")
add_subdirectory(${SDL2_SOURCE_DIR} ${SDL2_BINARY_DIR})

if(NOT PIXELENGINE_FREETYPE_SOURCE_DIR)
    set(PIXELENGINE_FREETYPE_SOURCE_DIR
        "D:/PixelEngineBuild-ttf/_deps/freetype-src"
        CACHE PATH "Local FreeType source directory" FORCE
    )
endif()
if(NOT PIXELENGINE_SDL2_TTF_SOURCE_DIR)
    set(PIXELENGINE_SDL2_TTF_SOURCE_DIR
        "D:/PixelEngineBuild-ttf/_deps/sdl2_ttf-src"
        CACHE PATH "Local SDL_ttf source directory" FORCE
    )
endif()

foreach(source_dir IN ITEMS
    "${PIXELENGINE_FREETYPE_SOURCE_DIR}"
    "${PIXELENGINE_SDL2_TTF_SOURCE_DIR}"
)
    if(NOT EXISTS "${source_dir}/CMakeLists.txt")
        message(FATAL_ERROR
            "Required local dependency source is missing: ${source_dir}. "
            "Set PIXELENGINE_FREETYPE_SOURCE_DIR and PIXELENGINE_SDL2_TTF_SOURCE_DIR."
        )
    endif()
endforeach()

set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
add_subdirectory(
    "${PIXELENGINE_FREETYPE_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/_deps/freetype-build"
)
if(TARGET freetype AND NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()

set(SDL2TTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL2TTF_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL2TTF_VENDORED OFF CACHE BOOL "" FORCE)
set(FREETYPE_INCLUDE_DIR_ft2build "${CMAKE_BINARY_DIR}/_deps/freetype-build/include" CACHE PATH "" FORCE)
set(FREETYPE_INCLUDE_DIR_freetype2 "${PIXELENGINE_FREETYPE_SOURCE_DIR}/include" CACHE PATH "" FORCE)
set(FREETYPE_LIBRARY freetype CACHE STRING "" FORCE)
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(
    "${PIXELENGINE_SDL2_TTF_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/_deps/sdl2_ttf-build"
)

# ---- SDL2_mixer: download with bounded timeouts, then use a local source fallback ----
set(PIXELENGINE_SDL2_MIXER_SOURCE_DIR "" CACHE PATH "Local SDL2_mixer source directory")
set(PIXELENGINE_SDL2_MIXER_DOWNLOAD_TIMEOUT 300 CACHE STRING "SDL2_mixer download timeout in seconds")
set(PIXELENGINE_SDL2_MIXER_INACTIVITY_TIMEOUT 60 CACHE STRING "SDL2_mixer download inactivity timeout in seconds")

set(_pixelengine_sdl2_mixer_version "2.6.3")
set(_pixelengine_sdl2_mixer_sha256 "91DD065E9E63F499E5317350B110184B0BA96BC5F63C39B3A9939A136C40C035")
set(_pixelengine_sdl2_mixer_download_dir "${CMAKE_BINARY_DIR}/_deps")
set(_pixelengine_sdl2_mixer_download_source "${_pixelengine_sdl2_mixer_download_dir}/SDL_mixer-release-${_pixelengine_sdl2_mixer_version}")
set(_pixelengine_sdl2_mixer_local_fallback "${CMAKE_SOURCE_DIR}/../build/_deps/SDL2_mixer-src")
set(_pixelengine_sdl2_mixer_source_dir "")

if(PIXELENGINE_SDL2_MIXER_SOURCE_DIR AND
   EXISTS "${PIXELENGINE_SDL2_MIXER_SOURCE_DIR}/CMakeLists.txt" AND
   EXISTS "${PIXELENGINE_SDL2_MIXER_SOURCE_DIR}/include/SDL_mixer.h")
    set(_pixelengine_sdl2_mixer_source_dir "${PIXELENGINE_SDL2_MIXER_SOURCE_DIR}")
elseif(PIXELENGINE_SDL2_MIXER_SOURCE_DIR)
    message(WARNING "PIXELENGINE_SDL2_MIXER_SOURCE_DIR is not a valid SDL2_mixer source directory; trying the online source.")
endif()

if(NOT _pixelengine_sdl2_mixer_source_dir AND
   EXISTS "${_pixelengine_sdl2_mixer_download_source}/CMakeLists.txt" AND
   EXISTS "${_pixelengine_sdl2_mixer_download_source}/include/SDL_mixer.h")
    set(_pixelengine_sdl2_mixer_source_dir "${_pixelengine_sdl2_mixer_download_source}")
    message(STATUS "Using cached SDL2_mixer ${_pixelengine_sdl2_mixer_version} source.")
endif()

if(NOT _pixelengine_sdl2_mixer_source_dir)
    file(MAKE_DIRECTORY "${_pixelengine_sdl2_mixer_download_dir}")
    set(_pixelengine_sdl2_mixer_archive "${_pixelengine_sdl2_mixer_download_dir}/SDL_mixer-${_pixelengine_sdl2_mixer_version}.tar.gz")
    file(DOWNLOAD
        "https://codeload.github.com/libsdl-org/SDL_mixer/tar.gz/refs/tags/release-${_pixelengine_sdl2_mixer_version}"
        "${_pixelengine_sdl2_mixer_archive}"
        EXPECTED_HASH "SHA256=${_pixelengine_sdl2_mixer_sha256}"
        TLS_VERIFY ON
        SHOW_PROGRESS
        TIMEOUT "${PIXELENGINE_SDL2_MIXER_DOWNLOAD_TIMEOUT}"
        INACTIVITY_TIMEOUT "${PIXELENGINE_SDL2_MIXER_INACTIVITY_TIMEOUT}"
        STATUS _pixelengine_sdl2_mixer_download_status
    )
    list(GET _pixelengine_sdl2_mixer_download_status 0 _pixelengine_sdl2_mixer_download_code)

    if(_pixelengine_sdl2_mixer_download_code EQUAL 0)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E tar xzf "${_pixelengine_sdl2_mixer_archive}"
            WORKING_DIRECTORY "${_pixelengine_sdl2_mixer_download_dir}"
            RESULT_VARIABLE _pixelengine_sdl2_mixer_extract_result
        )
        if("${_pixelengine_sdl2_mixer_extract_result}" STREQUAL "0" AND
           EXISTS "${_pixelengine_sdl2_mixer_download_source}/CMakeLists.txt" AND
           EXISTS "${_pixelengine_sdl2_mixer_download_source}/include/SDL_mixer.h")
            set(_pixelengine_sdl2_mixer_source_dir "${_pixelengine_sdl2_mixer_download_source}")
        else()
            message(WARNING "SDL2_mixer ${_pixelengine_sdl2_mixer_version} could not be extracted; checking the local source fallback.")
        endif()
    else()
        list(GET _pixelengine_sdl2_mixer_download_status 1 _pixelengine_sdl2_mixer_download_message)
        message(WARNING "Online SDL2_mixer download failed (${_pixelengine_sdl2_mixer_download_message}); checking the local source fallback.")
    endif()
endif()

if(NOT _pixelengine_sdl2_mixer_source_dir AND
   EXISTS "${_pixelengine_sdl2_mixer_local_fallback}/CMakeLists.txt" AND
   EXISTS "${_pixelengine_sdl2_mixer_local_fallback}/include/SDL_mixer.h")
    set(_pixelengine_sdl2_mixer_source_dir "${_pixelengine_sdl2_mixer_local_fallback}")
    message(STATUS "Using local SDL2_mixer source: ${_pixelengine_sdl2_mixer_source_dir}")
endif()

if(NOT _pixelengine_sdl2_mixer_source_dir)
    message(FATAL_ERROR
        "SDL2_mixer could not be downloaded or found locally. Download SDL_mixer release-${_pixelengine_sdl2_mixer_version}, extract it, then set PIXELENGINE_SDL2_MIXER_SOURCE_DIR to its source directory. The default fallback is ${_pixelengine_sdl2_mixer_local_fallback}."
    )
endif()

set(SDL2MIXER_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_CMD OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_FLAC OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MOD OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MP3 OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MIDI OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_OPUS OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_VORBIS "STB" CACHE STRING "" FORCE)
set(SDL2MIXER_WAVPACK OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(
    "${_pixelengine_sdl2_mixer_source_dir}"
    "${CMAKE_BINARY_DIR}/_deps/sdl2_mixer-build"
)

# ---- SDL2_image: PNG/JPG support with the bundled stb backend ----
set(PIXELENGINE_SDL2_IMAGE_SOURCE_DIR "" CACHE PATH "Local SDL2_image source directory")
set(_pixelengine_sdl2_image_version "2.8.2")
set(_pixelengine_sdl2_image_sha256 "8FD59B2C17772D7AC1192B11C645BE8D7874F595C1714F4B200EE70B7CC38F3E")
set(_pixelengine_sdl2_image_download_dir "${CMAKE_BINARY_DIR}/_deps")
set(_pixelengine_sdl2_image_download_source "${_pixelengine_sdl2_image_download_dir}/SDL_image-release-${_pixelengine_sdl2_image_version}")
set(_pixelengine_sdl2_image_local_fallback "${CMAKE_SOURCE_DIR}/../build/_deps/SDL_image-release-${_pixelengine_sdl2_image_version}")
set(_pixelengine_sdl2_image_source_dir "")

if(PIXELENGINE_SDL2_IMAGE_SOURCE_DIR AND
   EXISTS "${PIXELENGINE_SDL2_IMAGE_SOURCE_DIR}/CMakeLists.txt" AND
   EXISTS "${PIXELENGINE_SDL2_IMAGE_SOURCE_DIR}/include/SDL_image.h")
    set(_pixelengine_sdl2_image_source_dir "${PIXELENGINE_SDL2_IMAGE_SOURCE_DIR}")
elseif(PIXELENGINE_SDL2_IMAGE_SOURCE_DIR)
    message(WARNING "PIXELENGINE_SDL2_IMAGE_SOURCE_DIR is not a valid SDL2_image source directory; trying the online source.")
endif()

if(NOT _pixelengine_sdl2_image_source_dir AND
   EXISTS "${_pixelengine_sdl2_image_download_source}/CMakeLists.txt" AND
   EXISTS "${_pixelengine_sdl2_image_download_source}/include/SDL_image.h")
    set(_pixelengine_sdl2_image_source_dir "${_pixelengine_sdl2_image_download_source}")
    message(STATUS "Using cached SDL2_image ${_pixelengine_sdl2_image_version} source.")
endif()

if(NOT _pixelengine_sdl2_image_source_dir AND
   EXISTS "${_pixelengine_sdl2_image_local_fallback}/CMakeLists.txt" AND
   EXISTS "${_pixelengine_sdl2_image_local_fallback}/include/SDL_image.h")
    set(_pixelengine_sdl2_image_source_dir "${_pixelengine_sdl2_image_local_fallback}")
    message(STATUS "Using local SDL2_image source: ${_pixelengine_sdl2_image_source_dir}")
endif()

if(NOT _pixelengine_sdl2_image_source_dir)
    file(MAKE_DIRECTORY "${_pixelengine_sdl2_image_download_dir}")
    set(_pixelengine_sdl2_image_archive "${_pixelengine_sdl2_image_download_dir}/SDL2_image-${_pixelengine_sdl2_image_version}.tar.gz")
    file(DOWNLOAD
        "https://codeload.github.com/libsdl-org/SDL_image/tar.gz/refs/tags/release-${_pixelengine_sdl2_image_version}"
        "${_pixelengine_sdl2_image_archive}"
        EXPECTED_HASH "SHA256=${_pixelengine_sdl2_image_sha256}"
        TLS_VERIFY ON
        SHOW_PROGRESS
        TIMEOUT "${PIXELENGINE_SDL2_MIXER_DOWNLOAD_TIMEOUT}"
        INACTIVITY_TIMEOUT "${PIXELENGINE_SDL2_MIXER_INACTIVITY_TIMEOUT}"
        STATUS _pixelengine_sdl2_image_download_status
    )
    list(GET _pixelengine_sdl2_image_download_status 0 _pixelengine_sdl2_image_download_code)

    if(_pixelengine_sdl2_image_download_code EQUAL 0)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E tar xzf "${_pixelengine_sdl2_image_archive}"
            WORKING_DIRECTORY "${_pixelengine_sdl2_image_download_dir}"
            RESULT_VARIABLE _pixelengine_sdl2_image_extract_result
        )
        if("${_pixelengine_sdl2_image_extract_result}" STREQUAL "0" AND
           EXISTS "${_pixelengine_sdl2_image_download_source}/CMakeLists.txt" AND
           EXISTS "${_pixelengine_sdl2_image_download_source}/include/SDL_image.h")
            set(_pixelengine_sdl2_image_source_dir "${_pixelengine_sdl2_image_download_source}")
        else()
            message(WARNING "SDL2_image ${_pixelengine_sdl2_image_version} could not be extracted; checking the local source fallback.")
        endif()
    else()
        list(GET _pixelengine_sdl2_image_download_status 1 _pixelengine_sdl2_image_download_message)
        message(WARNING "Online SDL2_image download failed (${_pixelengine_sdl2_image_download_message}); checking the local source fallback.")
    endif()
endif()

if(NOT _pixelengine_sdl2_image_source_dir AND
   EXISTS "${_pixelengine_sdl2_image_local_fallback}/CMakeLists.txt" AND
   EXISTS "${_pixelengine_sdl2_image_local_fallback}/include/SDL_image.h")
    set(_pixelengine_sdl2_image_source_dir "${_pixelengine_sdl2_image_local_fallback}")
endif()

if(NOT _pixelengine_sdl2_image_source_dir)
    message(FATAL_ERROR
        "SDL2_image could not be downloaded or found locally. Download SDL_image release-${_pixelengine_sdl2_image_version}, extract it, then set PIXELENGINE_SDL2_IMAGE_SOURCE_DIR to its source directory. The default fallback is ${_pixelengine_sdl2_image_local_fallback}."
    )
endif()

set(SDL2IMAGE_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_TESTS OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_VENDORED OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_DEPS_SHARED OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_BACKEND_STB ON CACHE BOOL "" FORCE)
set(SDL2IMAGE_BMP OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_GIF OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_JPG ON CACHE BOOL "" FORCE)
set(SDL2IMAGE_PNG ON CACHE BOOL "" FORCE)
set(SDL2IMAGE_SVG OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_TIF OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_WEBP OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(
    "${_pixelengine_sdl2_image_source_dir}"
    "${CMAKE_BINARY_DIR}/_deps/sdl2_image-build"
)

# ---- Sources ----
set(SOURCES 
    main.cpp
    Engine.cpp
    Engine.hpp
    MazeGenerator.hpp
)

add_executable(EngineApp ${SOURCES})

target_include_directories(EngineApp PRIVATE
    ${SDL2_SOURCE_DIR}/include
)

target_link_libraries(EngineApp PRIVATE
    SDL2::SDL2main
    SDL2::SDL2
    SDL2_ttf
    SDL2_mixer::SDL2_mixer-static
    SDL2_image::SDL2_image-static
)

add_custom_command(TARGET EngineApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "D:/mingw/x86_64-15.2.0-release-win32-seh-ucrt-rt_v13-rev0/mingw64/bin/libgcc_s_seh-1.dll"
        "$<TARGET_FILE_DIR:EngineApp>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "D:/mingw/x86_64-15.2.0-release-win32-seh-ucrt-rt_v13-rev0/mingw64/bin/libstdc++-6.dll"
        "$<TARGET_FILE_DIR:EngineApp>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "D:/mingw/x86_64-15.2.0-release-win32-seh-ucrt-rt_v13-rev0/mingw64/bin/libwinpthread-1.dll"
        "$<TARGET_FILE_DIR:EngineApp>"
)