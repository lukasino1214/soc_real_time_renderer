#pragma once 

#include "graphics/window.hpp"
#include "graphics/camera.hpp"
#include "context.hpp"
#include "graphics/renderer.hpp"
#include "ecs/scene.hpp"
#include "ecs/entity.hpp"
#include "ecs/components.hpp"


struct Application {
    Application();
    ~Application();

    auto run() -> i32;
    void update();

    AppWindow window;
    Context context;
    std::shared_ptr<Scene> scene;
    Renderer renderer;

    ControlledCamera3D controlled_camera = {};
    f32 delta_time = 0.016f;
    std::chrono::time_point<std::chrono::steady_clock> last_time_point = {};
    u32 jitter_index = 0;
};
