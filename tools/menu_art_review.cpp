// Capture every menu customization through the production OpenGL renderer.
#include <SDL2/SDL.h>
#include <SDL_image.h>
#include <algorithm>
#include <filesystem>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "Core/PathUtils.hpp"
#include "Graphics/Menu3D.hpp"

struct Menu3DModelTestAccess {
    static float AnimationTime(const Menu3DModel& model) { return model.animationTime_; }
    static void VerifyWind(Menu3DModel& model) {
        if (model.animations_.size() != 1 || model.animations_[0].channels.size() != 18)
            throw std::runtime_error("Environment must animate all foliage in one clip");
        model.Update(0);
        const auto before = model.nodes_;
        model.Update(1.0f);
        for (const auto& channel : model.animations_[0].channels) {
            const auto& start = before[channel.node].world;
            const auto& end = model.nodes_[channel.node].world;
            float difference = 0;
            for (int i = 0; i < 16; ++i) difference += std::fabs(start.m[i]-end.m[i]);
            if (difference < .00001f) throw std::runtime_error("Foliage node is not animated at runtime");
        }
    }
    static void VerifyArena(const Menu3DModel& model, float scale, float x, float z) {
        for (const auto& node : model.nodes_) {
            if (!node.visible || node.mesh < 0) continue;
            for (const auto& primitive : model.meshes_[node.mesh].primitives) {
                for (const auto& vertex : primitive.skinnedPos) {
                    const float worldX = x + vertex.x * scale;
                    const float worldY = (vertex.y - model.minY_) * scale;
                    const float worldZ = z + vertex.z * scale;
                    if (std::fabs(worldX) > 6.3f || worldY > 6.5f || worldZ < -2.7f || worldZ > 6.1f)
                        throw std::runtime_error("Visible model leaves the scenery-protected arena: " + model.animationName_);
                }
            }
        }
    }
};

struct Menu3DTestAccess {
    static void VerifySceneArena(Menu3D& menu) {
        const auto saved = menu.GetCustomization();
        for (int object = 0; object < 2; ++object) {
            auto& model = object == 0 ? *menu.tModel_ : *menu.ctModel_;
            for (int weapon = 0; weapon < 6; ++weapon) {
                for (int pose = 0; pose < 4; ++pose) {
                    for (int accessory = 0; accessory < 4; ++accessory) {
                        menu.FocusSelect(object,0,weapon);
                        menu.FocusSelect(object,2,pose);
                        menu.FocusSelect(object,3,accessory);
                        for (int frame = 0; frame < 120; ++frame) {
                            model.Update(1.0f/30.0f);
                            Menu3DModelTestAccess::VerifyArena(model,1.22f,object == 0 ? -4.4f : 4.4f,0);
                            Menu3DModelTestAccess::VerifyArena(model,2.3f,-2.4f,0);
                        }
                    }
                }
            }
        }
        for (int pose = 0; pose < 4; ++pose) {
            for (int accessory = 0; accessory < 3; ++accessory) {
                menu.FocusSelect(2,1,pose);
                menu.FocusSelect(2,2,accessory);
                auto& model = *menu.chickens_[0];
                for (int frame = 0; frame < 120; ++frame) {
                    model.Update(1.0f/30.0f);
                    Menu3DModelTestAccess::VerifyArena(model,2.3f,-2.4f,0);
                    for (float x : {-5.5f,5.5f}) {
                        for (float z : {-2.0f,5.5f})
                            Menu3DModelTestAccess::VerifyArena(model,.34f,x,z);
                    }
                }
            }
        }
        menu.SetCustomization(saved);
        std::cout << "MENU COMPLETE POSE/WEAPON/ACCESSORY ARENA PASS" << std::endl;
    }

