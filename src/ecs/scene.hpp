#pragma once

#include <entt/entt.hpp>
#include "context.hpp"
#include "uuid.hpp"

struct Entity;

struct Scene {
    Scene(const std::string_view& _name, Context* _context, AppWindow* _window);
    ~Scene();

    auto create_entity(const std::string_view& _name) -> Entity;
    auto create_entity_with_UUID(const std::string_view& _name, UUID _uuid) -> Entity;

    void destroy_entity(const Entity& entity);

    void iterate(std::function<void(Entity)> fn);

    void update(f32 delta_time);

    std::string name;
    std::unique_ptr<entt::registry> registry;
    Context* context;
    AppWindow* window;
};