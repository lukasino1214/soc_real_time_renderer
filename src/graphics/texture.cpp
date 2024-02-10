#include "texture.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <ImfArray.h>
#include <ImfChannelList.h>
#include <ImfInputFile.h>
#include <ImfMatrixAttribute.h>
#include <ImfOutputFile.h>
#include <ImfStringAttribute.h>
#include <ImfRgbaFile.h>
#include <OpenEXRConfig.h>

#include <thread>

using namespace OPENEXR_IMF_NAMESPACE;
using namespace IMATH_NAMESPACE;


Texture::~Texture() {
    if(!dont_destroy) {
        device.destroy_image(this->image_id);
        device.destroy_sampler(this->sampler_id);
    }
}

auto Texture::get_texture_id() -> TextureId {
    return TextureId { .image_id = image_id.default_view(), .sampler_id = sampler_id };
}

Texture::Texture(daxa::Device& _device, u32 size_x, u32 size_y, u32 channels, u8* data, daxa::Format format, daxa::ImageUsageFlags flags) : device{_device} {
    auto payload = load_texture(device, size_x, size_y, channels, data, DeAllocType::STB, format, flags);
    payload.texture->dont_destroy = true;

    device.submit_commands({ .command_lists = { payload.command_list }});
    device.wait_idle();

    image_id = payload.texture->image_id;
    sampler_id = payload.texture->sampler_id;
    image_dimension = payload.texture->image_dimension;
}

Texture::Texture(daxa::Device& _device, u8* data, u32 size, daxa::Format format, daxa::ImageUsageFlags flags) : device{_device} {
    auto payload = load_texture(device, data, size, format, flags);
    payload.texture->dont_destroy = true;

    device.submit_commands({ .command_lists = { payload.command_list }});
    device.wait_idle();

    image_id = payload.texture->image_id;
    sampler_id = payload.texture->sampler_id;
    image_dimension = payload.texture->image_dimension;
}

Texture::Texture(daxa::Device& _device, const std::string_view& file_path, daxa::Format format, daxa::ImageUsageFlags flags) : device{_device} {
    auto payload = load_texture(device, file_path, format, flags);
    payload.texture->dont_destroy = true;

    device.submit_commands({ .command_lists = { payload.command_list }});
    device.wait_idle();

    image_id = payload.texture->image_id;
    sampler_id = payload.texture->sampler_id;
    image_dimension = payload.texture->image_dimension;
}