    static void VerifySceneState(Menu3D& menu) {
        const auto customization = menu.GetCustomization();
        const auto chickens = menu.chickenState_;
        menu.SetScene(-5);
        if (menu.SceneIndex() != 0) throw std::runtime_error("Scene lower bound");
        menu.SetScene(50);
        if (menu.SceneIndex() != 2) throw std::runtime_error("Scene upper bound");
        menu.sceneTransition_ = .35f;
        menu.SetScene(0);
        if (std::fabs(menu.sceneTransition_ - .15f) > .00001f)
            throw std::runtime_error("Rapid scene switch changed fade opacity");
        menu.SetScene(1, true);
        if (menu.displayedScene_ != 1 || menu.sceneTransition_ != .5f)
            throw std::runtime_error("Saved scene is not applied on first frame");
        for (int i = 0; i < 5; ++i) {
            if (menu.custom_.pose[i] != customization.pose[i] || menu.custom_.color[i] != customization.color[i] ||
                menu.custom_.accessory[i] != customization.accessory[i]) throw std::runtime_error("Scene reset customization");
        }
        for (int i = 0; i < 2; ++i) {
            if (menu.custom_.weapon[i] != customization.weapon[i]) throw std::runtime_error("Scene reset weapon");
        }
        for (int i = 0; i < 3; ++i) {
            if (menu.chickenState_[i].x != chickens[i].x || menu.chickenState_[i].z != chickens[i].z)
                throw std::runtime_error("Scene reset roaming position");
        }
        std::cout << "MENU SCENE STATE PRESERVATION PASS" << std::endl;
    }

    static void ReviewScenePerformance(Menu3D& menu, const std::filesystem::path& output) {
        for (const auto& environment : menu.environments_) {
            if (!environment || !environment->IsReady()) throw std::runtime_error("Missing menu environment");
            Menu3DModelTestAccess::VerifyWind(*environment);
        }
        std::cout << "MENU ALL FOLIAGE RUNTIME WIND PASS" << std::endl;
        std::ofstream results(output / "scene-performance.csv");
        results << "scene,mean_ms,p95_ms\n";
        auto environments = std::move(menu.environments_);
        for (int scene = -1; scene < 3; ++scene) {
            if (scene == 0) menu.environments_ = std::move(environments);
            menu.SetScene(std::max(0, scene));
            menu.displayedScene_ = menu.SceneIndex();
            menu.sceneTransition_ = .5f;
            std::vector<double> samples;
            for (int frame = 0; frame < 80; ++frame) {
                menu.lastRenderTicks_ = 0;
                const auto before = SDL_GetPerformanceCounter();
                menu.Render(1.0f / 30.0f);
                const double milliseconds = 1000.0 * (SDL_GetPerformanceCounter() - before) / SDL_GetPerformanceFrequency();
                if (frame >= 20) samples.push_back(milliseconds);
            }
            std::sort(samples.begin(), samples.end());
            double total = 0;
            for (double sample : samples) total += sample;
            results << scene << ',' << total / samples.size() << ',' << samples[56] << '\n';
        }
        menu.SetScene(1);
    }

    static void VerifyFrameClock(Menu3D& menu) {
        if (!menu.tModel_ || !menu.ctModel_ || menu.chickens_.size() != 3) {
            throw std::runtime_error("Art review requires all five loaded models");
        }
        constexpr float elapsed = 1.0f / 30.0f;
        for (int frames : {1, 2, 4}) {
            const float before = Menu3DModelTestAccess::AnimationTime(*menu.tModel_);
            for (int frame = 0; frame < frames; ++frame) {
                menu.lastRenderTicks_ = SDL_GetTicks64();
                menu.Render(elapsed / static_cast<float>(frames));
            }
            menu.lastRenderTicks_ = 0;
            menu.Render(0);
            const float advanced = Menu3DModelTestAccess::AnimationTime(*menu.tModel_) - before;
            if (std::fabs(advanced - elapsed) > .00001f) {
                throw std::runtime_error("Texture throttling dropped animation time");
            }
        }
        std::cout << "MENU FRAME CLOCK 30/60/120 Hz PASS" << std::endl;
    }
};

