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

project(sdl2_mixer-populate NONE)



include(ExternalProject)
ExternalProject_Add(sdl2_mixer-populate
                     "UPDATE_DISCONNECTED" "False" "DOWNLOAD_EXTRACT_TIMESTAMP" "TRUE" "EXTERNALPROJECT_INTERNAL_ARGUMENT_SEPARATOR" "URL" "https://codeload.github.com/libsdl-org/SDL_mixer/tar.gz/refs/tags/release-2.6.3" "URL_HASH" "SHA256=91dd065e9e63f499e5317350b110184b0ba96bc5f63c39b3a9939a136c40c035"
                    SOURCE_DIR          "D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build_cs2d/_deps/sdl2_mixer-src"
                    BINARY_DIR          "D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build_cs2d/_deps/sdl2_mixer-build"
                    CONFIGURE_COMMAND   ""
                    BUILD_COMMAND       ""
                    INSTALL_COMMAND     ""
                    TEST_COMMAND        ""
                    USES_TERMINAL_DOWNLOAD  YES
                    USES_TERMINAL_UPDATE    YES
                    USES_TERMINAL_PATCH     YES
)