auto Texture::load_texture(daxa::Device& device, u32 size_x, u32 size_y, u32 channels, u8* data, DeAllocType dealloc_memory, daxa::Format format, daxa::ImageUsageFlags flags) -> Texture::PayLoad {
    u8* image_data = data;
    bool deallocate_image_data = false;
    u32 bytes_per_channel = 1;

    if(format == daxa::Format::R8G8B8A8_UNORM) {
        bytes_per_channel = 1;
    } else if(format == daxa::Format::R8G8B8A8_SRGB) {
        bytes_per_channel = 1;
    } else if(format == daxa::Format::R32_SFLOAT) {
        bytes_per_channel = 4;
        channels = 1;
    } else if(format == daxa::Format::R32G32B32A32_SFLOAT) {
        bytes_per_channel = 4;
        channels = 4;
    } else {
        throw std::runtime_error("unsupported texture format: " + std::to_string(static_cast<u32>(format)));
    }
    
    // TODO(Lukas): no idea why all pngs have 3 channels
    // if(channels == 3) {
    //     deallocate_image_data = true;
    //     image_data = new u8[size_x * size_y * 4 * sizeof(u8)];

    //     u8* rgba = image_data;
    //     u8* rgb = data;
    //     for (u32 i = 0; i < size_x * size_y; ++i) {
    //         rgba[0] = rgb[0];
    //         rgba[1] = rgb[1];
    //         rgba[2] = rgb[2];
    //         rgba[3] = 255;

    //         rgba += 4;
    //         rgb += 3;
    //     }

    // } else {
    //     image_data = data;
    // }

    u32 mip_levels = static_cast<uint32_t>(std::floor(std::log2(std::max(size_x, size_y)))) + 1;

    daxa::ImageId image_id = device.create_image({
        .dimensions = 2,
        .format = format,
        .size = { static_cast<u32>(size_x), static_cast<u32>(size_y), 1 },
        .mip_level_count = mip_levels,
        .array_layer_count = 1,
        .sample_count = 1,
        .usage = daxa::ImageUsageFlagBits::SHADER_SAMPLED | daxa::ImageUsageFlagBits::TRANSFER_DST | daxa::ImageUsageFlagBits::TRANSFER_SRC | flags,
        .allocate_info = daxa::MemoryFlagBits::DEDICATED_MEMORY
    });

    daxa::SamplerId sampler_id = device.create_sampler({
        .magnification_filter = daxa::Filter::LINEAR,
        .minification_filter = daxa::Filter::LINEAR,
        .mipmap_filter = daxa::Filter::LINEAR,
        .address_mode_u = daxa::SamplerAddressMode::REPEAT,
        .address_mode_v = daxa::SamplerAddressMode::REPEAT,
        .address_mode_w = daxa::SamplerAddressMode::REPEAT,
        .mip_lod_bias = 0.0f,
        .enable_anisotropy = true,
        .max_anisotropy = 16.0f,
        .enable_compare = false,
        .compare_op = daxa::CompareOp::ALWAYS,
        .min_lod = 0.0f,
        .max_lod = static_cast<f32>(mip_levels),
        .enable_unnormalized_coordinates = false,
    });

    daxa::BufferId staging_buffer = device.create_buffer({
        .size = size_x * size_y * bytes_per_channel * channels,
        .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
        .name = "staging buffer"
    });

    auto* buffer_ptr = device.get_host_address_as<u8>(staging_buffer);
    std::memcpy(buffer_ptr, image_data, size_x * size_y * bytes_per_channel * channels);

    daxa::CommandList cmd_list = device.create_command_list({.name = "upload command list"});

    cmd_list.pipeline_barrier_image_transition({
        .src_access = daxa::AccessConsts::TRANSFER_READ_WRITE,
        .dst_access = daxa::AccessConsts::READ_WRITE,
        .src_layout = daxa::ImageLayout::UNDEFINED,
        .dst_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
        .image_slice = {
            .base_mip_level = 0,
            .level_count = mip_levels,
            .base_array_layer = 0,
            .layer_count = 1,
        },
        .image_id = image_id,
    });

    cmd_list.copy_buffer_to_image({
        .buffer = staging_buffer,
        .buffer_offset = 0,
        .image = image_id,
        .image_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
        .image_slice = {
            .mip_level = 0,
            .base_array_layer = 0,
            .layer_count = 1,
        },
        .image_offset = { 0, 0, 0 },
        .image_extent = { static_cast<u32>(size_x), static_cast<u32>(size_y), 1 }
    });

    cmd_list.pipeline_barrier({
        .src_access = daxa::AccessConsts::HOST_WRITE,
        .dst_access = daxa::AccessConsts::TRANSFER_READ,
    });

    auto image_info = device.info_image(image_id);

    std::array<i32, 3> mip_size = {
        static_cast<i32>(image_info.size.x),
        static_cast<i32>(image_info.size.y),
        static_cast<i32>(image_info.size.z),
    };

    for(u32 i = 1; i < image_info.mip_level_count; i++) {
        cmd_list.pipeline_barrier_image_transition({
            .src_access = daxa::AccessConsts::TRANSFER_WRITE,
            .dst_access = daxa::AccessConsts::BLIT_READ,
            .src_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
            .dst_layout = daxa::ImageLayout::TRANSFER_SRC_OPTIMAL,
            .image_slice = {
                .base_mip_level = i - 1,
                .level_count = 1,
                .base_array_layer = 0,
                .layer_count = 1,
            },
            .image_id = image_id,
        });

        std::array<i32, 3> next_mip_size = {
            std::max<i32>(1, mip_size[0] / 2),
            std::max<i32>(1, mip_size[1] / 2),
            std::max<i32>(1, mip_size[2] / 2),
        };

        cmd_list.blit_image_to_image({
            .src_image = image_id,
            .src_image_layout = daxa::ImageLayout::TRANSFER_SRC_OPTIMAL,
            .dst_image = image_id,
            .dst_image_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
            .src_slice = {
                .mip_level = i - 1,
                .base_array_layer = 0,
                .layer_count = 1,
            },
            .src_offsets = {{{0, 0, 0}, {mip_size[0], mip_size[1], mip_size[2]}}},
            .dst_slice = {
                .mip_level = i,
                .base_array_layer = 0,
                .layer_count = 1,
            },
            .dst_offsets = {{{0, 0, 0}, {next_mip_size[0], next_mip_size[1], next_mip_size[2]}}},
            .filter = daxa::Filter::LINEAR,
        });

        cmd_list.pipeline_barrier_image_transition({
            .src_access = daxa::AccessConsts::TRANSFER_WRITE,
            .dst_access = daxa::AccessConsts::BLIT_READ,
            .src_layout = daxa::ImageLayout::TRANSFER_SRC_OPTIMAL,
            .dst_layout = daxa::ImageLayout::READ_ONLY_OPTIMAL,
            .image_slice = {
                .base_mip_level = i - 1,
                .level_count = 1,
                .base_array_layer = 0,
                .layer_count = 1,
            },
            .image_id = image_id,
        });
        
        mip_size = next_mip_size;
    }

    cmd_list.pipeline_barrier_image_transition({
        .src_access = daxa::AccessConsts::TRANSFER_READ_WRITE,
        .dst_access = daxa::AccessConsts::READ_WRITE,
        .src_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
        .dst_layout = daxa::ImageLayout::READ_ONLY_OPTIMAL,
        .image_slice = {
            .base_mip_level = image_info.mip_level_count - 1,
            .level_count = 1,
            .base_array_layer = 0,
            .layer_count = 1,
        },
        .image_id = image_id,
    });

    cmd_list.destroy_buffer_deferred(staging_buffer);
    cmd_list.complete();

    if(deallocate_image_data) { delete[] image_data; }
    if(dealloc_memory == DeAllocType::STB) {
        stbi_image_free(data);
    }

    if(dealloc_memory == DeAllocType::ARRAY) {
        delete[] data;
    }

    auto tex = std::make_unique<Texture>();
    tex->device = device;
    tex->image_id = image_id;
    tex->sampler_id = sampler_id;
    tex->image_dimension = { size_x, size_y };

    return { .texture = std::move(tex), .command_list = cmd_list };
}

