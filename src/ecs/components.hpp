#pragma once

#include "uuid.hpp"
#include "graphics/model.hpp"
#include <entt/entt.hpp>

struct UUIDComponent {
    UUID uuid = UUID();

    void draw();
};

struct TagComponent {
    std::string name = "Empty Entity";

    void draw();
};

struct RelationshipComponent {
    entt::entity parent = entt::null;
    std::vector<entt::entity> children = {};

    void draw();
};

struct TransformComponent {
    glm::vec3 position = { 0.0f, 0.0f, 0.0f };
    glm::vec3 rotation = { 0.0f, 0.0f, 0.0f };
    glm::vec3 scale = { 1.0f, 1.0f, 1.0f };
    glm::mat4 model_matrix{1.0f};
    glm::mat4 normal_matrix{1.0f};
    bool is_dirty = true;

    void set_position(const glm::vec3 _position);
    void set_rotation(const glm::vec3 _rotation);
    void set_scale(const glm::vec3 _scale);

    auto get_position() -> glm::vec3;
    auto get_rotation() -> glm::vec3;
    auto get_scale() -> glm::vec3;

    daxa::BufferId buffer = {};
    daxa::SetConstantBufferInfo buffer_info = {};

    void draw();
};

struct MeshComponent {
    std::string path = {};
    std::shared_ptr<Model> model = {};

    void draw();
};

struct PointLightComponent {
    glm::vec3 color = { 1.0f, 1.0f, 1.0f };
    f32 intensity = 16.0f;

    void draw();
};

struct SpotLightComponent {
    glm::vec3 color = { 1.0f, 1.0f, 1.0f };
    f32 intensity = 16.0f;
    f32 cut_off = 20.0f;
    f32 outer_cut_off = 30.0f;

    void draw();
};