static void Capture(Menu3D& menu, SDL_Renderer* renderer, const std::filesystem::path& path) {
    SDL_Delay(34);
    menu.Render(1.0f / 30.0f);
    SDL_SetRenderDrawColor(renderer, 18, 22, 28, 255);
    int width = 0, height = 0;
    if (SDL_GetRendererOutputSize(renderer,&width,&height) != 0) throw std::runtime_error(SDL_GetError());
    const float aspect = static_cast<float>(width)/height;
    const float textureAspect = static_cast<float>(menu.Width())/menu.Height();
    SDL_Rect source{0,0,menu.Width(),menu.Height()};
    if (aspect < textureAspect) {
        source.w = static_cast<int>(menu.Height()*aspect);
        source.x = (menu.Width()-source.w)/2;
    } else {
        source.h = static_cast<int>(menu.Width()/aspect);
        source.y = (menu.Height()-source.h)/2;
    }
    if (SDL_RenderClear(renderer) != 0 || SDL_RenderCopy(renderer, menu.GetTexture(), &source, nullptr) != 0) {
        throw std::runtime_error(SDL_GetError());
    }
    std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> surface(
        SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32), SDL_FreeSurface);
    if (!surface || SDL_RenderReadPixels(renderer, nullptr, surface->format->format, surface->pixels, surface->pitch) != 0) {
        throw std::runtime_error(SDL_GetError());
    }
    if (IMG_SavePNG(surface.get(), path.string().c_str()) != 0) {
        throw std::runtime_error(IMG_GetError());
    }
}

