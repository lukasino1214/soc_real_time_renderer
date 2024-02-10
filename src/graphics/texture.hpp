#pragma once

#include "pch.hpp"

struct Texture {
    struct PayLoad {
        std::unique_ptr<Texture> texture;
        daxa::CommandList command_list;
    };

    enum struct DeAllocType: u32 {
        NONE,
        STB,
        ARRAY
    };

    Texture() = default;
    ~Texture();

    auto get_texture_id() -> TextureId;

    Texture(daxa::Device& _device, u32 size_x, u32 size_y, u32 channels, u8* data, daxa::Format format, daxa::ImageUsageFlags flags = {});
    Texture(daxa::Device& _device, u8* data, u32 size, daxa::Format format, daxa::ImageUsageFlags flags = {});
    Texture(daxa::Device& _device, const std::string_view& file_path, daxa::Format format, daxa::ImageUsageFlags flags = {});

    static auto load_texture(daxa::Device& device, u32 size_x, u32 size_y, u32 channels, u8* data, DeAllocType dealloc_memory, daxa::Format format, daxa::ImageUsageFlags flags = {}) -> PayLoad;
    static auto load_texture(daxa::Device& device, u8* data, u32 size, daxa::Format format, daxa::ImageUsageFlags flags = {}) -> PayLoad;
    static auto load_texture(daxa::Device& device, const std::string_view& file_path, daxa::Format format, daxa::ImageUsageFlags flags = {}) -> PayLoad;

    daxa::Device device;
    daxa::ImageId image_id;
    daxa::SamplerId sampler_id;
    bool dont_destroy = false;
    u32vec2 image_dimension = {};
};