auto Texture::load_texture(daxa::Device& device, u8* data, u32 size, daxa::Format format, daxa::ImageUsageFlags flags) -> Texture::PayLoad {
    i32 size_x = 0;
    i32 size_y = 0;
    i32 num_channels = 0;
    u8* loaded_data = stbi_load_from_memory(data, static_cast<i32>(size), &size_x, &size_y, &num_channels, 0);
    if(loaded_data == nullptr) {
        throw std::runtime_error("Textures couldn't be loaded from memory");
    }

    return load_texture(device, static_cast<u32>(size_x), static_cast<u32>(size_y), static_cast<u32>(num_channels), loaded_data, DeAllocType::STB, format, flags);
}

struct CreateStagingBufferInfo {
    daxa_i32vec2 dimensions;
    daxa_u32 present_channel_count;
    daxa::Device & device;
    std::string name;
    std::array<std::string,4> channel_names;
    std::unique_ptr<InputFile> & file;
};

struct ElemType {
    daxa::Format format;
    daxa_u32 elem_cnt;
    PixelType type;
    std::array<std::string, 4> channel_names;
};

auto get_texture_element(const std::unique_ptr<InputFile> & file) -> ElemType {
    struct ChannelInfo
    {
        std::string name;
        PixelType type;
        bool linear;
    };

    std::vector<ChannelInfo> parsed_channels;

    const ChannelList &channels = file->header().channels();
    for (ChannelList::ConstIterator i = channels.begin(); i != channels.end(); ++i) {
        parsed_channels.emplace_back(ChannelInfo{
            .name = i.name(),
            .type = i.channel().type,
            .linear = i.channel().pLinear
        });
    }

    ElemType ret;

    std::array channels_present{false, false, false, false};
    for(u32 j = 0; const auto & parsed_channel : parsed_channels) {
        ret.channel_names.at(j++) = parsed_channel.name;
        if(parsed_channel.name == "R") {channels_present.at(0) = true;}
        else if(parsed_channel.name == "G") {channels_present.at(1) = true;}
        else if(parsed_channel.name == "B") {channels_present.at(2) = true;}
        else if(parsed_channel.name == "A") {channels_present.at(3) = true;}
    }

    std::array<std::array<daxa::Format, 4>, 4> formats_lut = {{
        {daxa::Format::R32_UINT, daxa::Format::R16_SFLOAT, daxa::Format::R32_SFLOAT, daxa::Format::R8_UINT},
        {daxa::Format::R32G32_UINT, daxa::Format::R16G16_SFLOAT, daxa::Format::R32G32_SFLOAT, daxa::Format::R8G8_UINT},
        {daxa::Format::R32G32B32_UINT, daxa::Format::R16G16B16_SFLOAT, daxa::Format::R32G32B32_SFLOAT, daxa::Format::R8G8B8_UINT},
        {daxa::Format::R32G32B32A32_UINT, daxa::Format::R16G16B16A16_SFLOAT, daxa::Format::R32G32B32A32_SFLOAT, daxa::Format::R8G8B8A8_UINT},
    }};

    u32 channel_cnt = parsed_channels.size() == 1 ? 0 : 3;
    auto texture_format = formats_lut.at(channel_cnt).at(daxa_u32(parsed_channels.back().type));
    ret.format = texture_format;
    ret.elem_cnt = static_cast<u32>(parsed_channels.size());
    ret.type = parsed_channels.back().type;
    return ret;
}

