// Exercise map surrounds through the production renderer at preview/gameplay cameras.
#include "CS2D/CS2DRenderer.hpp"
#include "CS2D/TMJParser.hpp"
#include "Core/PathUtils.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

struct MapPerimeterReview {
    static void Require(bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    }

    static void CheckBundle(const CS2DRenderer& map) {
        Require(map.perimeterTex_ && map.perimeterGroundTex_, "Missing authored perimeter");
        Require(map.perimeterTileW_ > 0 && map.perimeterTileH_ > 0, "Invalid material scale");
        Require(map.perimeterWorldRect_.x < 0 && map.perimeterWorldRect_.y < 0, "Invalid padding");
    }

    static void Capture(CS2DRenderer& map, SDL_Renderer* renderer,
                        const std::filesystem::path& path, int width, int height) {
        SDL_SetRenderDrawColor(renderer, 18, 26, 36, 255);
        SDL_RenderClear(renderer);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        map.DrawWorld();
        SDL_BlendMode blend;
        SDL_GetRenderDrawBlendMode(renderer, &blend);
        Require(blend == SDL_BLENDMODE_BLEND, "World rendering changed the draw blend mode");
        std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> surface(
            SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32), SDL_FreeSurface);
        Require(surface != nullptr, SDL_GetError());
        Require(SDL_RenderReadPixels(renderer, nullptr, surface->format->format,
                                    surface->pixels, surface->pitch) == 0, SDL_GetError());
        // A sampled old clear color indicates uncovered canvas, including tile cracks.
        const auto* bytes = static_cast<const Uint8*>(surface->pixels);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const auto* pixel = bytes+y*surface->pitch+x*4;
                Require(!(pixel[0] == 18 && pixel[1] == 26 && pixel[2] == 36), "Exposed old clear color");
            }
        }
        Require(IMG_SavePNG(surface.get(), path.string().c_str()) == 0, IMG_GetError());
    }

    static void CheckBackgroundPrecedence(CS2DRenderer& map, SDL_Renderer* renderer) {
        std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> surface(
            SDL_CreateRGBSurfaceWithFormat(0, 1, 1, 32, SDL_PIXELFORMAT_RGBA32), SDL_FreeSurface);
        Require(surface != nullptr, SDL_GetError());
        SDL_FillRect(surface.get(), nullptr, SDL_MapRGBA(surface->format, 17, 231, 49, 255));
        std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> replacement(
            SDL_CreateTextureFromSurface(renderer, surface.get()), SDL_DestroyTexture);
        Require(replacement != nullptr, SDL_GetError());
        SDL_Texture* original = map.backgroundTex_;
        map.backgroundTex_ = replacement.get();
        map.SetPreviewCamera(map.world_->GetImageW() / 2, map.world_->GetImageH() / 2, 1, false);
        map.DrawImageBackground();
        map.backgroundTex_ = original;
        const SDL_Rect center{640, 360, 1, 1};
        std::array<Uint8, 4> pixel{};
        Require(SDL_RenderReadPixels(renderer, &center, SDL_PIXELFORMAT_RGBA32,
                                     pixel.data(), 4) == 0, SDL_GetError());
        Require(pixel[0] == 17 && pixel[1] == 231 && pixel[2] == 49,
                "Perimeter scene obscures the current map background");
    }

    static void CheckFallbacks(CS2DRenderer& map, SDL_Renderer* renderer,
                               const std::filesystem::path& output) {
        SDL_Texture* scene = map.perimeterTex_;
        SDL_Texture* ground = map.perimeterGroundTex_;
        map.perimeterTex_ = nullptr;
        map.perimeterGroundTex_ = nullptr;
        map.SetPreviewCamera(-100, -100, 1, false);
        Capture(map, renderer, output, 1280, 720);
        map.perimeterGroundTex_ = ground;
        map.perimeterTex_ = scene;
        map.Clean();
        Require(!map.perimeterTex_ && !map.perimeterGroundTex_ && map.perimeterTileW_ == 0,
                "Perimeter resources survived cleanup");
    }
};

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: map_perimeter_review REPO_ROOT OUTPUT_DIRECTORY\n";
        return 2;
    }
    const auto root = std::filesystem::absolute(argv[1]);
    const auto output = std::filesystem::absolute(argv[2]);
    std::filesystem::create_directories(output);
    PathUtils::SetBaseDir(root.string());
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return 1;
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
    int result = 0;
    try {
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Map perimeter review", 0, 0, 1280, 720, SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
        std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
            SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE),
            SDL_DestroyRenderer);
        MapPerimeterReview::Require(renderer != nullptr, SDL_GetError());
        CS2DRenderer map;
        map.Initialize(renderer.get(), (root/"assets/cs2d/maps").string(), 1280, 720);
        std::ofstream performance(output/"performance.csv");
        performance << "map,mean_ms,p95_ms\n";
        for (const std::string name : {"ancient_ruins", "dust_2", "mirage"}) {
            CS2DMapData data;
            MapPerimeterReview::Require(TMJParser::Parse((root/"assets/cs2d/maps"/(name+".tmj")).string(), data),
                                         "Cannot parse map");
            CS2DWorld world;
            MapPerimeterReview::Require(world.Build(data), "Cannot build world");
            const auto collisions = world.GetCollisionGrid();
            map.SetWorld(&world);
            MapPerimeterReview::CheckBundle(map);
            const float worldW = world.GetImageW();
            const float worldH = world.GetImageH();
            for (const int width : {900, 960, 1152, 1280, 1680, 1720}) {
                constexpr int height = 720;
                std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> target(
                    SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_RGBA8888,
                                      SDL_TEXTUREACCESS_TARGET, width, height), SDL_DestroyTexture);
                MapPerimeterReview::Require(target != nullptr, SDL_GetError());
                SDL_SetRenderTarget(renderer.get(), target.get());
                map.SetViewportSize(width, height);
                const float fit = std::min(width/(worldW+64), height/(worldH+64));
                map.SetPreviewCamera(worldW/2, worldH/2, fit, false);
                MapPerimeterReview::Capture(map, renderer.get(), output/(name+"-preview-"+std::to_string(width)+".png"), width, height);
                // Four corners plus edge centers, normal camera and scope multiplier.
                const std::array<SDL_FPoint, 8> points{{{0, 0}, {worldW/2, 0}, {worldW, 0},
                    {worldW, worldH/2}, {worldW, worldH}, {worldW/2, worldH}, {0, worldH}, {0, worldH/2}}};
                for (size_t i = 0; i < points.size(); ++i) {
                    map.UpdateCamera(points[i].x, points[i].y, width, height);
                    if (width == 1280)
                        MapPerimeterReview::Capture(map, renderer.get(), output/(name+"-edge-"+std::to_string(i)+".png"), width, height);
                    else map.DrawWorld();
                    map.UpdateCamera(points[i].x, points[i].y, width, height, 2.0f);
                    map.DrawWorld();
                }
                // Preview-to-spawn interpolation exercises fractional camera/zoom.
                const auto& spawn = world.GetSpawnPoints().front();
                for (int frame = 0; frame <= 30; ++frame) {
                    const float t = frame/30.0f;
                    map.SetPreviewCamera(worldW/2+(spawn.x-worldW/2)*t,
                        worldH/2+(spawn.y-worldH/2)*t, fit+(2-fit)*t, false);
                    map.DrawWorld();
                    if (width == 1280 && frame % 15 == 0)
                        MapPerimeterReview::Capture(map, renderer.get(),
                            output/(name+"-transition-"+std::to_string(frame)+".png"), width, height);
                }
                SDL_SetRenderTarget(renderer.get(), nullptr);
            }
            map.SetViewportSize(1280, 720);
            map.UpdateCamera(0, 0, 1280, 720);
            std::vector<double> samples;
            for (int i = 0; i < 100; ++i) {
                const auto before = std::chrono::steady_clock::now();
                map.DrawWorld();
                SDL_RenderFlush(renderer.get());
                const double elapsed = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now()-before).count();
                if (i >= 20) samples.push_back(elapsed);
            }
            std::sort(samples.begin(), samples.end());
            double total = 0;
            for (double value : samples) total += value;
            performance << name << ',' << total/samples.size() << ',' << samples[75] << '\n';
            MapPerimeterReview::Require(collisions == world.GetCollisionGrid(), "Rendering changed collisions");
            MapPerimeterReview::CheckBackgroundPrecedence(map, renderer.get());
            MapPerimeterReview::CheckFallbacks(map, renderer.get(), output/(name+"-fallback.png"));
            std::cout << "MAP PERIMETER PASS " << name << std::endl;
        }
    } catch (const std::exception& error) {
        std::cerr << "MAP PERIMETER FAILED " << error.what() << std::endl;
        result = 1;
    }
    IMG_Quit();
    SDL_Quit();
    return result;
}
