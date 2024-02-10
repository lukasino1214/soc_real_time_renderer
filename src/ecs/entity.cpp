#include "entity.hpp"
#include "components.hpp"

Entity::Entity(entt::entity _handle, Scene* _scene) : handle{_handle}, scene{_scene} {}

auto Entity::get_name() -> std::string_view {
    return get_component<TagComponent>().name;
}

auto Entity::get_uuid() -> UUID {
    return get_component<UUIDComponent>().uuid;
}