template <u32 NumElems, typename T, PixelType PixT>
auto load_texture_data(CreateStagingBufferInfo & info) -> u8* {
    using Elem = std::array<T,NumElems>;

    auto pos_from_name = [](const std::string_view name) -> i32 {
        if(name[0] == 'R') return 0;
        else if(name[0] == 'G') return 1;
        else if(name[0] == 'B') return 2;
        else if(name[0] == 'A') return 3;
        else return -1;
    };

    Elem* data = new Elem[static_cast<u32>(info.dimensions.x * info.dimensions.y) * NumElems * u32(sizeof(T))];

    FrameBuffer frame_buffer;

    std::array<int, 4> positions{-1, -1, -1, -1};
    std::array<bool, 4> position_occupied{false, false, false, false};

    if(info.present_channel_count == 3) { info.channel_names.at(3) = "A"; }

    for(u32 i = 0; i < NumElems; i++) {
        positions.at(i) = pos_from_name(info.channel_names.at(i));
        if (positions.at(i) != -1) {position_occupied.at(static_cast<u32>(positions.at(static_cast<u32>(i)))) = true; }
    }

    for(u32 i = 0; i < NumElems; ++i) {
        if(positions.at(i) == -1) {
            for(u32 j = 0; j < NumElems; ++j) {
                if(!position_occupied.at(j)) {
                    position_occupied.at(j) = true;
                    positions.at(i) = static_cast<i32>(j);
                    break;
                }
            }
        }
    }

    for(u32 i = 0; i < NumElems; i++) {
        frame_buffer.insert(
            info.channel_names.at(i),
            Slice(
                PixT,
                reinterpret_cast<char*>(&data[0].at(static_cast<u32>(positions.at(i)))), 
                sizeof(Elem), sizeof(Elem) * static_cast<u32>(info.dimensions.x), 
                1, 1, 0.0
            )
        );  
    }

    info.file->setFrameBuffer(frame_buffer);
    info.file->readPixels(0, info.dimensions.y - 1);

    return reinterpret_cast<u8*>(data);
}

