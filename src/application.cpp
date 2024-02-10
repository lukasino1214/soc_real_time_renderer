#include "application.hpp"
#include "utils/file_io.hpp"


Application::Application() 
    : window{1280, 720, "stredni odborna cinnost"},
        context{this->window},
        scene{std::make_shared<Scene>("sponza", &context, &window)},
        renderer{&this->window, &this->context, scene}
{
    last_time_point = std::chrono::steady_clock::now();
    controlled_camera.camera.resize(static_cast<i32>(window.get_width()), static_cast<i32>(window.get_height()));

    {
        auto entity = scene->create_entity("sponza model");
        entity.add_component<TransformComponent>().scale = glm::vec3(0.01f);
        auto& mesh_component = entity.add_component<MeshComponent>();
        mesh_component.model = std::make_shared<Model>(context.device, "assets/Sponza/glTF/Sponza.gltf");
        //mesh_component.model = std::make_shared<Model>(context.device, "assets/old_sponza/old_sponza.gltf");
    }

    {
        auto entity = scene->create_entity("damaged helmet model");
        entity.add_component<TransformComponent>();

        auto& mesh_component = entity.add_component<MeshComponent>();
        mesh_component.model = std::make_shared<Model>(context.device, "assets/DamagedHelmet/glTF/DamagedHelmet.gltf");
    }

    // {
    //     auto entity = scene->create_entity("stone wall model");
    //     entity.add_component<TransformComponent>();

    //     auto& mesh_component = entity.add_component<MeshComponent>();
    //     mesh_component.model = std::make_shared<Model>(context.device, "assets/parallax_cube/parallax_cube.gltf");
    // }

    controlled_camera.update(window, delta_time);
    scene->update(delta_time);

    glm::vec2 jitter_vec2 = [this]() -> glm::vec2 {
        glm::vec2 jitter_scale = {1.0f/f32(window.get_width()), 1.0f/f32(window.get_height())};
        f32 g = 1.32471795724474602596f;
        f32 a1 = 1.0f / g;
        f32 a2 = 1.0f / (g * g);

        glm::vec2 jitter = {
            glm::mod(0.5f + a1 * (static_cast<f32>(jitter_index) + 1.0f), 1.0f) - 0.5f,
            glm::mod(0.5f + a2 * (static_cast<f32>(jitter_index) + 1.0f), 1.0f) - 0.5f
        };
        jitter = jitter * jitter_scale;
        jitter_index = (jitter_index + 1) % 32; 

        return jitter;
    }();

    glm::mat4 projection_matrix = controlled_camera.camera.proj_mat;
    projection_matrix[3][0] += jitter_vec2.x;
    projection_matrix[3][1] += jitter_vec2.y;

    glm::mat4 inverse_projection_matrix = glm::inverse(projection_matrix);
    glm::mat4 inverse_view_matrix = glm::inverse(controlled_camera.camera.view_mat);
    glm::mat4 projection_view_matrix = projection_matrix * controlled_camera.camera.view_mat;
    glm::mat4 inverse_projection_view = inverse_projection_matrix * inverse_view_matrix;
    glm::vec4 terrain_y_clip_trick = projection_view_matrix * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f};

    this->context.shader_global_block.globals.camera_projection_matrix = *reinterpret_cast<f32mat4x4*>(&projection_matrix);
    this->context.shader_global_block.globals.camera_inverse_projection_matrix = *reinterpret_cast<f32mat4x4*>(&inverse_projection_matrix);
    this->context.shader_global_block.globals.camera_view_matrix = *reinterpret_cast<f32mat4x4*>(&controlled_camera.camera.view_mat);
    this->context.shader_global_block.globals.camera_inverse_view_matrix = *reinterpret_cast<f32mat4x4*>(&inverse_view_matrix);
    this->context.shader_global_block.globals.camera_projection_view_matrix = *reinterpret_cast<f32mat4x4*>(&projection_view_matrix);
    this->context.shader_global_block.globals.camera_inverse_projection_view_matrix = *reinterpret_cast<f32mat4x4*>(&inverse_projection_view);
    this->context.shader_global_block.globals.terrain_y_clip_trick = *reinterpret_cast<f32vec4*>(&terrain_y_clip_trick);

    this->context.shader_global_block.globals.camera_previous_projection_matrix = *reinterpret_cast<f32mat4x4*>(&projection_matrix);
    this->context.shader_global_block.globals.camera_previous_inverse_projection_matrix = *reinterpret_cast<f32mat4x4*>(&inverse_projection_matrix);
    this->context.shader_global_block.globals.camera_previous_view_matrix = *reinterpret_cast<f32mat4x4*>(&controlled_camera.camera.view_mat);
    this->context.shader_global_block.globals.camera_previous_inverse_view_matrix = *reinterpret_cast<f32mat4x4*>(&inverse_view_matrix);
    this->context.shader_global_block.globals.camera_previous_projection_view_matrix = *reinterpret_cast<f32mat4x4*>(&projection_view_matrix);
    this->context.shader_global_block.globals.camera_previous_inverse_projection_view_matrix = *reinterpret_cast<f32mat4x4*>(&inverse_projection_view);
    this->context.shader_global_block.globals.terrain_previous_y_clip_trick = *reinterpret_cast<f32vec4*>(&terrain_y_clip_trick);

    this->context.shader_global_block.globals.jitter = *reinterpret_cast<f32vec2*>(&jitter_vec2);
    this->context.shader_global_block.globals.previous_jitter = *reinterpret_cast<f32vec2*>(&jitter_vec2);
}

