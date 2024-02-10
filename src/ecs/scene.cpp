#include "scene.hpp"
#include "entity.hpp"
#include "components.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

Scene::Scene(const std::string_view& _name, Context* _context, AppWindow* _window) : name{_name}, registry{std::make_unique<entt::registry>()}, context{_context}, window{_window} {}

Scene::~Scene() {
    iterate([this](Entity entity){
        if(entity.has_component<TransformComponent>()) {
            auto& tc = entity.get_component<TransformComponent>();
            if(!tc.buffer.is_empty()) { context->device.destroy_buffer(tc.buffer); }
        }
    });
}

auto Scene::create_entity(const std::string_view& _name) -> Entity {
    return create_entity_with_UUID(_name, UUID());
}

auto Scene::create_entity_with_UUID(const std::string_view& _name, UUID _uuid) -> Entity {
    Entity entity = Entity(registry->create(), this);

    entity.add_component<UUIDComponent>().uuid = _uuid;
    entity.add_component<TagComponent>().name = _name;
    entity.add_component<RelationshipComponent>();

    return entity;
}

void Scene::destroy_entity(const Entity& entity) {
    registry->destroy(entity.handle);
}

void Scene::iterate(std::function<void(Entity)> fn) {
    registry->each([&](auto entity_handle) {
        Entity entity = {entity_handle, this};
        if (!entity) { return; }
        if(!entity.has_component<TagComponent>()) { return; } // stupid fix I have no idea why its happening

        fn(entity);
    });
};

void Scene::update(f32 delta_time) {
    context->shader_global_block.globals.point_light_count = 0;
    context->shader_global_block.globals.spot_light_count = 0;

    iterate([&](Entity entity) {
        if(entity.has_component<TransformComponent>()) {
            auto& tc = entity.get_component<TransformComponent>();

            if(tc.buffer.is_empty()) {
                tc.buffer = context->device.create_buffer(daxa::BufferInfo{
                    .size = static_cast<u32>(((static_cast<i32>(sizeof(TransformInfoBlock)) + 256 - 1) / 256) * 256 * context->swapchain.info().max_allowed_frames_in_flight),
                    .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_SEQUENTIAL_WRITE | daxa::MemoryFlagBits::DEDICATED_MEMORY,
                    .name = "transform buffer",
                });

            }

            if(tc.is_dirty) {
                tc.model_matrix = glm::translate(glm::mat4(1.0f), tc.position) 
                    * glm::toMat4(glm::quat({glm::radians(tc.rotation.x), glm::radians(tc.rotation.y), glm::radians(tc.rotation.z)})) 
                    * glm::scale(glm::mat4(1.0f), tc.scale);

                tc.normal_matrix = glm::transpose(glm::inverse(tc.model_matrix));
            }

            char* mapped_ptr = reinterpret_cast<char*>(context->device.get_host_address(tc.buffer));
            auto* ptr = reinterpret_cast<TransformInfoBlock*>(mapped_ptr + ((static_cast<i32>(sizeof(TransformInfoBlock)) + 256 - 1) / 256) * 256 * context->frame_index);
            ptr->transform.model_matrix = *reinterpret_cast<f32mat4x4*>(&tc.model_matrix);
            ptr->transform.normal_matrix = *reinterpret_cast<f32mat4x4*>(&tc.normal_matrix);

            tc.buffer_info = {
                .slot = TRANSFORM_INFO_SLOT,
                .buffer = tc.buffer,
                .size = ((static_cast<i32>(sizeof(TransformInfoBlock)) + 256 - 1) / 256) * 256,
                .offset = ((static_cast<i32>(sizeof(TransformInfoBlock)) + 256 - 1) / 256) * 256 * context->frame_index,
            };
        }

        if(entity.has_component<PointLightComponent>()) {
            auto& lc = entity.get_component<PointLightComponent>();
            auto& tc = entity.get_component<TransformComponent>();
        
            auto& globals = context->shader_global_block.globals;
            globals.point_lights[globals.point_light_count++] = {
                .position = *reinterpret_cast<f32vec3*>(&tc.position),
                .color = *reinterpret_cast<f32vec3*>(&lc.color),
                .intensity = lc.intensity
            };
        }

        if(entity.has_component<SpotLightComponent>()) {
            auto& lc = entity.get_component<SpotLightComponent>();
            auto& tc = entity.get_component<TransformComponent>();
        
            glm::vec3 rot = tc.rotation;
            glm::vec3 dir = { 0.0f, -1.0f, 0.0f };
            dir = glm::rotateX(dir, glm::radians(rot.x));
            dir = glm::rotateY(dir, glm::radians(rot.y));
            dir = glm::rotateZ(dir, glm::radians(rot.z));

            auto& globals = context->shader_global_block.globals;
            globals.spot_lights[globals.spot_light_count++] = {
                .position = *reinterpret_cast<f32vec3*>(&tc.position),
                .direction = *reinterpret_cast<f32vec3*>(&dir),
                .color = *reinterpret_cast<f32vec3*>(&lc.color),
                .intensity = lc.intensity,
                .cut_off = glm::cos(glm::radians(lc.cut_off)),
                .outer_cut_off = glm::cos(glm::radians(lc.outer_cut_off))
            };
        }
    });
}
