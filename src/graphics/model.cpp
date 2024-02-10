#include "model.hpp"

#include <fastgltf/parser.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/util.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <stb_image.h>

#include "utils/threadpool.hpp"

Model::Model(daxa::Device _device, const std::string_view& file_path) : device{_device} {
    std::filesystem::path path(file_path.data());

    if(!std::filesystem::exists(path)) {
        throw std::runtime_error("couldnt not find model: " + path.string());
    }

    std::unique_ptr<fastgltf::GltfDataBuffer> gltf;
    fastgltf::GltfDataBuffer data_buffer;
    fastgltf::Asset asset;

    {
        fastgltf::Parser parser(fastgltf::Extensions::KHR_mesh_quantization);

        constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::AllowDouble | fastgltf::Options::LoadGLBBuffers | fastgltf::Options::LoadExternalBuffers;
        fastgltf::GltfDataBuffer data;
        data.loadFromFile(path);

        auto type = fastgltf::determineGltfFileType(&data);
		fastgltf::Expected<fastgltf::Asset> asset_(fastgltf::Error::None);
        if (type == fastgltf::GltfType::glTF) {
	        asset_ = parser.loadGLTF(&data, path.parent_path(), gltfOptions);
        } else if (type == fastgltf::GltfType::GLB) {
	        asset_ = parser.loadBinaryGLTF(&data, path.parent_path(), gltfOptions);
        } else {
            std::cerr << "Failed to determine glTF container" << '\n';
            //return false;
        }

        // if (asset.error() != fastgltf::Error::None) {
        //     std::cerr << "Failed to load glTF: " << fastgltf::getErrorMessage(asset.error()) << '\n';
        //     //return false;
        // }

        asset = std::move(asset_.get());
    }

    auto get_image_type = [&](usize image_index) -> daxa::Format {
        for(auto& material : asset.materials) {
            if(material.pbrData.baseColorTexture.has_value()) {
                u32 diffuseTextureIndex = static_cast<u32>(material.pbrData.baseColorTexture.value().textureIndex);
                auto& diffuseTexture = asset.textures[diffuseTextureIndex];
                if (image_index == diffuseTexture.imageIndex.value()) {
                    return daxa::Format::R8G8B8A8_SRGB;
                }
            }

            if(material.emissiveTexture.has_value()) {
                u32 emissiveTextureIndex = static_cast<u32>(material.emissiveTexture.value().textureIndex);
                auto& emissiveTexture = asset.textures[emissiveTextureIndex];
                if (image_index == emissiveTexture.imageIndex.value()) {
                    return daxa::Format::R8G8B8A8_SRGB;
                }
            }
        }
        
        return daxa::Format::R8G8B8A8_UNORM;
    };

    images.resize(asset.images.size());
    std::vector<std::pair<Texture::PayLoad, u32>> textures;
    textures.resize(asset.images.size());
    ThreadPool pool(std::thread::hardware_concurrency());

    auto process_image = [&](fastgltf::Image& image, u32 index) {
        Texture::PayLoad tex;
        std::visit(fastgltf::visitor {
            [](auto&) {},
            [&](fastgltf::sources::URI& image_path) {
               tex = Texture::load_texture(device, path.parent_path().string() + '/' + std::string(image_path.uri.path().begin(), image_path.uri.path().end()), get_image_type(index));
            },

            [&](fastgltf::sources::Vector& vector) {
                tex = Texture::load_texture(device, vector.bytes.data(), static_cast<u32>(vector.bytes.size()), get_image_type(index));
                // i32 width = 0, height = 0, nrChannels = 0;
                // unsigned char *data = nullptr;
                // data = stbi_load_from_memory(vector.bytes.data(), static_cast<i32>(vector.bytes.size()), &width, &height, &nrChannels, 0);
                // if(!data) {
                //     throw std::runtime_error("wtf");
                // }

                // // utter garbage thanks to RGB formats are not supported
                // unsigned char* buffer = nullptr;
                // u64 buffer_size = {};

                // if (nrChannels == 3) {
                //     buffer_size = static_cast<u64>(width * height) * 4;
                //     std::vector<unsigned char> image_data(buffer_size, 255);

                //     buffer = image_data.data();
                //     unsigned char* rgba = buffer;
                //     unsigned char* rgb = data;
                //     for (i32 i = 0; i < width * height; ++i) {
                //         std::memcpy(rgba, rgb, sizeof(unsigned char) * 3);
                //         rgba += 4;
                //         rgb += 3;
                //     }

                //     tex = Texture::load_texture(device, static_cast<u32>(width), static_cast<u32>(height), buffer, get_image_type(index));
                // }
                // else {
                //     buffer = data;
                //     buffer_size = static_cast<u64>(width * height) * 4;
                //     tex = Texture::load_texture(device, static_cast<u32>(width), static_cast<u32>(height), buffer, get_image_type(index));
                // }

                // stbi_image_free(data);
            },

            [&](fastgltf::sources::BufferView& view) {
                auto& buffer_view = asset.bufferViews[view.bufferViewIndex];
                auto& gltf_buffer = asset.buffers[buffer_view.bufferIndex];

                std::visit(fastgltf::visitor {
                    [](auto&) {},
                    [&](fastgltf::sources::Vector& vector) {
                        tex = Texture::load_texture(device, vector.bytes.data(), static_cast<u32>(vector.bytes.size()), get_image_type(index));
                        // i32 width = 0, height = 0, nrChannels = 0;
                        // unsigned char *data = nullptr;
                        // data = stbi_load_from_memory(vector.bytes.data(), static_cast<i32>(vector.bytes.size()), &width, &height, &nrChannels, 0);
                        // if(!data) {
                        //     throw std::runtime_error("wtf");
                        // }

                        // // utter garbage thanks to RGB formats are no supported
                        // unsigned char* buffer = {};
                        // u64 buffer_size = {};

                        // if (nrChannels == 3) {
                        //     buffer_size = static_cast<u64>(width * height) * 4;
                        //     std::vector<unsigned char> image_data(buffer_size, 255);

                        //     buffer = image_data.data();
                        //     unsigned char* rgba = buffer;
                        //     unsigned char* rgb = data;
                        //     for (i32 i = 0; i < width * height; ++i) {
                        //         std::memcpy(rgba, rgb, sizeof(unsigned char) * 3);
                        //         rgba += 4;
                        //         rgb += 3;
                        //     }
                        // }
                        // else {
                        //     buffer = data;
                        //     buffer_size = static_cast<u64>(width * height) * 4;
                        // }

                        // tex = Texture::load_texture(device, static_cast<u32>(width), static_cast<u32>(height), buffer, get_image_type(index));
                        // stbi_image_free(data);
                    }
                }, gltf_buffer.data);
            },
        }, image.data);
   
        textures[index] = std::pair{std::move(tex), index};
    };

    for (u32 i = 0; i < asset.images.size(); i++) {
        auto& image = asset.images[i];
        pool.push_task(process_image, image, i);
    }

    pool.wait_for_tasks();

    for(auto& tex : textures) {
        device.submit_commands({
            .command_lists = {std::move(tex.first.command_list)},
        });

        images[tex.second] = std::move(tex.first.texture);
    }

    device.wait_idle();

    null_texture = std::make_unique<Texture>(device, "assets/white.png", daxa::Format::R8G8B8A8_SRGB);

    std::vector<Material> materials = {};
    materials.reserve(asset.materials.size());

    for(auto& material : asset.materials) {
        Material mat = {};

        if(material.pbrData.baseColorTexture.has_value()) {
            u32 texture_index = static_cast<u32>(material.pbrData.baseColorTexture.value().textureIndex);
            u32 image_index = static_cast<u32>(asset.textures[texture_index].imageIndex.value());
            mat.albedo_image = images[image_index]->get_texture_id();
            mat.has_albedo_image = 1;
        } else {
            mat.albedo_image = null_texture->get_texture_id();
            mat.has_albedo_image = 0;
        }

        if(material.pbrData.metallicRoughnessTexture.has_value()) {
            u32 texture_index = static_cast<u32>(material.pbrData.metallicRoughnessTexture.value().textureIndex);
            u32 image_index = static_cast<u32>(asset.textures[texture_index].imageIndex.value());
            mat.metallic_roughness_image = images[image_index]->get_texture_id();
            mat.has_metallic_roughness_image = 1;
        } else {
            mat.metallic_roughness_image = null_texture->get_texture_id();
            mat.has_metallic_roughness_image = 0;
        }

        if(material.normalTexture.has_value()) {
            u32 texture_index = static_cast<u32>(material.normalTexture.value().textureIndex);
            u32 image_index = static_cast<u32>(asset.textures[texture_index].imageIndex.value());
            mat.normal_image = images[image_index]->get_texture_id();
            mat.has_normal_image = 1;
        } else {
            mat.normal_image = null_texture->get_texture_id();
            mat.has_normal_image = 0;
        }

        if(material.occlusionTexture.has_value()) {
            u32 texture_index = static_cast<u32>(material.occlusionTexture.value().textureIndex);
            u32 image_index = static_cast<u32>(asset.textures[texture_index].imageIndex.value());
            mat.occlusion_image = images[image_index]->get_texture_id();
            mat.has_occlusion_image = 1;
        } else {
            mat.occlusion_image = null_texture->get_texture_id();
            mat.has_occlusion_image = 0;
        }

        if(material.emissiveTexture.has_value()) {
            u32 texture_index = static_cast<u32>(material.emissiveTexture.value().textureIndex);
            u32 image_index = static_cast<u32>(asset.textures[texture_index].imageIndex.value());
            mat.emissive_image = images[image_index]->get_texture_id();
            mat.has_emissive_image = 1;
        } else {
            mat.emissive_image = null_texture->get_texture_id();
            mat.has_emissive_image = 0;
        }

        materials.push_back(mat);
    }

    {
        daxa::BufferId staging_material_buffer = device.create_buffer({
            .size = static_cast<u32>(materials.size() * sizeof(Material)),
            .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
        });

        material_buffer = device.create_buffer({
            .size = static_cast<u32>(materials.size() * sizeof(Material)),
            .allocate_info = daxa::MemoryFlagBits::DEDICATED_MEMORY,
        });

        auto cmd_list = device.create_command_list({
            .name = "cmd_list",
        });

        auto buffer_ptr = device.get_host_address_as<Material>(staging_material_buffer);
        std::memcpy(buffer_ptr, materials.data(), materials.size() * sizeof(Material));

        cmd_list.pipeline_barrier({
            .src_access = daxa::AccessConsts::HOST_WRITE,
            .dst_access = daxa::AccessConsts::TRANSFER_READ,
        });

        cmd_list.copy_buffer_to_buffer({
            .src_buffer = staging_material_buffer,
            .dst_buffer = material_buffer,
            .size = static_cast<u32>(materials.size() * sizeof(Material)),
        });

        cmd_list.complete();
        device.submit_commands({
            .command_lists = {std::move(cmd_list)}
        });

        device.wait_idle();
        device.destroy_buffer(staging_material_buffer);
    }

    std::vector<Vertex> vertices = {};
    std::vector<u32> indices = {};

    for (auto & scene : asset.scenes) {
        for (usize i = 0; i < scene.nodeIndices.size(); i++) {
            auto& node = asset.nodes[i];
            u32 vertex_offset = 0;
            u32 index_offset = 0;

            for (auto& primitive : asset.meshes[node.meshIndex.value()].primitives) {
                u32 vertex_count = 0;
                u32 index_count = 0;

                const f32* positionBuffer = nullptr;
                const f32* normalBuffer = nullptr;
                const f32* texCoordsBuffer = nullptr;
                const f32* tangentsBuffer = nullptr;

                if (primitive.findAttribute("POSITION") != primitive.attributes.end()) {
                    auto& accessor = asset.accessors[primitive.findAttribute("POSITION")->second];
                    auto& view = asset.bufferViews[accessor.bufferViewIndex.value()];
                    positionBuffer = reinterpret_cast<const f32*>(&(std::get<fastgltf::sources::Vector>(asset.buffers[view.bufferIndex].data).bytes[accessor.byteOffset + view.byteOffset]));
                    vertex_count = static_cast<u32>(accessor.count);
                }

                if (primitive.findAttribute("NORMAL") != primitive.attributes.end()) {
                    auto& accessor = asset.accessors[primitive.findAttribute("NORMAL")->second];
                    auto& view = asset.bufferViews[accessor.bufferViewIndex.value()];
                    normalBuffer = reinterpret_cast<const f32*>(&(std::get<fastgltf::sources::Vector>(asset.buffers[view.bufferIndex].data).bytes[accessor.byteOffset + view.byteOffset]));
                }

                if (primitive.findAttribute("TEXCOORD_0") != primitive.attributes.end()) {
                    auto& accessor = asset.accessors[primitive.findAttribute("TEXCOORD_0")->second];
                    auto& view = asset.bufferViews[accessor.bufferViewIndex.value()];
                    texCoordsBuffer = reinterpret_cast<const f32*>(&(std::get<fastgltf::sources::Vector>(asset.buffers[view.bufferIndex].data).bytes[accessor.byteOffset + view.byteOffset]));
                }

                if (primitive.findAttribute("TANGENT") != primitive.attributes.end()) {
                    auto& accessor = asset.accessors[primitive.findAttribute("TANGENT")->second];
                    auto& view = asset.bufferViews[accessor.bufferViewIndex.value()];
                    tangentsBuffer = reinterpret_cast<const f32*>(&(std::get<fastgltf::sources::Vector>(asset.buffers[view.bufferIndex].data).bytes[accessor.byteOffset + view.byteOffset]));
                }

                for (size_t v = 0; v < vertex_count; v++) {
                    glm::vec3 temp_position = glm::make_vec3(&positionBuffer[v * 3]);
                    glm::vec3 temp_normal = glm::make_vec3(&normalBuffer[v * 3]);
                    glm::vec2 temp_uv = texCoordsBuffer ? glm::make_vec2(&texCoordsBuffer[v * 2]) : glm::vec2(0.0f);
                    glm::vec4 temp_tangent = tangentsBuffer ? glm::make_vec4(&tangentsBuffer[v * 4]) : glm::vec4(0.0f);
                    Vertex vertex{
                        .position = {temp_position.x, temp_position.y, temp_position.z},
                        .normal = {temp_normal.x, temp_normal.y, temp_normal.z},
                        .uv = {temp_uv.x, temp_uv.y},
                        .tangent = {temp_tangent.x, temp_tangent.y, temp_tangent.z, temp_tangent.w}
                    };

                    vertices.push_back(vertex);
                }

                {
                    auto& accessor = asset.accessors[primitive.indicesAccessor.value()];
                    auto& bufferView = asset.bufferViews[accessor.bufferViewIndex.value()];
                    auto& buffer = asset.buffers[bufferView.bufferIndex];

                    index_count = static_cast<u32>(accessor.count);

                    switch(accessor.componentType) {
                        case fastgltf::ComponentType::UnsignedInt: {
                            const uint32_t * buf = reinterpret_cast<const uint32_t *>(&std::get<fastgltf::sources::Vector>(buffer.data).bytes[accessor.byteOffset + bufferView.byteOffset]);
                            indices.reserve((indices.size() + accessor.count) * sizeof(uint32_t));
                            for (size_t index = 0; index < accessor.count; index++) {
                                indices.push_back(buf[index]);
                            }
                            break;
                        }
                        case fastgltf::ComponentType::UnsignedShort: {
                            const uint16_t * buf = reinterpret_cast<const uint16_t *>(&std::get<fastgltf::sources::Vector>(buffer.data).bytes[accessor.byteOffset + bufferView.byteOffset]);
                            indices.reserve((indices.size() + accessor.count) * sizeof(uint16_t));
                            for (size_t index = 0; index < accessor.count; index++) {
                                indices.push_back(buf[index]);
                            }
                            break;
                        }
                        case fastgltf::ComponentType::UnsignedByte: {
                            const uint8_t * buf = reinterpret_cast<const uint8_t *>(&std::get<fastgltf::sources::Vector>(buffer.data).bytes[accessor.byteOffset + bufferView.byteOffset]);
                            indices.reserve((indices.size() + accessor.count) * sizeof(uint8_t));
                            for (size_t index = 0; index < accessor.count; index++) {
                                indices.push_back(buf[index]);
                            }
                            break;
                        }
                        default: { throw std::runtime_error("unhandled index buffer type"); }
                    }
                }

                Primitive temp_primitive {
                    .first_index = index_offset,
                    .first_vertex = vertex_offset,
                    .index_count = index_count,
                    .vertex_count = vertex_count,
                    .material_index = static_cast<u32>(primitive.materialIndex.value()) 
                };

                primitives.push_back(temp_primitive);

                vertex_offset += vertex_count;
                index_offset += index_count;
            }
        }
    }

    vertex_buffer = device.create_buffer(daxa::BufferInfo{
        .size = static_cast<u32>(sizeof(Vertex) * vertices.size()),
        .allocate_info = daxa::MemoryFlagBits::DEDICATED_MEMORY,
        .name = "vertex buffer",
    });

    index_buffer = device.create_buffer(daxa::BufferInfo{
        .size = static_cast<u32>(sizeof(u32) * indices.size()),
        .allocate_info = daxa::MemoryFlagBits::DEDICATED_MEMORY,
        .name = "index buffer",
    });

    {
        auto cmd_list = device.create_command_list({
            .name = "cmd_list",
        });

        auto vertex_staging_buffer = device.create_buffer({
            .size = static_cast<u32>(sizeof(Vertex) * vertices.size()),
            .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
            .name = "staging vertex buffer",
        });

        cmd_list.destroy_buffer_deferred(vertex_staging_buffer);

        auto index_staging_buffer = device.create_buffer({
            .size = static_cast<u32>(sizeof(u32) * indices.size()),
            .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
            .name = "staging index buffer",
        });
        
        cmd_list.destroy_buffer_deferred(index_staging_buffer);

        {
            auto buffer_ptr = device.get_host_address_as<Vertex>(vertex_staging_buffer);
            std::memcpy(buffer_ptr, vertices.data(), vertices.size() * sizeof(Vertex));
        }

        {
            auto buffer_ptr = device.get_host_address_as<Vertex>(index_staging_buffer);
            std::memcpy(buffer_ptr, indices.data(), indices.size() * sizeof(u32));
        }

        cmd_list.pipeline_barrier({
            .src_access = daxa::AccessConsts::HOST_WRITE,
            .dst_access = daxa::AccessConsts::TRANSFER_READ,
        });

        cmd_list.copy_buffer_to_buffer({
            .src_buffer = vertex_staging_buffer,
            .dst_buffer = vertex_buffer,
            .size = static_cast<u32>(sizeof(Vertex) * vertices.size()),
        });

        cmd_list.copy_buffer_to_buffer({
            .src_buffer = index_staging_buffer,
            .dst_buffer = index_buffer,
            .size = static_cast<u32>(sizeof(u32) * indices.size()),
        });

        cmd_list.pipeline_barrier({
            .src_access = daxa::AccessConsts::TRANSFER_WRITE,
            .dst_access = daxa::AccessConsts::VERTEX_SHADER_READ,
        });
        cmd_list.complete();
        device.submit_commands({
            .command_lists = {std::move(cmd_list)},
        });
    }
}

Model::~Model() {
    this->device.destroy_buffer(vertex_buffer);
    this->device.destroy_buffer(index_buffer);
    this->device.destroy_buffer(material_buffer);
}