Application::~Application() {}

auto Application::run() -> i32 {
    while(!window.window_state->close_requested) {
        auto new_time_point = std::chrono::steady_clock::now();
        this->delta_time = std::chrono::duration_cast<std::chrono::duration<float, std::chrono::milliseconds::period>>(new_time_point - this->last_time_point).count() * 0.001f;
        this->last_time_point = new_time_point;
        window.update();

        if(window.window_state->resize_requested) {
            renderer.window_resized();
            controlled_camera.camera.resize(static_cast<i32>(window.get_width()), static_cast<i32>(window.get_height()));
            window.window_state->resize_requested = false;
        }

        update();
        renderer.render();
    }

    return 0;
}

void Application::update() {
    controlled_camera.update(window, delta_time);
    scene->update(delta_time);

    glm::vec2 jitter_vec2 = [this]() -> glm::vec2 {
        glm::vec2 jitter_scale = {1.0f/f32(window.get_width()), 1.0f/f32(window.get_height())};
        f32 g = 1.32471795724474602596f;
        f32 a1 = 1.0f / g;
        f32 a2 = 1.0f / (g * g);

        glm::vec2 jitter = {
            glm::mod(0.5f + a1 * (static_cast<f32>(jitter_index) + 1.0f), 1.0f) - 0.5f,
            glm::mod(0.5f + a2 * (static_cast<f32>(jitter_index) + 1.0f), 1.0f) - 0.5f
        };
        jitter = jitter * jitter_scale;
        jitter_index = (jitter_index + 1) % 32; 

        return jitter;
    }();

    glm::mat4 projection_matrix = controlled_camera.camera.proj_mat;
    projection_matrix[3][0] += jitter_vec2.x;
    projection_matrix[3][1] += jitter_vec2.y;
    
    glm::mat4 inverse_projection_matrix = glm::inverse(projection_matrix);
    glm::mat4 inverse_view_matrix = glm::inverse(controlled_camera.camera.view_mat);
    glm::mat4 projection_view_matrix = projection_matrix * controlled_camera.camera.view_mat;
    glm::mat4 inverse_projection_view = inverse_projection_matrix * inverse_view_matrix;
    glm::vec4 terrain_y_clip_trick = projection_view_matrix * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f};

    this->context.shader_global_block.globals.camera_previous_projection_matrix = this->context.shader_global_block.globals.camera_projection_matrix;
    this->context.shader_global_block.globals.camera_previous_inverse_projection_matrix = this->context.shader_global_block.globals.camera_inverse_projection_matrix;
    this->context.shader_global_block.globals.camera_previous_view_matrix = this->context.shader_global_block.globals.camera_view_matrix;
    this->context.shader_global_block.globals.camera_previous_inverse_view_matrix = this->context.shader_global_block.globals.camera_inverse_view_matrix;
    this->context.shader_global_block.globals.camera_previous_projection_view_matrix = this->context.shader_global_block.globals.camera_projection_view_matrix;
    this->context.shader_global_block.globals.camera_previous_inverse_projection_view_matrix = this->context.shader_global_block.globals.camera_inverse_projection_view_matrix;
    this->context.shader_global_block.globals.terrain_previous_y_clip_trick = this->context.shader_global_block.globals.terrain_y_clip_trick;
    this->context.shader_global_block.globals.previous_jitter = this->context.shader_global_block.globals.jitter;

    this->context.shader_global_block.globals.camera_projection_matrix = *reinterpret_cast<f32mat4x4*>(&projection_matrix);
    this->context.shader_global_block.globals.camera_inverse_projection_matrix = *reinterpret_cast<f32mat4x4*>(&inverse_projection_matrix);
    this->context.shader_global_block.globals.camera_view_matrix = *reinterpret_cast<f32mat4x4*>(&controlled_camera.camera.view_mat);
    this->context.shader_global_block.globals.camera_inverse_view_matrix = *reinterpret_cast<f32mat4x4*>(&inverse_view_matrix);
    this->context.shader_global_block.globals.camera_projection_view_matrix = *reinterpret_cast<f32mat4x4*>(&projection_view_matrix);
    this->context.shader_global_block.globals.camera_inverse_projection_view_matrix = *reinterpret_cast<f32mat4x4*>(&inverse_projection_view);
    this->context.shader_global_block.globals.terrain_y_clip_trick = *reinterpret_cast<f32vec4*>(&terrain_y_clip_trick);
    this->context.shader_global_block.globals.jitter = *reinterpret_cast<f32vec2*>(&jitter_vec2);

    this->context.shader_global_block.globals.camera_near_clip = controlled_camera.camera.near_clip;
    this->context.shader_global_block.globals.camera_far_clip = controlled_camera.camera.far_clip;
    this->context.shader_global_block.globals.resolution = { static_cast<i32>(window.get_width()), static_cast<i32>(window.get_height()) };
    this->context.shader_global_block.globals.camera_position = *reinterpret_cast<f32vec3*>(&controlled_camera.position);

    this->context.shader_global_block.globals.delta_time = delta_time;
    this->context.shader_global_block.globals.elapsed_time += delta_time;
    this->context.shader_global_block.globals.frame_counter++;
}