static void Review(const std::filesystem::path& models, const std::filesystem::path& output, const std::string& mode) {
    const auto stage = output / "runtime";
    const auto stagedModels = stage / "assets/models/menu";
    std::filesystem::create_directories(stagedModels);
    for (const char* actor : { "t", "ct", "chicken" }) {
        const std::string filename = std::string(actor) + ".glb";
        std::filesystem::copy_file(models / filename, stagedModels / filename,
                                   std::filesystem::copy_options::overwrite_existing);
    }
    PathUtils::SetBaseDir(stage.string());
    if (std::filesystem::exists(models / "scenes")) {
        std::filesystem::create_directories(stagedModels / "scenes");
        std::filesystem::copy(models / "scenes", stagedModels / "scenes",
            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
    }
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("Menu art review", 0, 0, 1280, 720, SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
    if (!window) throw std::runtime_error(SDL_GetError());
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
        SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_SOFTWARE), SDL_DestroyRenderer);
    if (!renderer) throw std::runtime_error(SDL_GetError());
    Menu3D menu;
    if (!menu.Initialize(renderer.get(), 1280, 720)) throw std::runtime_error(menu.LastError());
    Menu3DTestAccess::VerifyFrameClock(menu);
    if (mode == "--scenes") {
        Menu3DTestAccess::VerifySceneState(menu);
        Menu3DTestAccess::VerifySceneArena(menu);
        Menu3DTestAccess::ReviewScenePerformance(menu, output);
        for (int scene = 0; scene < 3; ++scene) {
            menu.SetScene(scene);
            for (int frame = 0; frame < 18; ++frame) {
                SDL_Delay(34);
                menu.Render(1.0f / 30.0f);
            }
            const std::string prefix = "scene" + std::to_string(scene);
            menu.SetFocusedObject(-1);
            menu.SetCameraTopDownProgress(0);
            Capture(menu, renderer.get(), output / (prefix + "-main.png"));
            for (int object = 0; object < 5; ++object) {
                menu.SetFocusedObject(object);
                menu.SetFocusProgress(1);
                Capture(menu, renderer.get(), output / (prefix + "-focus" + std::to_string(object) + ".png"));
            }
            menu.SetFocusedObject(-1);
            for (int camera = 1; camera <= 4; ++camera) {
                menu.SetCameraTopDownProgress(camera / 4.0f);
                Capture(menu, renderer.get(), output / (prefix + "-camera" + std::to_string(camera) + ".png"));
            }
            menu.SetCameraTopDownProgress(0);
            for (int width : {900,960,1152,1280,1680,1720}) {
                SDL_SetWindowSize(window.get(),width,720);
                menu.SetPresentationAspect(width/720.0f);
                Capture(menu,renderer.get(),output/(prefix+"-viewport"+std::to_string(width)+".png"));
            }
            SDL_SetWindowSize(window.get(),1280,720);
            menu.SetPresentationAspect(1280.0f/720.0f);
        }
        std::cout << "MENU SCENE CAPTURES 48" << std::endl;
        return;
    }
    if (mode == "--motion") {
        menu.SetFocusedObject(-1);
        for (int frame = 0; frame < 360; ++frame) {
            Capture(menu, renderer.get(), output / ("motion_" + std::to_string(frame) + ".png"));
        }
        std::cout << "MENU MOTION CAPTURES 360" << std::endl;
        return;
    }
    std::ofstream manifest(output / "captures.csv");
    if (!manifest) throw std::runtime_error("Cannot create capture manifest");
    manifest << "file,object,weapon,pose,color,accessory\n";
    Capture(menu, renderer.get(), output / "main-menu.png");
    int captures = 0;
    for (int object = 0; object < Menu3D::kObjectCount; ++object) {
        const bool human = object < 2;
        const int weaponCount = human ? menu.FocusOptionCount(object, 0) : 1;
        const int poseCount = menu.FocusOptionCount(object, human ? 2 : 1);
        const int colorCount = menu.FocusOptionCount(object, human ? 1 : 0);
        const int accessoryCount = menu.FocusOptionCount(object, human ? 3 : 2);
        menu.SetFocusedObject(object);
        menu.SetFocusProgress(1.0f);
        for (int weapon = 0; weapon < weaponCount; ++weapon) {
            for (int pose = 0; pose < poseCount; ++pose) {
                for (int color = 0; color < colorCount; ++color) {
                    for (int accessory = 0; accessory < accessoryCount; ++accessory) {
                        if (human) menu.FocusSelect(object, 0, weapon);
                        menu.FocusSelect(object, human ? 2 : 1, pose);
                        menu.FocusSelect(object, human ? 1 : 0, color);
                        menu.FocusSelect(object, human ? 3 : 2, accessory);
                        const std::string name = "object" + std::to_string(object) + "_weapon" + std::to_string(weapon) +
                            "_pose" + std::to_string(pose) + "_color" + std::to_string(color) +
                            "_accessory" + std::to_string(accessory) + ".png";
                        Capture(menu, renderer.get(), output / name);
                        manifest << name << ',' << object << ',' << weapon << ',' << pose << ',' << color << ',' << accessory << '\n';
                        ++captures;
                    }
                }
            }
        }
        manifest.flush();
        std::cout << "REVIEW OBJECT " << object << " complete, captures " << captures << std::endl;
    }
    std::cout << "MENU ART CAPTURES " << captures << std::endl;
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4 || (argc == 4 && std::string(argv[3]) != "--motion" && std::string(argv[3]) != "--scenes")) {
        std::cerr << "Usage: menu_art_review MODEL_DIRECTORY OUTPUT_DIRECTORY [--motion|--scenes]\n";
        return 2;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << SDL_GetError() << '\n';
        return 1;
    }
    int result = 0;
    try {
        Review(std::filesystem::absolute(argv[1]), std::filesystem::absolute(argv[2]), argc == 4 ? argv[3] : "");
    } catch (const std::exception& error) {
        std::cerr << "MENU ART REVIEW FAILED " << error.what() << '\n';
        result = 1;
    }
    IMG_Quit();
    SDL_Quit();
    return result;
}
