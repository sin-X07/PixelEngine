# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 4.3.2)

# Reject any attempt to use a toolchain file. We must not use one because
# we could be downloading it here. If the CMAKE_TOOLCHAIN_FILE environment
# variable is set, the cache variable will have been initialized from it.
unset(CMAKE_TOOLCHAIN_FILE CACHE)
unset(ENV{CMAKE_TOOLCHAIN_FILE})

# We name the project and the target for the ExternalProject_Add() call
# to something that will highlight to the user what we are working on if
# something goes wrong and an error message is produced.

project(sdl2_image-populate NONE)



include(ExternalProject)
ExternalProject_Add(sdl2_image-populate
                     "UPDATE_DISCONNECTED" "False" "DOWNLOAD_EXTRACT_TIMESTAMP" "TRUE" "EXTERNALPROJECT_INTERNAL_ARGUMENT_SEPARATOR" "URL" "https://codeload.github.com/libsdl-org/SDL_image/tar.gz/refs/tags/release-2.8.2" "URL_HASH" "SHA256=8fd59b2c17772d7ac1192b11c645be8d7874f595c1714f4b200ee70b7cc38f3e"
                    SOURCE_DIR          "D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build_cs2d/_deps/sdl2_image-src"
                    BINARY_DIR          "D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build_cs2d/_deps/sdl2_image-build"
                    CONFIGURE_COMMAND   ""
                    BUILD_COMMAND       ""
                    INSTALL_COMMAND     ""
                    TEST_COMMAND        ""
                    USES_TERMINAL_DOWNLOAD  YES
                    USES_TERMINAL_UPDATE    YES
                    USES_TERMINAL_PATCH     YES
)


