# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build_cs2d/_deps/sdl2_image-src")
  file(MAKE_DIRECTORY "D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build_cs2d/_deps/sdl2_image-src")
endif()
file(MAKE_DIRECTORY
  "D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build_cs2d/_deps/sdl2_image-build"
  "D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build_cs2d/_deps/sdl2_image-subbuild/sdl2_image-populate-prefix"
  "D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build_cs2d/_deps/sdl2_image-subbuild/sdl2_image-populate-prefix/tmp"
  "D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build_cs2d/_deps/sdl2_image-subbuild/sdl2_image-populate-prefix/src/sdl2_image-populate-stamp"
  "D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build_cs2d/_deps/sdl2_image-subbuild/sdl2_image-populate-prefix/src"
  "D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build_cs2d/_deps/sdl2_image-subbuild/sdl2_image-populate-prefix/src/sdl2_image-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build_cs2d/_deps/sdl2_image-subbuild/sdl2_image-populate-prefix/src/sdl2_image-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/BJTU/Cpp/Outstanding Projects/PixelEngine/build_cs2d/_deps/sdl2_image-subbuild/sdl2_image-populate-prefix/src/sdl2_image-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