auto Texture::load_texture(daxa::Device& device, const std::string_view& file_path, daxa::Format format, daxa::ImageUsageFlags flags) -> Texture::PayLoad {    
    std::string extension = std::filesystem::path{file_path.data()}.extension().string();

    i32 size_x = 0;
    i32 size_y = 0;
    i32 num_channels = 0;
    u8* data = nullptr;
    DeAllocType de_alloc_type = DeAllocType::NONE;

    if(extension == ".jpg" || extension == ".png") {
        data = stbi_load(file_path.data(), &size_x, &size_y, &num_channels, 4);
        num_channels = 4;
        de_alloc_type = DeAllocType::STB;

        if(data == nullptr) {
            throw std::runtime_error("Textures couldn't be found with path: " + std::string{file_path});
        }
    } else if(extension == ".exr") {
        setGlobalThreadCount(static_cast<i32>(std::thread::hardware_concurrency()));
        std::unique_ptr<InputFile> file = std::make_unique<InputFile>(file_path.data());
 

        Box2i data_window = file->header().dataWindow();
        daxa::i32vec2 resolution = {
            data_window.max.x - data_window.min.x + 1,
            data_window.max.y - data_window.min.y + 1
        };

        auto texture_elem = get_texture_element(file);

        CreateStagingBufferInfo stanging_info{
            .dimensions = resolution,
            .present_channel_count = texture_elem.elem_cnt,
            .device = device,
            .name = "exr staging texture buffer",
            .channel_names = texture_elem.channel_names,
            .file = file
        };

        num_channels = static_cast<i32>(texture_elem.elem_cnt);
        de_alloc_type = DeAllocType::ARRAY;
        format = texture_elem.format;
        size_x = resolution.x;
        size_y = resolution.y;

        switch(texture_elem.elem_cnt) {
            case 1: {
                if (texture_elem.type == PixelType::UINT) { data = load_texture_data<1, daxa_u32, PixelType::UINT>(stanging_info); }
                else if (texture_elem.type == PixelType::HALF) { data = load_texture_data<1, half, PixelType::HALF>(stanging_info); }
                else if (texture_elem.type == PixelType::FLOAT) { data = load_texture_data<1, daxa_f32, PixelType::FLOAT>(stanging_info); }
                break;
            }
            case 2: {
                break;
            }
            case 3: {
                if (texture_elem.type == PixelType::UINT) { data = load_texture_data<4, daxa_u32, PixelType::UINT>(stanging_info); }
                else if (texture_elem.type == PixelType::HALF) { data = load_texture_data<4, half, PixelType::HALF>(stanging_info); }
                else if (texture_elem.type == PixelType::FLOAT) { data = load_texture_data<4, daxa_f32, PixelType::FLOAT>(stanging_info); }
                break;
            }
            case 4: {
                if(texture_elem.type == PixelType::UINT)  { data = load_texture_data<4, daxa_u32, PixelType::UINT>(stanging_info); }
                else if(texture_elem.type == PixelType::HALF) { data = load_texture_data<4, half, PixelType::HALF>(stanging_info); }
                else if(texture_elem.type == PixelType::FLOAT) { data = load_texture_data<4, daxa_f32, PixelType::FLOAT>(stanging_info); }
                break;
            }
        }
    } else {
        throw std::runtime_error("Unsupported textures type with path: " + std::string{file_path});
    }
    

    return load_texture(device, static_cast<u32>(size_x), static_cast<u32>(size_y), static_cast<u32>(num_channels), data, de_alloc_type, format, flags);
}