# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build_cs2d/_deps/sdl2_mixer-src")
  file(MAKE_DIRECTORY "D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build_cs2d/_deps/sdl2_mixer-src")
endif()
file(MAKE_DIRECTORY
  "D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build_cs2d/_deps/sdl2_mixer-build"
  "D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build_cs2d/_deps/sdl2_mixer-subbuild/sdl2_mixer-populate-prefix"
  "D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build_cs2d/_deps/sdl2_mixer-subbuild/sdl2_mixer-populate-prefix/tmp"
  "D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build_cs2d/_deps/sdl2_mixer-subbuild/sdl2_mixer-populate-prefix/src/sdl2_mixer-populate-stamp"
  "D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build_cs2d/_deps/sdl2_mixer-subbuild/sdl2_mixer-populate-prefix/src"
  "D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build_cs2d/_deps/sdl2_mixer-subbuild/sdl2_mixer-populate-prefix/src/sdl2_mixer-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build_cs2d/_deps/sdl2_mixer-subbuild/sdl2_mixer-populate-prefix/src/sdl2_mixer-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build_cs2d/_deps/sdl2_mixer-subbuild/sdl2_mixer-populate-prefix/src/sdl2_mixer-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
