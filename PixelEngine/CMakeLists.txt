# PixelEngine — 可移植构建配置
# 依赖（SDL2 / SDL2_mixer / SDL2_image）通过 FetchContent 自动下载并校验 SHA-256，
# 无需任何本机绝对路径。前置要求仅：CMake 3.16+ 与 MinGW-w64（x86_64 UCRT）。
#
# 离线或使用本地源码时，可用 CMake 变量指向已解压的源码目录：
#   -DFETCHCONTENT_SOURCE_DIR_SDL2=...
#   -DFETCHCONTENT_SOURCE_DIR_SDL2_mixer=...
#   -DFETCHCONTENT_SOURCE_DIR_SDL2_image=...
# 详见 README「构建与运行」。

cmake_minimum_required(VERSION 3.16)
project(PixelGameEngine LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 全部依赖静态构建：SDL2_mixer / SDL2_image 依据 BUILD_SHARED_LIBS 决定导出目标名
# （SDL2_mixer::SDL2_mixer-static / SDL2_image::SDL2_image-static），故必须为 OFF。
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)

include(FetchContent)

# ---- SDL2：静态构建，保留 SDL2main 以便在 Windows 上接管 WinMain ----
set(SDL_SHARED OFF CACHE BOOL "Build SDL2 as shared library" FORCE)
set(SDL_STATIC ON CACHE BOOL "Build SDL2 as static library" FORCE)
set(SDL_TEST OFF CACHE BOOL "Build the SDL2_test library" FORCE)
set(SDL2_DISABLE_SDL2MAIN OFF CACHE BOOL "Keep SDL2main" FORCE)
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "Disable SDL2 install target" FORCE)

FetchContent_Declare(
    SDL2
    URL https://www.libsdl.org/release/SDL2-2.30.3.tar.gz
    URL_HASH SHA256=820440072f8f5b50188c1dae104f2ad25984de268785be40c41a099a510f0aec
)

# ---- SDL2_mixer：静态构建，MP3 用 dr_mp3，OGG Vorbis 用 stb 后端 ----
set(SDL2MIXER_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_CMD OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_FLAC OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MOD OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MP3 ON CACHE BOOL "" FORCE)
set(SDL2MIXER_MP3_DRMP3 ON CACHE BOOL "" FORCE)
set(SDL2MIXER_MIDI OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_OPUS OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_VORBIS "STB" CACHE STRING "" FORCE)

FetchContent_Declare(
    SDL2_mixer
    URL https://codeload.github.com/libsdl-org/SDL_mixer/tar.gz/refs/tags/release-2.6.3
    URL_HASH SHA256=91dd065e9e63f499e5317350b110184b0ba96bc5f63c39b3a9939a136c40c035
)

# ---- SDL2_image：静态构建，PNG/JPG 使用内置 stb 后端 ----
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

FetchContent_Declare(
    SDL2_image
    URL https://codeload.github.com/libsdl-org/SDL_image/tar.gz/refs/tags/release-2.8.2
    URL_HASH SHA256=8fd59b2c17772d7ac1192b11c645be8d7874f595c1714f4b200ee70b7cc38f3e
)

FetchContent_MakeAvailable(SDL2)
FetchContent_MakeAvailable(SDL2_mixer SDL2_image)

# ---- 源码 ----
set(SOURCES
    main.cpp
    Engine.cpp
    Engine.hpp
    MazeGenerator.hpp
    MazeGenerator.cpp
    Core/Types.hpp
    Core/PathUtils.hpp
    Core/Roles.hpp
    Audio/AudioManager.cpp
    Input/InputManager.cpp
    World/WorldMap.cpp
    World/PhysicsSystem.cpp
    World/Monster.cpp
    World/Weapon.cpp
    World/Player.cpp
    World/CombatSystem.cpp
    World/EnemyManager.cpp
    Graphics/ParticleSystem.cpp
    Graphics/RenderSystem.cpp
    Graphics/UIRenderer.cpp
    Graphics/ImGuiUI.cpp
    Graphics/GameUI.cpp
    Graphics/TextConfig.cpp
    Graphics/TextTweakerUI.cpp
    Storage/SaveManager.cpp
    CS2D/CS2DTypes.hpp
    CS2D/MiniJSON.hpp
    CS2D/TMJParser.cpp
    CS2D/CS2DWorld.cpp
    CS2D/CS2DRenderer.cpp
    CS2D/CS2DGame.cpp
    CS2D/CS2DOverlay.cpp
    ThirdParty/imgui/imgui.cpp
    ThirdParty/imgui/imgui_draw.cpp
    ThirdParty/imgui/imgui_tables.cpp
    ThirdParty/imgui/imgui_widgets.cpp
    ThirdParty/imgui/backends/imgui_impl_sdl2.cpp
    ThirdParty/imgui/backends/imgui_impl_sdlrenderer2.cpp
    LLM/LLMBotController.cpp
)

add_executable(EngineApp ${SOURCES})

target_include_directories(EngineApp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/imgui/backends
)

target_link_libraries(EngineApp PRIVATE
    SDL2::SDL2main
    SDL2::SDL2
    SDL2_mixer::SDL2_mixer-static
    SDL2_image::SDL2_image-static
    ws2_32
    winhttp
)

# 静态链接 C/C++ 运行库：消除对 libgcc_s_seh-1.dll / libstdc++-6.dll /
# libwinpthread-1.dll 的运行时依赖，产出可独立运行的单文件 exe。
if(MINGW)
    target_link_options(EngineApp PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# 将仓库根目录的 assets 复制到可执行文件旁，使运行时以 exe 所在目录为基准找到资源。
add_custom_command(TARGET EngineApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/../assets"
        "$<TARGET_FILE_DIR:EngineApp>/assets"
    COMMENT "Copying assets next to EngineApp executable"
)
