#pragma once

#include "ui/ui.hpp"
#include <daxa/utils/imgui.hpp>

#include "window.hpp"
#include "context.hpp"
#include "camera.hpp"
#include "ecs/scene.hpp"
#include "ecs/entity.hpp"
#include "ecs/components.hpp"
#include "ui/editor/scene_hiearchy_panel.hpp"
#include "utils/scrolling_buffer.hpp"

struct Renderer {
    Renderer(AppWindow* _window, Context* _context, const std::shared_ptr<Scene>& scene);
    ~Renderer();

    void render();
    void window_resized();

    void recreate_framebuffer();
    void compile_pipelines();
    void rebuild_task_graph();

    AppWindow* window = {};
    Context* context = {};

    std::shared_ptr<SceneHiearchyPanel> scene_hiearchy_panel;

    daxa::TaskImage swapchain_image = {};
    daxa::TaskImage color_image = {};
    daxa::TaskImage albedo_image = {};
    daxa::TaskImage emissive_image = {};
    daxa::TaskImage normal_image = {};
    daxa::TaskImage metallic_roughness_image = {};
    daxa::TaskImage depth_image = {};
    daxa::TaskImage velocity_image = {};
    daxa::TaskImage previous_color_image = {};
    daxa::TaskImage previous_velocity_image = {};
    daxa::TaskImage resolved_image = {};
    daxa::TaskImageView min_hiz_image = {};
    daxa::TaskImageView max_hiz_image = {};
    daxa::TaskImage clouds_image = {};
    daxa::TaskImage ssr_image = {};
    daxa::TaskImage depth_of_field_image = {};
    u32 depth_of_field_mips = {};

    std::unique_ptr<Texture> noise_texture = {};

    u32 mip_chain_length = 4;
    std::vector<daxa::TaskImage> bloom_mip_chain = {};

    daxa::TaskImage ssao_image = {};
    daxa::TaskImage ssao_blur_image = {};

    std::vector<daxa::TaskImage> images = {};
    std::vector<std::pair<daxa::ImageInfo, daxa::TaskImage>> frame_buffer_images = {};

    daxa::TaskBuffer terrain_vertices = {};
    daxa::TaskBuffer terrain_indices = {};
    std::unique_ptr<Texture> terrain_heightmap = {};
    std::unique_ptr<Texture> terrain_albedomap = {};
    daxa::TaskImage terrain_normalmap_task = {};

    daxa::TaskImage sun_shadow_image = {};
    glm::vec3 angle_direction = { 4.0, 0.0f, 0.0f };

    u32 terrain_index_size = {};

    daxa::TaskBuffer auto_exposure_buffer = {};

    std::vector<daxa::TaskBuffer> buffers = {};

    daxa::TaskGraph render_task_graph = {};

    daxa::ImGuiRenderer imgui_renderer = {};

    ScrollingBuffer<f32> accumulated_time = {};
    std::unordered_map<std::string, std::string> names = {};
    std::unordered_map<std::string, ScrollingBuffer<f32>> metrics = {};
};
