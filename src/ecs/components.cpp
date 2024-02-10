#include "components.hpp"
#include "ui/ui.hpp"
#include "utils/file_io.hpp"

    void UUIDComponent::draw() {
    GUI::begin_properties();

    GUI::push_deactivated_status();
    GUI::u64_property("UUID:", uuid.uuid, nullptr, ImGuiInputTextFlags_ReadOnly);
    GUI::pop_deactivated_status();

    GUI::end_properties();
}

void TagComponent::draw() {
    GUI::begin_properties();

    GUI::string_property("Tag:", name, nullptr, ImGuiInputTextFlags_None);
    
    GUI::end_properties();
}

void RelationshipComponent::draw() {

}

void TransformComponent::draw() {
    GUI::begin_properties(ImGuiTableFlags_BordersInnerV);

    static std::array<f32, 3> reset_values = { 0.0f, 0.0f, 0.0f };
    static std::array<const char*, 3> tooltips = { "Some tooltip.", "Some tooltip.", "Some tooltip." };

    if (GUI::vec3_property("Position:", position, reset_values.data(), tooltips.data())) { is_dirty = true; }
    if (GUI::vec3_property("Rotation:", rotation, reset_values.data(), tooltips.data())) { is_dirty = true; }

    reset_values = { 1.0f, 1.0f, 1.0f };

    if (GUI::vec3_property("Scale:", scale, reset_values.data(), tooltips.data())) { is_dirty = true; }

    GUI::end_properties();
}

void TransformComponent::set_position(const glm::vec3 _position) {
    position = _position;
    is_dirty = true;
}

void TransformComponent::set_rotation(const glm::vec3 _rotation) {
    rotation = _rotation;
    is_dirty = true;
}

void TransformComponent::set_scale(const glm::vec3 _scale) {
    scale = _scale;
    is_dirty = true;
}

auto TransformComponent::get_position() -> glm::vec3 {
    return position;
}

auto TransformComponent::get_rotation() -> glm::vec3 {
    return rotation;
}

auto TransformComponent::get_scale() -> glm::vec3 {
    return scale;
}

void MeshComponent::draw() {}

void PointLightComponent::draw() {
    GUI::begin_properties();

    GUI::f32_property("Intensity:", intensity, nullptr);
    ImGui::ColorPicker3("Color:", &color[0]);

    GUI::end_properties();
}

void SpotLightComponent::draw() {
    GUI::begin_properties();

    GUI::f32_property("Intensity:", intensity, nullptr);
    GUI::f32_property("Cut Off:", cut_off, nullptr);
    GUI::f32_property("Outer Cut Off:", outer_cut_off, nullptr);
    ImGui::ColorPicker3("Color:", &color[0]);

    GUI::end_properties();
}