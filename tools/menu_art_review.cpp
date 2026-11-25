// Capture every menu customization through the production OpenGL renderer.
#include <SDL2/SDL.h>
#include <SDL_image.h>
#include <SDL_opengl.h>
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
        if (model.animations_.size() != 1 || model.animations_[0].name != "Menu_Environment")
            throw std::runtime_error("Environment must animate foliage and dust in one clip");
        const auto& clip = model.animations_[0];
        int foliage = 0;
        for (const auto& channel : clip.channels) {
            if (model.nodes_[channel.node].name.rfind("Sunlit dust | ", 0) != 0) ++foliage;
        }
        if (foliage != 18)
            throw std::runtime_error("Environment must retain all 18 foliage channels");
        model.Update(0);
        const auto before = model.nodes_;
        model.Update(1.0f);
        for (const auto& channel : model.animations_[0].channels) {
            const auto& start = before[channel.node].world;
            const auto& end = model.nodes_[channel.node].world;
            float difference = 0;
            for (int i = 0; i < 16; ++i) difference += std::fabs(start.m[i]-end.m[i]);
            if (difference < .00001f) throw std::runtime_error("Environment node did not move at runtime: " + before[channel.node].name);
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
        // T/CT 角色已移除（2026-11-25）：只验证三只小鸡的姿势/配饰组合。
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
        std::cout << "MENU CHICKEN POSE/ACCESSORY ARENA PASS" << std::endl;
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

    static void SpotlightBenchmark(Menu3D& menu, const std::filesystem::path& output) {
        std::ofstream csv(output / "scene-performance.csv");
        csv << "scene,mean_ms,p95_ms\n";
        for (int scene = 0; scene < 3; ++scene) {
            menu.SetScene(scene, true);
            menu.chickenRng_.seed(1701);
            menu.InitChickenState();
            menu.spotScanTimer_ = 0;
            menu.animationTime_ = 0;
            std::vector<double> times;
            for (int frame = 0; frame < 150; ++frame) {
                menu.lastRenderTicks_ = 0;
                const auto start = SDL_GetPerformanceCounter();
                menu.Render(1.f / 30);
                if (frame >= 30) times.push_back(1000.0 * (SDL_GetPerformanceCounter() - start) / SDL_GetPerformanceFrequency());
            }
            std::sort(times.begin(), times.end());
            double total = 0;
            for (double sample : times) total += sample;
            csv << scene << ',' << total / times.size() << ',' << times[113] << '\n';
        }
    }

    // Compare adjacent renders of the exact same pose to separate light cost from
    // changes in system load between baseline and candidate process launches.
    static void MeasureLightCost(Menu3D& menu, const std::filesystem::path& output) {
        std::ofstream csv(output / "effect-cost.csv");
        csv << "scene,off_p95_ms,on_p95_ms,delta_ms\n";
        for (int scene = 0; scene < 3; ++scene) {
            menu.SetScene(scene, true);
            std::array<std::vector<double>,2> samples;
            for (int pair = 0; pair < 100; ++pair) {
                // Advance once, then freeze both measured frames at the same time.
                menu.lastRenderTicks_ = 0;
                menu.Render(1.f/30.f);
                for (int turn = 0; turn < 2; ++turn) {
                    const int on = (pair+turn)%2;
                    menu.SetSpotlightSuppressed(!on);
                    menu.lastRenderTicks_ = 0;
                    const auto start = SDL_GetPerformanceCounter();
                    menu.Render(0);
                    const double ms = 1000.0*(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency();
                    if (pair >= 20) samples[on].push_back(ms);
                }
            }
            for (auto& times : samples) std::sort(times.begin(),times.end());
            csv << scene << ',' << samples[0][75] << ',' << samples[1][75] << ','
                << samples[1][75]-samples[0][75] << '\n';
        }
        menu.SetSpotlightSuppressed(false);
    }

    static void FreezeSpotlight(Menu3D& menu, bool enabled) {
        menu.SetSpotlightSuppressed(!enabled);
        menu.pendingAnimationTime_ = 0;
        menu.lastRenderTicks_ = 0;
        while (glGetError() != GL_NO_ERROR) {}
        menu.Render(0);
        const auto error = glGetError();
        if (error != GL_NO_ERROR) throw std::runtime_error("Spotlight frame GL error " + std::to_string(error));
        if (menu.surfaceProgram_ && (!menu.spotDepth_ || !menu.volumeProgram_ || !menu.volumeWidth_))
            throw std::runtime_error("Missing spotlight render targets");
    }

    static void AimAtChicken(Menu3D& menu) {
        menu.SetHoveredObject(3);
        menu.UpdateSpotlight(2);
    }

    static void VerifySpotlight(Menu3D& menu) {
        const bool forcedFallback = SDL_getenv("PIXELENGINE_MENU_INVALID_SHADER") &&
                                    std::string(SDL_getenv("PIXELENGINE_MENU_INVALID_SHADER")) == "1";
        if (forcedFallback == (menu.surfaceProgram_ != 0))
            throw std::runtime_error("Unexpected spotlight rendering path");
        if (!forcedFallback) {
            if (!menu.chickenProgram_) throw std::runtime_error("Chicken spotlight shader missing");
            const auto bind = reinterpret_cast<PFNGLBINDFRAMEBUFFERPROC>(SDL_GL_GetProcAddress("glBindFramebuffer"));
            // Reset both depth inputs independently. Measure scattering from this same camera.
            auto scatteredLight = [&](double cameraDepth, double lampDepth) {
                glPushAttrib(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_TEXTURE_BIT);
                glDepthMask(GL_TRUE);
                bind(GL_FRAMEBUFFER, menu.spotFramebuffer_);
                glClearDepth(lampDepth);
                glClear(GL_DEPTH_BUFFER_BIT);
                bind(GL_FRAMEBUFFER, 0);
                glClearDepth(cameraDepth);
                glClear(GL_DEPTH_BUFFER_BIT);
                bind(GL_FRAMEBUFFER, menu.volumeFramebuffer_);
                glClearColor(0,0,0,0);
                glClear(GL_COLOR_BUFFER_BIT);
                bind(GL_FRAMEBUFFER, 0);
                menu.DrawSpotlightVolume();
                std::vector<unsigned char> samples(size_t(menu.volumeWidth_) * menu.volumeHeight_ * 4);
                glBindTexture(GL_TEXTURE_2D,menu.volumeTexture_);
                glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,samples.data());
                glPopAttrib();
                uint64_t total = 0;
                for (auto sample : samples) total += sample;
                return total;
            };
            if (!scatteredLight(1,1) || scatteredLight(0,1) || scatteredLight(1,0))
                throw std::runtime_error("Camera or spotlight depth failed to occlude the volume");
        }
        // Aim a review camera at the lamp aperture; ordinary menu framing often leaves it offscreen.
        if (!forcedFallback) {
            const auto& light = menu.light_;
            const auto savedEye = menu.cameraPos_;
            glPushAttrib(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glClearColor(0,0,0,0);
            glMatrixMode(GL_PROJECTION);
            glPushMatrix();
            glLoadIdentity();
            glOrtho(-0.2,0.2,-0.2,0.2,0.1,5);
            glMatrixMode(GL_MODELVIEW);
            glPushMatrix();
            auto lensBrightness = [&](float side, double depth) {
                for (int k = 0; k < 3; ++k) menu.cameraPos_[k] = light.position[k] + side*light.direction[k];
                const auto& r = light.right;
                const auto& u = light.up;
                const auto& d = light.direction;
                const float basis[] = {r[0],u[0],side*d[0],0, r[1],u[1],side*d[1],0,
                                       r[2],u[2],side*d[2],0, 0,0,0,1};
                glLoadMatrixf(basis);
                glTranslatef(-menu.cameraPos_[0],-menu.cameraPos_[1],-menu.cameraPos_[2]);
                glClearDepth(depth);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                menu.DrawSpotlightLens();
                unsigned char rgba[4]{};
                glReadPixels(menu.Width()/2,menu.Height()/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
                return rgba[0];
            };
            const bool lensVisible = lensBrightness(1,1) > 100;
            const bool backDark = lensBrightness(-1,1) == 0;
            const bool occluded = lensBrightness(1,0) == 0;
            glPopMatrix();
            glMatrixMode(GL_PROJECTION); glPopMatrix();
            glMatrixMode(GL_MODELVIEW);
            glPopAttrib();
            menu.cameraPos_ = savedEye;
            if (!lensVisible || !backDark || !occluded) throw std::runtime_error("Spotlight lens visibility");
        }
        menu.SetCameraTopDownProgress(1);
        menu.SetSpotlightSourceProgress(1);
        for (const float u : {0.2f,0.8f}) {
            const float v = 1.f-u;
            menu.SetOverlayMouse(u,v,true);
            menu.UpdateSpotlight(2);
            FreezeSpotlight(menu,true);
            float view[16], projection[16];
            glGetFloatv(GL_MODELVIEW_MATRIX,view);
            glGetFloatv(GL_PROJECTION_MATRIX,projection);
            float eye[3];
            for (int i = 0; i < 3; ++i)
                eye[i] = view[i]*menu.spotX_ + view[8+i]*menu.spotZ_ + view[12+i];
            const float screenU = 0.5f - 0.5f*projection[0]*eye[0]/eye[2];
            const float screenV = 0.5f + 0.5f*projection[5]*eye[1]/eye[2];
            if (std::abs(screenU-u)>0.005f || std::abs(screenV-v)>0.005f)
                throw std::runtime_error("Overhead spot misses cursor");
        }
        menu.CaptureScenePreviews();
        if (menu.light_.power != 0.f) throw std::runtime_error("Light leaked into a scene preview");
        std::cout << "MENU SPOTLIGHT " << (forcedFallback ? "FALLBACK" : "LIGHTING AND OCCLUSION")
                  << ", CURSOR AND PREVIEWS PASS" << std::endl;
    }

    static void VerifyFrameClock(Menu3D& menu) {
        if (menu.chickens_.size() != 3) {
            throw std::runtime_error("Art review requires all three chickens");
        }
        // Walking uses stride-matched variable playback; isolate the frame clock with idle.
        const auto savedPose = menu.GetCustomization().pose[2];
        menu.SetObjectPose(2, 1);
        constexpr float elapsed = 1.0f / 30.0f;
        for (int frames : {1, 2, 4}) {
            const float before = Menu3DModelTestAccess::AnimationTime(*menu.chickens_[0]);
            for (int frame = 0; frame < frames; ++frame) {
                menu.lastRenderTicks_ = SDL_GetTicks64();
                menu.Render(elapsed / static_cast<float>(frames));
            }
            menu.lastRenderTicks_ = 0;
            menu.Render(0);
            const float advanced = Menu3DModelTestAccess::AnimationTime(*menu.chickens_[0]) - before;
            if (std::fabs(advanced - elapsed) > .00001f) {
                throw std::runtime_error("Texture throttling dropped animation time");
            }
        }
        menu.SetObjectPose(2, savedPose);
        std::cout << "MENU FRAME CLOCK 30/60/120 Hz PASS" << std::endl;
    }
};

static void Capture(Menu3D& menu, SDL_Renderer* renderer, const std::filesystem::path& path, bool advance = true) {
    if (advance) {
        SDL_Delay(34);
        menu.Render(1.0f / 30.0f);
    }
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
    const int width = mode == "--spotlight" ? 1728 : 1280;
    const int height = mode == "--spotlight" ? 1080 : 720;
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("Menu art review", 0, 0, width, height, SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
    if (!window) throw std::runtime_error(SDL_GetError());
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
        SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_SOFTWARE), SDL_DestroyRenderer);
    if (!renderer) throw std::runtime_error(SDL_GetError());
    Menu3D menu;
    const auto start = SDL_GetPerformanceCounter();
    if (!menu.Initialize(renderer.get(), width, height)) throw std::runtime_error(menu.LastError());
    std::cout << "Menu initialize (ms): " << 1000.0*(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency() << std::endl;
    if (mode == "--spotlight") {
        menu.SetPresentationAspect(float(width) / height);
        Menu3DTestAccess::SpotlightBenchmark(menu, output);
        for (int scene = 0; scene < 3; ++scene) {
            menu.SetScene(scene, true);
            Menu3DTestAccess::AimAtChicken(menu);
            for (bool on : {false, true}) {
                Menu3DTestAccess::FreezeSpotlight(menu, on);
                Capture(menu, renderer.get(), output / ("scene" + std::to_string(scene) + (on ? "-on.png" : "-off.png")), false);
            }
        }
        Menu3DTestAccess::VerifySpotlight(menu);
        menu.SetCameraTopDownProgress(0);
        menu.SetSpotlightSourceProgress(0);
        menu.SetOverlayMouse(0,0,false);
        menu.SetHoveredObject(-1);
        Menu3DTestAccess::MeasureLightCost(menu, output);
        return;
    }
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
    if (argc < 3 || argc > 4 || (argc == 4 && std::string(argv[3]) != "--motion" && std::string(argv[3]) != "--scenes" && std::string(argv[3]) != "--spotlight")) {
        std::cerr << "Usage: menu_art_review MODEL_DIRECTORY OUTPUT_DIRECTORY [--motion|--scenes|--spotlight]\n";
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
