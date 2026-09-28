set(CMAKE_MAKE_PROGRAM "D:/mingw/x86_64-15.2.0-release-win32-seh-ucrt-rt_v13-rev0/mingw64/bin/mingw32-make.exe" CACHE FILEPATH "" FORCE)

# Disable RC language before any add_subdirectory calls
if(MINGW OR MSYS)
  set(CMAKE_RC_COMPILER "")
  set(CMAKE_RC_COMPILER_WORKS TRUE)
endif()

cmake_minimum_required(VERSION 3.14)
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