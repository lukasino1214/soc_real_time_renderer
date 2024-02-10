#include "renderer.hpp"
#include <imgui_impl_glfw.h>
#include <implot.h>

#include "tasks/depth_prepass.inl"
#include "tasks/g_buffer_generation.inl"
#include "tasks/display_attachment.inl"
#include "tasks/composition.inl"
#include "tasks/bloom_downsample.inl"
#include "tasks/bloom_upsample.inl"
#include "tasks/ssao_generation.inl"
#include "tasks/ssao_blur.inl"
#include "tasks/draw_terrain.inl"
#include "tasks/height_to_normal.inl"
#include "tasks/sun_shadow_draw.inl"
#include "tasks/sun_shadow_draw_terrain.inl"
#include "tasks/cloud_rendering.inl"
#include "tasks/generate_min_hiz.inl"
#include "tasks/generate_max_hiz.inl"
#include "tasks/screen_space_reflection.inl"
#include "tasks/generate_luminance_histogram.inl"
#include "tasks/resolve_luminance_histogram.inl"
#include "tasks/tone_mapping.inl"
#include "tasks/depth_of_field.inl"
#include "tasks/temporal_antialiasing.inl"

Renderer::Renderer(AppWindow* _window, Context* _context, const std::shared_ptr<Scene>& scene) 
    : window{_window}, context{_context}, scene_hiearchy_panel{std::make_shared<SceneHiearchyPanel>(scene)} {
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui_ImplGlfw_InitForVulkan(window->glfw_handle, true);
    imgui_renderer =  daxa::ImGuiRenderer({
        .device = context->device,
        .format = context->swapchain.get_format(),
    });
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    sun_shadow_image = daxa::TaskImage{daxa::TaskImageInfo {
        .initial_images = {
            .images = std::array{
                context->device.create_image(daxa::ImageInfo {
                    .format = daxa::Format::D32_SFLOAT,
                    .size = {4096, 4096, 1},
                    .usage = daxa::ImageUsageFlagBits::DEPTH_STENCIL_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
                    .name = "shadow image"
                })
            }
        },
        .name = "sun shadow image"
    }};

    auto* block = &context->shader_global_block;
    block->globals.sun_info.shadow_image = sun_shadow_image.get_state().images[0].default_view();
    block->globals.sun_info.shadow_sampler = context->device.create_sampler(daxa::SamplerInfo {
        .magnification_filter = daxa::Filter::LINEAR,
        .minification_filter = daxa::Filter::LINEAR,
        .mipmap_filter = daxa::Filter::LINEAR,
        .address_mode_u = daxa::SamplerAddressMode::CLAMP_TO_BORDER,
        .address_mode_v = daxa::SamplerAddressMode::CLAMP_TO_BORDER,
        .address_mode_w = daxa::SamplerAddressMode::CLAMP_TO_BORDER,
        .mip_lod_bias = 0.0f,
        .enable_anisotropy = true,
        .max_anisotropy = 16.0f,
        .enable_compare = true,
        .compare_op = daxa::CompareOp::LESS,
        .min_lod = 0.0f,
        .max_lod = 1.0f,
        .border_color = daxa::BorderColor::FLOAT_OPAQUE_WHITE,
        .enable_unnormalized_coordinates = false,
    });

    context->shader_global_block.globals.terrain_offset = { 0.0f, 0.0f, 0.0f };
    context->shader_global_block.globals.terrain_scale = { 100.0f, 100.0f };
    context->shader_global_block.globals.terrain_height_scale = 70.0f;
    context->shader_global_block.globals.terrain_midpoint = 0.2f;
    context->shader_global_block.globals.terrain_delta = 8.0f;
    context->shader_global_block.globals.terrain_min_depth = 1.0f;
    context->shader_global_block.globals.terrain_max_depth = 100.0f;
    context->shader_global_block.globals.terrain_min_tess_level = 1;
    context->shader_global_block.globals.terrain_max_tess_level = 3;

    context->shader_global_block.globals.ssao_bias = 0.025f;
    context->shader_global_block.globals.ssao_radius = 0.3f;
    context->shader_global_block.globals.ssao_kernel_size = 26;

    context->shader_global_block.globals.ambient = { 0.1f, 0.1f, 0.1f };
    context->shader_global_block.globals.ambient_occlussion_strength = 1.2f;
    context->shader_global_block.globals.emissive_bloom_strength = 2.0f;

    context->shader_global_block.globals.focal_length =  5.0f;
    context->shader_global_block.globals.plane_in_focus =  1.0f;
    context->shader_global_block.globals.aperture =  8.0f;

    context->shader_global_block.globals.adjustment_speed = 1.0f;
    context->shader_global_block.globals.log_min_luminance = -15.0f;
    context->shader_global_block.globals.log_max_luminance = 15.0f;
    context->shader_global_block.globals.target_luminance = 0.2140f;
    context->shader_global_block.globals.elapsed_time = 0.0f;

    context->shader_global_block.globals.log_min_luminance = std::log2(context->shader_global_block.globals.target_luminance / std::exp2(context->shader_global_block.globals.log_min_luminance));
    context->shader_global_block.globals.log_max_luminance = std::log2(context->shader_global_block.globals.target_luminance / std::exp2(context->shader_global_block.globals.log_max_luminance));

    context->shader_global_block.globals.saturation = 1.0f;
    context->shader_global_block.globals.agxDs_linear_section = 0.18f;
    context->shader_global_block.globals.peak = 1.0f;
    context->shader_global_block.globals.compression = 0.15f;
    context->shader_global_block.globals.frame_counter = 0;

    glm::vec3 light_position(-3.2f, 40.0f, -4.0f);
    f32 planes = 16.0f;
    glm::mat4 light_projection = glm::ortho(-planes, planes, -planes, planes, -planes, planes);

    glm::vec3 dir = { 0.0f, -1.0f, 0.0f };
    dir = glm::rotateX(dir, glm::radians(angle_direction.x));
    dir = glm::rotateY(dir, glm::radians(angle_direction.y));
    dir = glm::rotateZ(dir, glm::radians(angle_direction.z));
    //dir = glm::normalize(dir);

    glm::mat4 light_view = glm::lookAt(light_position, light_position + dir, glm::vec3(0.0, -1.0, 0.0));

    glm::mat4 projection_view_matrix = light_projection * light_view;
    glm::vec4 terrain_y_clip_trick = projection_view_matrix * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f};

    block->globals.sun_info.position = *reinterpret_cast<f32vec3*>(&light_position);
    block->globals.sun_info.direction = *reinterpret_cast<f32vec3*>(&dir);
    block->globals.sun_info.projection_matrix = *reinterpret_cast<f32mat4x4*>(&light_projection);
    block->globals.sun_info.view_matrix = *reinterpret_cast<f32mat4x4*>(&light_view);
    block->globals.sun_info.projection_view_matrix = *reinterpret_cast<f32mat4x4*>(&projection_view_matrix);
    block->globals.sun_info.terrain_y_clip_trick = *reinterpret_cast<f32vec4*>(&terrain_y_clip_trick);
    block->globals.sun_info.exponential_factor = -80.0f;
    block->globals.sun_info.darkening_factor = 1.0f;
    block->globals.sun_info.bias = 0.0001f;
    block->globals.sun_info.intensity = 1.0f;

    block->globals.resolution = { static_cast<i32>(window->get_width()), static_cast<i32>(window->get_height()) };

    context->frame_index = context->swapchain.get_cpu_timeline_value() % (context->swapchain.info().max_allowed_frames_in_flight);

    char* mapped_ptr = reinterpret_cast<char*>(context->device.get_host_address(context->shader_globals_buffer));
    auto* ptr = reinterpret_cast<ShaderGlobalsBlock*>(mapped_ptr + ((static_cast<i32>(sizeof(ShaderGlobalsBlock)) + 256 - 1) / 256) * 256 * context->frame_index);
    *ptr = this->context->shader_global_block;

    this->context->shader_globals_set_info = {
        .slot = SHADER_GLOBALS_SLOT,
        .buffer = this->context->shader_globals_buffer,
        .size = ((static_cast<i32>(sizeof(ShaderGlobalsBlock)) + 256 - 1) / 256) * 256,
        .offset = ((static_cast<i32>(sizeof(ShaderGlobalsBlock)) + 256 - 1) / 256) * 256 * context->frame_index,
    };

    compile_pipelines();

    noise_texture = std::make_unique<Texture>(context->device, "assets/Clouds/noise.png", daxa::Format::R8G8B8A8_UNORM, daxa::ImageUsageFlagBits::SHADER_STORAGE);

    {
        terrain_heightmap = std::make_unique<Texture>(context->device, "assets/Terrain/heightmap.exr", daxa::Format::R8G8B8A8_UNORM, daxa::ImageUsageFlagBits::SHADER_STORAGE);
        terrain_albedomap = std::make_unique<Texture>(context->device, "assets/Terrain/albedo.exr", daxa::Format::R8G8B8A8_SRGB);
        
        daxa::TaskImage terrain_heightmap_image = daxa::TaskImage{daxa::TaskImageInfo{ .initial_images = { .images = std::array{terrain_heightmap->image_id}}, .name = "terrain heightmap"}};
        terrain_normalmap_task = daxa::TaskImage{daxa::TaskImageInfo{ 
            .initial_images = {
                .images = std::array{
                    context->device.create_image(daxa::ImageInfo {
                        .format = daxa::Format::R16G16B16A16_SFLOAT,
                        .size = { terrain_heightmap->image_dimension.x, terrain_heightmap->image_dimension.y, 1 },
                        .usage = daxa::ImageUsageFlagBits::SHADER_STORAGE | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
                        .name = "terrain normalmap"
                    })
                }
            }, 
            .name = "terrain normalmap"
        }};

        daxa::TaskGraph convert_task_graph = daxa::TaskGraph{daxa::TaskGraphInfo {
            .device = context->device,
            .name = "convert heightmap to normalmap"
        }};

        convert_task_graph.use_persistent_image(terrain_heightmap_image);
        convert_task_graph.use_persistent_image(terrain_normalmap_task);

        convert_task_graph.add_task(HeightToNormalTask {
            .uses = {
                .u_normal_target = terrain_normalmap_task,
                .u_heightmap = terrain_heightmap_image
            },
            .context = context,
            .image_dimension = terrain_heightmap->image_dimension,
        });

        convert_task_graph.submit({});
        convert_task_graph.complete({});
        convert_task_graph.execute({});

        std::vector<f32vec2> vertices = {};
        std::vector<u32> indices = {};

        u32 terrain_size = 100u;

        for(u32 i = 0; i < terrain_size; i++) {
            for(u32 j = 0; j < terrain_size; j++) {
                vertices.push_back(f32vec2{
                    .x = f32(i) / f32(terrain_size - 1), 
                    .y = f32(j) / f32(terrain_size - 1)
                });
            }
        }

        for(u32 i = 0; i < terrain_size - 1; i++) {
            for(u32 j = 0; j < terrain_size - 1; j++) {
                u32 i0 = j + i * terrain_size;
                u32 i1 = i0 + 1;
                u32 i2 = i0 + terrain_size;
                u32 i3 = i2 + 1;

                indices.push_back(i0);
                indices.push_back(i1);
                indices.push_back(i2);
                indices.push_back(i3);
            }
        }

        u32 vertices_bytesize = static_cast<u32>(vertices.size() * sizeof(f32vec2));
        u32 indices_bytesize = static_cast<u32>(indices.size() * sizeof(u32));
        terrain_index_size = static_cast<u32>(indices.size());

        terrain_vertices = daxa::TaskBuffer{daxa::TaskBufferInfo{ 
                .initial_buffers = {
                    .buffers = std::array{
                        context->device.create_buffer(daxa::BufferInfo {
                            .size = vertices_bytesize,
                            .allocate_info = daxa::AutoAllocInfo{daxa::MemoryFlagBits::DEDICATED_MEMORY},
                            .name = "terrain vertices"
                        })
                    }
                },
                .name = "terrain vertices" 
            }
        };

        terrain_indices = daxa::TaskBuffer{daxa::TaskBufferInfo{ 
                .initial_buffers = {
                    .buffers = std::array{
                        context->device.create_buffer(daxa::BufferInfo {
                            .size = indices_bytesize,
                            .allocate_info = daxa::AutoAllocInfo{daxa::MemoryFlagBits::DEDICATED_MEMORY},
                            .name = "terrain indices"
                        })
                    }
                },
                .name = "terrain indices" 
            }
        };

        buffers.push_back(terrain_vertices);
        buffers.push_back(terrain_indices);

        auto cmd = context->device.create_command_list({});

        auto v_buf = context->device.create_buffer({
            .size = vertices_bytesize,
            .allocate_info = daxa::AutoAllocInfo{daxa::MemoryFlagBits::HOST_ACCESS_RANDOM}
        });

        cmd.destroy_buffer_deferred(v_buf);

        auto i_buf = context->device.create_buffer({
            .size = indices_bytesize,
            .allocate_info = daxa::AutoAllocInfo{daxa::MemoryFlagBits::HOST_ACCESS_RANDOM}
        });

        cmd.destroy_buffer_deferred(i_buf);

        std::memcpy(context->device.get_host_address(v_buf), vertices.data(), vertices_bytesize);
        std::memcpy(context->device.get_host_address(i_buf), indices.data(), indices_bytesize);

        cmd.copy_buffer_to_buffer(daxa::BufferCopyInfo {
            .src_buffer = v_buf,
            .dst_buffer = terrain_vertices.get_state().buffers[0],
            .size = vertices_bytesize
        });

        cmd.copy_buffer_to_buffer(daxa::BufferCopyInfo {
            .src_buffer = i_buf,
            .dst_buffer = terrain_indices.get_state().buffers[0],
            .size = indices_bytesize
        });

        cmd.complete();
        context->device.submit_commands({ .command_lists = {cmd}});
    }

    auto_exposure_buffer = daxa::TaskBuffer{daxa::TaskBufferInfo{ 
            .initial_buffers = {
                .buffers = std::array{
                    context->device.create_buffer(daxa::BufferInfo {
                        .size = sizeof(AutoExposure),
                        .allocate_info = daxa::AutoAllocInfo{daxa::MemoryFlagBits::DEDICATED_MEMORY},
                        .name = "auto exposure"
                    })
                }
            },
            .name = "auto exposure" 
        }
    };

    context->shader_global_block.globals.auto_exposure_buffer_ptr = context->device.get_device_address(auto_exposure_buffer.get_state().buffers[0]);

    buffers.push_back(auto_exposure_buffer);

    color_image = daxa::TaskImage{{ .name = "color image" }};
    albedo_image = daxa::TaskImage{{ .name = "albedo image" }};
    emissive_image = daxa::TaskImage{{ .name = "emissive image" }};
    normal_image = daxa::TaskImage{{ .name = "normal image" }};
    metallic_roughness_image = daxa::TaskImage{{ .name = "metallic roughness" }};
    depth_image = daxa::TaskImage{{ .name = "depth image" }};
    velocity_image = daxa::TaskImage{{ .name = "velocity image" }};
    previous_color_image = daxa::TaskImage{{ .name = "previous color image" }};
    previous_velocity_image = daxa::TaskImage{{ .name = "previous velocity image" }};
    resolved_image = daxa::TaskImage{{ .name = "resolved image" }};
    ssao_image = daxa::TaskImage{{ .name = "ssao image" }};
    ssao_blur_image = daxa::TaskImage{{ .name = "ssao blur image" }};
    clouds_image = daxa::TaskImage{{ .name = "clouds image" }};
    ssr_image = daxa::TaskImage{{ .name = "ssr image" }};
    depth_of_field_image = daxa::TaskImage{{ .name = "depth of field image" }};

    images = {
        color_image,
        albedo_image,
        emissive_image,
        normal_image,
        metallic_roughness_image,
        depth_image,
        velocity_image,
        previous_color_image,
        previous_velocity_image,
        resolved_image,
        ssao_image,
        ssao_blur_image,
        terrain_normalmap_task,
        sun_shadow_image,
        clouds_image,
        ssr_image,
        depth_of_field_image
    };

    depth_of_field_mips = static_cast<u32>(std::floor(std::log2(std::max(window->get_width(), window->get_height())))) + 1;

    frame_buffer_images = {
        {
            {
                .format = daxa::Format::R16G16B16A16_SFLOAT,
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED | daxa::ImageUsageFlagBits::TRANSFER_SRC,
                .name = color_image.info().name,
            },
            color_image,
        },
        {
            {
                .format = daxa::Format::R16G16B16A16_SFLOAT,
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
                .name = albedo_image.info().name,
            },
            albedo_image,
        },
        {
            {
                .format = daxa::Format::R16G16B16A16_SFLOAT,
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
                .name = emissive_image.info().name,
            },
            emissive_image,
        },
        {
            {
                .format = daxa::Format::R16G16B16A16_SFLOAT,
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
                .name = normal_image.info().name,
            },
            normal_image,
        },
        {
            {
                .format = daxa::Format::R16G16B16A16_SFLOAT,
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
                .name = metallic_roughness_image.info().name,
            },
            metallic_roughness_image,
        },
        {
            {
                .format = daxa::Format::D32_SFLOAT,
                .usage = daxa::ImageUsageFlagBits::DEPTH_STENCIL_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED | daxa::ImageUsageFlagBits::TRANSFER_SRC,
                .name = depth_image.info().name,
            },
            depth_image,
        },
        {
            {
                .format = daxa::Format::R16G16B16A16_SFLOAT,
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED | daxa::ImageUsageFlagBits::TRANSFER_SRC,
                .name = velocity_image.info().name,
            },
            velocity_image,
        },
        {
            {
                .format = daxa::Format::R16G16B16A16_SFLOAT,
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED | daxa::ImageUsageFlagBits::TRANSFER_DST,
                .name = previous_color_image.info().name,
            },
            previous_color_image,
        },
        {
            {
                .format = daxa::Format::R16G16B16A16_SFLOAT,
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED | daxa::ImageUsageFlagBits::TRANSFER_DST,
                .name = previous_velocity_image.info().name,
            },
            previous_velocity_image,
        },
        {
            {
                .format = daxa::Format::R16G16B16A16_SFLOAT,
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED | daxa::ImageUsageFlagBits::TRANSFER_SRC,
                .name = resolved_image.info().name,
            },
            resolved_image,
        },
        {
            {
                .format = daxa::Format::R8_UNORM,
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
                .name = ssao_image.info().name,
            },
            ssao_image,
        },
        {
            {
                .format = daxa::Format::R8_UNORM,
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
                .name = ssao_blur_image.info().name,
            },
            ssao_blur_image,
        },
        {
            {
                .format = daxa::Format::R8G8B8A8_UNORM,
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED | daxa::ImageUsageFlagBits::SHADER_STORAGE,
                .name = clouds_image.info().name,
            },
            clouds_image,
        },
        {
            {
                .format = daxa::Format::R16G16B16A16_SFLOAT,
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED | daxa::ImageUsageFlagBits::SHADER_STORAGE,
                .name = ssr_image.info().name,
            },
            ssr_image,
        },
        {
            {
                .format = daxa::Format::R16G16B16A16_SFLOAT,
                .mip_level_count = depth_of_field_mips,
                .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED | daxa::ImageUsageFlagBits::TRANSFER_SRC | daxa::ImageUsageFlagBits::TRANSFER_DST,
                .name = depth_of_field_image.info().name,
            },
            depth_of_field_image,
        },
    };

    context->shader_global_block.globals.depth_of_field_sampler = context->device.create_sampler(daxa::SamplerInfo {
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
        .max_lod = static_cast<f32>(depth_of_field_mips),
        .enable_unnormalized_coordinates = false,
        .name = "depth of field sampler"
    });

    swapchain_image = daxa::TaskImage{{.swapchain_image = true, .name = "swapchain image"}};

    glm::uvec2 mip_size = { window->get_width(), window->get_height() };
    for(usize i = 0; i < mip_chain_length; i++) {
        daxa::TaskImage mip = daxa::TaskImage{ daxa::TaskImageInfo {
                .name = "bloom mip chain " + std::to_string(i)
            }
        };

        mip.set_images({ 
            .images = std::array{
                context->device.create_image({
                    .format = daxa::Format::R16G16B16A16_SFLOAT,
                    .size = { mip_size.x, mip_size.y, 1 },
                    .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
                    .name = "bloom mip chain " + std::to_string(i),
                }),
            }
        });

        images.push_back(mip);
        bloom_mip_chain.push_back(mip);
        mip_size /= 2;
    }

    recreate_framebuffer();

    {
        std::vector<std::string> name_tasks = {
            std::string{DepthPrepassTask::NAME},
            std::string{SunShadowDrawTask::NAME},
            std::string{GBufferGenerationTask::NAME},
            std::string{DrawTerrainTask::NAME},
            std::string{SSAOGenerationTask::NAME},
            std::string{SSAOBlurTask::NAME},
            std::string{CompositionTask::NAME},
            std::string{SunShadowDrawTerrainTask::NAME},
            std::string{CloudRenderingTask::NAME},
            std::string{GenerateMinHIZTask::NAME},
            std::string{GenerateMaxHIZTask::NAME},
            std::string{ScreenSpaceReflectionTask::NAME},
            std::string{GenerateLuminanceHistogramTask::NAME},
            std::string{ResolveLuminanceHistogramTask::NAME},
            std::string{ToneMappingTask::NAME},
            std::string{DepthOfFieldTask::NAME},
            std::string{BlitImageToImageTask::NAME},
            std::string{TemporalAntiAliasingTask::NAME},
            std::string{CopyImageTask::NAME} + " - velocity",
            std::string{CopyImageTask::NAME} + " - color"
        };

        for(u32 i = 0; i < mip_chain_length; i++) {
            name_tasks.push_back(std::string{BloomUpsampleTask::NAME} + " - " + std::to_string(i));
            names[name_tasks.back()] = "Bloom";
            name_tasks.push_back(std::string{BloomDownsampleTask::NAME} + " - " + std::to_string(i));
            names[name_tasks.back()] = "Bloom";
        }

        for(u32 i = 0; i < depth_of_field_mips; i++) {
            name_tasks.push_back(std::string{MipMappingTask::NAME} + " - " + std::to_string(i));
            names[name_tasks.back()] = "Depth Of Field";
        }

        for(const auto& name : name_tasks) {
            context->gpu_metrics[name] = std::make_shared<GPUMetric>(context->gpu_metric_pool.get());
        }
    }

    names[std::string{DepthPrepassTask::NAME}] = "Depth Prepass";
    names[std::string{CompositionTask::NAME}] = "Composition";
    names[std::string{ToneMappingTask::NAME}] = "Tone Mapping";
    names[std::string{BlitImageToImageTask::NAME}] = "Depth Of Field";
    names[std::string{DepthOfFieldTask::NAME}] = "Depth Of Field";
    names[std::string{SunShadowDrawTask::NAME}] = "Shadows";
    names[std::string{SunShadowDrawTerrainTask::NAME}] = "Shadows";
    names[std::string{DrawTerrainTask::NAME}] = "Rendering G-Buffer";
    names[std::string{GBufferGenerationTask::NAME}] = "Rendering G-Buffer";
    names[std::string{ScreenSpaceReflectionTask::NAME}] = "Screen Space Reflections";
    names[std::string{SSAOGenerationTask::NAME}] = "Ambient Occlusion";
    names[std::string{SSAOBlurTask::NAME}] = "Ambient Occlusion";
    names[std::string{GenerateLuminanceHistogramTask::NAME}] = "Auto Exposure";
    names[std::string{ResolveLuminanceHistogramTask::NAME}] = "Auto Exposure";
    names[std::string{CloudRenderingTask::NAME}] = "Sky Rendering";
    names[std::string{TemporalAntiAliasingTask::NAME}] = "Temporal Anti-Aliasing";
    names[std::string{CopyImageTask::NAME} + " - velocity"] = "Temporal Anti-Aliasing";
    names[std::string{CopyImageTask::NAME} + " - color"] = "Temporal Anti-Aliasing";

    metrics[names[std::string{DepthPrepassTask::NAME}]] = {};
    metrics[names[std::string{CompositionTask::NAME}]] = {};
    metrics[names[std::string{ToneMappingTask::NAME}]] = {};
    metrics["Bloom"] = {};
    metrics["Depth Of Field"] = {};
    metrics["Shadows"] = {};
    metrics["Rendering G-Buffer"] = {};
    metrics["Screen Space Reflections"] = {};
    metrics["Ambient Occlusion"] = {};
    metrics["Auto Exposure"] = {};
    metrics["Sky Rendering"] = {};
    metrics["Temporal Anti-Aliasing"] = {};

    rebuild_task_graph();

    context->shader_global_block.globals.nearest_sampler = context->device.create_sampler(daxa::SamplerInfo {
        .magnification_filter = daxa::Filter::NEAREST,
        .minification_filter = daxa::Filter::NEAREST,
        .mipmap_filter = daxa::Filter::NEAREST,
        .name = "nearest sampler",
    });

    context->shader_global_block.globals.linear_sampler = context->device.create_sampler(daxa::SamplerInfo {
        .magnification_filter = daxa::Filter::LINEAR,
        .minification_filter = daxa::Filter::LINEAR,
        .mipmap_filter = daxa::Filter::LINEAR,
        .name = "linear sampler",
    });
}

Renderer::~Renderer() {
    for(auto& task_image : images) {
        for(auto image : task_image.get_state().images) {
            context->device.destroy_image(image);
        }
    }

    for (auto& task_buffer : buffers) {
        for (auto buffer : task_buffer.get_state().buffers) {
            this->context->device.destroy_buffer(buffer);
        }
    }

    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    context->device.destroy_sampler(context->shader_global_block.globals.nearest_sampler);
    context->device.destroy_sampler(context->shader_global_block.globals.linear_sampler);
    context->device.destroy_sampler(context->shader_global_block.globals.sun_info.shadow_sampler);
    context->device.destroy_sampler(context->shader_global_block.globals.depth_of_field_sampler);

    this->context->device.wait_idle();
    this->context->device.collect_garbage();
}

void Renderer::render() {
    auto reloaded_result = context->pipeline_manager.reload_all();
    if (auto reload_err = std::get_if<daxa::PipelineReloadError>(&reloaded_result)) {
        std::cout << "Failed to reload " << reload_err->message << '\n';
    }
    if (std::get_if<daxa::PipelineReloadSuccess>(&reloaded_result)) {
        std::cout << "Successfully reloaded!\n";
    }

    auto image = context->swapchain.acquire_next_image();
    if(image.is_empty()) { return; }
    swapchain_image.set_images({.images = std::span{&image, 1}});

    context->frame_index = (context->swapchain.get_cpu_timeline_value()) % (context->swapchain.info().max_allowed_frames_in_flight);

    u8* mapped_ptr = reinterpret_cast<u8*>(context->device.get_host_address(context->shader_globals_buffer));
    auto* ptr = reinterpret_cast<ShaderGlobalsBlock*>(mapped_ptr + ((static_cast<i32>(sizeof(ShaderGlobalsBlock)) + 256 - 1) / 256) * 256 * context->frame_index);
    *ptr = this->context->shader_global_block;

    this->context->shader_globals_set_info = {
        .slot = SHADER_GLOBALS_SLOT,
        .buffer = this->context->shader_globals_buffer,
        .size = ((static_cast<i32>(sizeof(ShaderGlobalsBlock)) + 256 - 1) / 256) * 256,
        .offset = ((static_cast<i32>(sizeof(ShaderGlobalsBlock)) + 256 - 1) / 256) * 256 * context->frame_index,
    };

    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    scene_hiearchy_panel->draw();
    
    auto* globals = &context->shader_global_block.globals;

    auto settings_ui = [&](std::string_view name, auto fn) {
        const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_FramePadding;

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4, 4 });

        bool open = ImGui::TreeNodeEx(reinterpret_cast<const void*>(name.data()), treeNodeFlags, "%s", name.data());
        ImGui::PopStyleVar();

        if(open) {
            GUI::begin_properties();
            fn();
            GUI::end_properties();
            GUI::indent(0.0f, GImGui->Style.ItemSpacing.y);
            ImGui::TreePop();
        }
    };

    ImGui::Begin("test");
    settings_ui("terrain settings", [&](){
        GUI::vec3_property("offset", *reinterpret_cast<glm::vec3*>(&globals->terrain_offset), nullptr);
        GUI::vec2_property("scale", *reinterpret_cast<glm::vec2*>(&globals->terrain_scale), nullptr);
        GUI::f32_property("height scale", globals->terrain_height_scale);
        GUI::f32_property("midpoint", globals->terrain_midpoint);
        GUI::f32_property("delta", globals->terrain_delta);
        GUI::f32_property("min depth", globals->terrain_min_depth);
        GUI::f32_property("max depth", globals->terrain_max_depth);
        GUI::i32_property("min tessalation level", globals->terrain_min_tess_level);
        GUI::i32_property("max tessalation level", globals->terrain_max_tess_level);
    });

    settings_ui("sun settings", [&](){
        GUI::f32_property("exponential factor", globals->sun_info.exponential_factor);
        GUI::f32_property("darkening factor", globals->sun_info.darkening_factor);
        GUI::f32_property("shadow bias", globals->sun_info.bias);
        GUI::f32_property("intensity", globals->sun_info.intensity);
        bool changed = GUI::vec3_property("position", *reinterpret_cast<glm::vec3*>(&globals->sun_info.position), nullptr);
        changed |= GUI::vec3_property("direction", angle_direction, nullptr);

        glm::vec3 dir = { 0.0f, -1.0f, 0.0f };
        dir = glm::rotateX(dir, glm::radians(angle_direction.x));
        dir = glm::rotateY(dir, glm::radians(angle_direction.y));
        dir = glm::rotateZ(dir, glm::radians(angle_direction.z));

        GUI::vec3_property("normalized direction", dir, nullptr);

        if(changed) {
            glm::vec3 position = *reinterpret_cast<glm::vec3*>(&globals->sun_info.position);
            glm::mat4 view = glm::lookAt(position, position + dir, glm::vec3(0.0, -1.0, 0.0));
            globals->sun_info.view_matrix = *reinterpret_cast<f32mat4x4*>(&view);

            glm::mat4 projection_matrix = *reinterpret_cast<glm::mat4*>(&globals->sun_info.projection_matrix);
            glm::vec4 terrain_y_clip_trick = projection_matrix * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f};
            glm::mat4 projection_view_matrix = projection_matrix * view;

            globals->sun_info.projection_view_matrix = *reinterpret_cast<f32mat4x4*>(&projection_view_matrix);
            globals->sun_info.terrain_y_clip_trick = *reinterpret_cast<f32vec4*>(&terrain_y_clip_trick);
            globals->sun_info.direction = *reinterpret_cast<f32vec3*>(&dir);
        }
    });

    settings_ui("ssao settings", [&](){
        GUI::f32_property("bias", globals->ssao_bias);
        GUI::f32_property("radius", globals->ssao_radius);
        GUI::i32_property("kernel size", globals->ssao_kernel_size);
    });

    settings_ui("composition settings", [&](){
        GUI::vec3_property("ambient", *reinterpret_cast<glm::vec3*>(&globals->ambient), nullptr);
        GUI::f32_property("ambient oclussion strength", globals->ambient_occlussion_strength);
        GUI::f32_property("emissive strength", globals->emissive_bloom_strength);
    });

    settings_ui("depth of field settings", [&](){
        GUI::f32_property("focal length", globals->focal_length);
        GUI::f32_property("plane in focus", globals->plane_in_focus);
        GUI::f32_property("aperture", globals->aperture);
    });

    settings_ui("auto exposure settings", [&](){
        f32 min_luminance = static_cast<f32>(std::log2(std::pow(std::exp2(globals->log_min_luminance), -1.0) * static_cast<f64>(globals->target_luminance)));
        f32 max_luminance = static_cast<f32>(std::log2(std::pow(std::exp2(globals->log_max_luminance), -1.0) * static_cast<f64>(globals->target_luminance)));

        bool changed = false;
        GUI::f32_property("adjustment speed", globals->adjustment_speed);
        changed |= GUI::f32_property("min luminance", min_luminance);
        changed |= GUI::f32_property("max luminance", max_luminance);
        changed |= GUI::f32_property("target luminance", globals->target_luminance);

        if(changed) {
            globals->log_min_luminance = std::log2(globals->target_luminance / std::exp2(min_luminance));
            globals->log_max_luminance = std::log2(globals->target_luminance / std::exp2(max_luminance));
        }
    });

    settings_ui("tone mapping settings", [&](){
        GUI::f32_property("saturation", globals->saturation);
        GUI::f32_property("agx ds linear section", globals->agxDs_linear_section);
        GUI::f32_property("peak", globals->peak);
        GUI::f32_property("compression", globals->compression);
    });

    ImGui::End();

    ImGui::Begin("GPU Metric");
    f64 total_time = 0.0;
    for(auto& [key, metric] : context->gpu_metrics) {
        total_time += metric->time_elapsed;
        ImGui::Text("%s : %f ms", key.data(), metric->time_elapsed);
    }

    ImGui::Separator();
    ImGui::Text("Total GPU time : %f ms", total_time);
    ImGui::Separator();
    accumulated_time.push(globals->elapsed_time);
    if (ImPlot::BeginPlot("GPU Metric", ImVec2(-1, 250))) {
        ImPlot::SetupAxes(nullptr, nullptr, 0, ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit);
        ImPlot::SetupAxisFormat(ImAxis_X1, "%g s");
        ImPlot::SetupAxisFormat(ImAxis_Y1, "%g ms");
        ImPlot::SetupAxisLimits(ImAxis_X1, globals->elapsed_time - 5.0, globals->elapsed_time, ImGuiCond_Always);
        ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Outside);

        for(auto& [key, value] : metrics) {
            value.push(0.0f);
        }

        for(auto& [key_metric, value_metric] : context->gpu_metrics) {
            if(auto name = names.find(key_metric); name != names.end()) {
                if(auto stat = metrics.find(name->second); stat != metrics.end()) {
                    stat->second.get_current() += static_cast<f32>(value_metric->time_elapsed);
                }
            }
        }

        for(auto& [key, value] : metrics) {
            ImPlot::SetNextFillStyle(IMPLOT_AUTO_COL, 1.0f);
            ImPlot::PlotLine(key.c_str(), accumulated_time.data.get(), value.data.get(), static_cast<i32>(value.size), 0, static_cast<i32>(value.offset), sizeof(f32));
        }

        ImPlot::EndPlot();
    }
    ImGui::End();

    ImGui::Render();

    render_task_graph.execute({});
    context->device.wait_idle();
}

void Renderer::window_resized() {
    context->swapchain.resize();

    recreate_framebuffer();
}

void Renderer::recreate_framebuffer() {
    for (auto &[info, timg] : frame_buffer_images) {
        if (!timg.get_state().images.empty() && !timg.get_state().images[0].is_empty()) {
            context->device.destroy_image(timg.get_state().images[0]);
        }

        auto new_info = info;
        if(info.name.substr(0, 4) == "ssao") {
            new_info.size = {this->window->get_width() / 2, this->window->get_height() / 2, 1};
        } else if(info.name.substr(0, 5) == "clouds") {
            new_info.size = {this->window->get_width() / 2, this->window->get_height() / 2, 1};
        } else {
            new_info.size = {this->window->get_width(), this->window->get_height(), 1};
        }

        if(info.name == "depth of field image") {
            depth_of_field_mips = static_cast<u32>(std::floor(std::log2(std::max(window->get_width(), window->get_height())))) + 1;
            new_info.mip_level_count = depth_of_field_mips;

            context->device.destroy_sampler(context->shader_global_block.globals.depth_of_field_sampler);
            context->shader_global_block.globals.depth_of_field_sampler = context->device.create_sampler(daxa::SamplerInfo {
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
                .max_lod = static_cast<f32>(depth_of_field_mips),
                .enable_unnormalized_coordinates = false,
                .name = "depth of field sampler"
            });
        }

        timg.set_images({.images = std::array{this->context->device.create_image(new_info)}});
    }

    glm::uvec2 mip_size = { window->get_width(), window->get_height() };
    for(usize i = 0; i < mip_chain_length; i++) {
        if (!bloom_mip_chain[i].get_state().images.empty() && !bloom_mip_chain[i].get_state().images[0].is_empty()) {
            context->device.destroy_image(bloom_mip_chain[i].get_state().images[0]);
        }

        bloom_mip_chain[i].set_images({
            .images = std::array{
                context->device.create_image({
                    .format = daxa::Format::R16G16B16A16_SFLOAT,
                    .size = { mip_size.x, mip_size.y, 1 },
                    .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::SHADER_SAMPLED,
                    .name = "bloom mip chain " + std::to_string(i),
                }),
            }
        });

        mip_size /= 2;
    }
}

void Renderer::compile_pipelines() {
    std::vector<std::tuple<std::string_view, daxa::RasterPipelineCompileInfo>> rasters = {
        {DisplayAttachmentTask::NAME, DisplayAttachmentTask::PIPELINE_COMPILE_INFO},
        {DepthPrepassTask::NAME, DepthPrepassTask::PIPELINE_COMPILE_INFO},
        {SunShadowDrawTask::NAME, SunShadowDrawTask::PIPELINE_COMPILE_INFO},
        {SunShadowDrawTerrainTask::NAME, SunShadowDrawTerrainTask::PIPELINE_COMPILE_INFO},
        {GBufferGenerationTask::NAME, GBufferGenerationTask::PIPELINE_COMPILE_INFO},
        {DrawTerrainTask::NAME , DrawTerrainTask::PIPELINE_COMPILE_INFO},
        {BloomDownsampleTask::NAME, BloomDownsampleTask::PIPELINE_COMPILE_INFO},
        {BloomUpsampleTask::NAME, BloomUpsampleTask::PIPELINE_COMPILE_INFO},
        {SSAOGenerationTask::NAME, SSAOGenerationTask::PIPELINE_COMPILE_INFO},
        {SSAOBlurTask::NAME, SSAOBlurTask::PIPELINE_COMPILE_INFO},
        {CompositionTask::NAME, CompositionTask::PIPELINE_COMPILE_INFO},
        {ScreenSpaceReflectionTask::NAME, ScreenSpaceReflectionTask::PIPELINE_COMPILE_INFO},
        {ToneMappingTask::NAME, ToneMappingTask::PIPELINE_COMPILE_INFO},
        {DepthOfFieldTask::NAME, DepthOfFieldTask::PIPELINE_COMPILE_INFO},
        {TemporalAntiAliasingTask::NAME, TemporalAntiAliasingTask::PIPELINE_COMPILE_INFO},
    };

    for (auto [name, info] : rasters) {
        if(name == DisplayAttachmentTask::NAME) { info.color_attachments = {{ .format = context->swapchain.get_format() }}; }
        if(name == ToneMappingTask::NAME) { info.color_attachments = {{ .format = context->swapchain.get_format() }}; }
        //if(name == CompositionTask::NAME) { info.color_attachments = {{ .format = context->swapchain.get_format() }}; }

        auto compilation_result = this->context->pipeline_manager.add_raster_pipeline(info);
        std::cout << std::string{name} + " " << compilation_result.to_string() << std::endl;
        this->context->raster_pipelines[name] = compilation_result.value();
    }

    std::vector<std::tuple<std::string_view, daxa::ComputePipelineCompileInfo>> computes = {
        {HeightToNormalTask::NAME, HeightToNormalTask::PIPELINE_COMPILE_INFO},
        {CloudRenderingTask::NAME, CloudRenderingTask::PIPELINE_COMPILE_INFO},
        {GenerateMinHIZTask::NAME, GenerateMinHIZTask::PIPELINE_COMPILE_INFO},
        {GenerateMaxHIZTask::NAME, GenerateMaxHIZTask::PIPELINE_COMPILE_INFO},
        {GenerateLuminanceHistogramTask::NAME, GenerateLuminanceHistogramTask::PIPELINE_COMPILE_INFO},
        {ResolveLuminanceHistogramTask::NAME, ResolveLuminanceHistogramTask::PIPELINE_COMPILE_INFO},
        //{TemporalAntiAliasingTask::NAME, TemporalAntiAliasingTask::PIPELINE_COMPILE_INFO},
    };

    for (auto [name, info] : computes) {
        auto compilation_result = this->context->pipeline_manager.add_compute_pipeline(info);
        std::cout << std::string{name} + " " << compilation_result.to_string() << std::endl;
        this->context->compute_pipelines[name] = compilation_result.value();
    }
}

void Renderer::rebuild_task_graph() {
    auto scene = scene_hiearchy_panel->scene;

    render_task_graph = daxa::TaskGraph({
        .device = context->device,
        .swapchain = context->swapchain,
        .name = "render task graph",
    });

    render_task_graph.use_persistent_image(swapchain_image);
    render_task_graph.use_persistent_image(color_image);
    render_task_graph.use_persistent_image(albedo_image);
    render_task_graph.use_persistent_image(emissive_image);
    render_task_graph.use_persistent_image(normal_image);
    render_task_graph.use_persistent_image(depth_image);
    render_task_graph.use_persistent_image(velocity_image);
    render_task_graph.use_persistent_image(ssao_image);
    render_task_graph.use_persistent_image(ssao_blur_image);
    render_task_graph.use_persistent_image(terrain_normalmap_task);
    render_task_graph.use_persistent_image(sun_shadow_image);
    render_task_graph.use_persistent_image(clouds_image);
    render_task_graph.use_persistent_image(metallic_roughness_image);
    render_task_graph.use_persistent_image(ssr_image);
    render_task_graph.use_persistent_image(depth_of_field_image);
    render_task_graph.use_persistent_image(previous_color_image);
    render_task_graph.use_persistent_image(previous_velocity_image);
    render_task_graph.use_persistent_image(resolved_image);

    render_task_graph.use_persistent_buffer(terrain_vertices);
    render_task_graph.use_persistent_buffer(terrain_indices);
    render_task_graph.use_persistent_buffer(auto_exposure_buffer);

    for(auto& mip : bloom_mip_chain) {
        render_task_graph.use_persistent_image(mip);
    }

    render_task_graph.add_task(DepthPrepassTask {
        .uses = {
            .u_depth_image = depth_image
        },
        .context = context,
        .scene = scene.get()
    });

    min_hiz_image = GenerateMinHIZTask::build(context, render_task_graph, depth_image);
    max_hiz_image = GenerateMaxHIZTask::build(context, render_task_graph, depth_image);

    render_task_graph.add_task(SunShadowDrawTask {
        .uses = {
            .u_depth_image = sun_shadow_image
        },
        .context = context,
        .scene = scene.get()
    });

    render_task_graph.add_task(SunShadowDrawTerrainTask {
        .uses = {
            .u_vertices = terrain_vertices,
            .u_indices = terrain_indices,
            .u_depth_image = sun_shadow_image
        },
        .context = context,
        .terrain_index_size = terrain_index_size,
        .terrain_heightmap = terrain_heightmap.get(),
    });

    render_task_graph.add_task(GBufferGenerationTask {
        .uses = {
            .u_albedo_image = albedo_image,
            .u_emissive_image = emissive_image,
            .u_normal_image = normal_image,
            .u_metallic_roughness_image = metallic_roughness_image,
            .u_velocity_image = velocity_image,
            .u_depth_image = depth_image,
        },
        .context = context,
        .scene = scene.get()
    });

    render_task_graph.add_task(DrawTerrainTask {
        .uses = {
            .u_vertices = terrain_vertices,
            .u_indices = terrain_indices,
            .u_terrain_normal_image = terrain_normalmap_task,
            .u_albedo_image = albedo_image,
            .u_normal_image = normal_image,
            .u_velocity_image = velocity_image,
            .u_depth_image = depth_image
        },
        .context = context,
        .terrain_index_size = terrain_index_size,
        .terrain_heightmap = terrain_heightmap.get(),
        .terrain_albedomap = terrain_albedomap.get(),
    });

    render_task_graph.add_task(BloomDownsampleTask {
        .uses = {
            .u_higher_mip = emissive_image,
            .u_lower_mip = bloom_mip_chain[0]
        },
        .context = context,
        .index = 0
    });

    for(u32 i = 0; i < mip_chain_length - 1; i++) {
        render_task_graph.add_task(BloomDownsampleTask {
            .uses = {
                .u_higher_mip = bloom_mip_chain[i],
                .u_lower_mip = bloom_mip_chain[i + 1]
            },
            .context = context,
            .index = i + 1
        });
    }

    for(u32 i = mip_chain_length - 1; i > 0; i--) {
        render_task_graph.add_task(BloomUpsampleTask {
            .uses = {
                .u_higher_mip = bloom_mip_chain[i - 1],
                .u_lower_mip = bloom_mip_chain[i]
            },
            .context = context,
            .index = i
        });
    }

    render_task_graph.add_task(BloomUpsampleTask {
        .uses = {
            .u_higher_mip = emissive_image,
            .u_lower_mip = bloom_mip_chain[0]
        },
        .context = context,
        .index = 0
    });

    render_task_graph.add_task(SSAOGenerationTask {
        .uses = {
            .u_target_image = ssao_image,
            .u_normal_image = normal_image,
            .u_depth_image = depth_image
        },
        .context = context,
    });

    render_task_graph.add_task(SSAOBlurTask {
        .uses = {
            .u_target_image = ssao_blur_image,
            .u_ssao_image = ssao_image
        },
        .context = context,
    });

    render_task_graph.add_task(ScreenSpaceReflectionTask {
        .uses = {
            .u_ssr_image = ssr_image,
            .u_albedo_image = albedo_image,
            .u_normal_image = normal_image,
            .u_metallic_roughness_image = metallic_roughness_image,
            .u_depth_image = depth_image,
            .u_min_hiz = min_hiz_image,
            .u_max_hiz = max_hiz_image
        },
        .context = context
    });

    render_task_graph.add_task(CloudRenderingTask {
        .uses = {
            .u_target_image = clouds_image,
            .u_depth_image = depth_image
        },
        .context = context,
        .noise_texture = noise_texture.get()
    });

    render_task_graph.add_task(CompositionTask {
        .uses = {
            .u_target_image = color_image,
            .u_albedo_image = albedo_image,
            .u_emissive_image = emissive_image,
            .u_normal_image = normal_image,
            .u_depth_image = depth_image,
            .u_ssao_image = ssao_blur_image,
            .u_shadow_image = sun_shadow_image,
            .u_ssr_image = ssr_image,
            .u_metallic_roughness_image = metallic_roughness_image,
            .u_clouds_image = clouds_image
        },
        .context = context,
    });

    // render_task_graph.add_task(BlitImageToImageTask {
    //     .uses = {
    //         .u_target_image = depth_of_field_image.view().view({.base_mip_level = 0}),
    //         .u_color_image = color_image,
    //     },
    //     .context = context
    // });

    // {
    //     auto image_info = context->device.info_image(depth_of_field_image.get_state().images[0]);
    //     std::array<i32, 3> mip_size = {std::max<i32>(1, static_cast<i32>(image_info.size.x)), std::max<i32>(1, static_cast<i32>(image_info.size.y)), std::max<i32>(1, static_cast<i32>(image_info.size.z))};
    //     for (u32 i = 0; i < image_info.mip_level_count - 1; ++i) {
    //         std::array<i32, 3> next_mip_size = {std::max<i32>(1, mip_size[0] / 2), std::max<i32>(1, mip_size[1] / 2), std::max<i32>(1, mip_size[2] / 2)};
    //         render_task_graph.add_task(MipMappingTask{
    //             .uses = {
    //                 .u_higher_mip = depth_of_field_image.view().view({.base_mip_level = i+1}),
    //                 .u_lower_mip = depth_of_field_image.view().view({.base_mip_level = i}),
    //             },
    //             .context = context,
    //             .mip = i,
    //             .mip_size = mip_size,
    //             .next_mip_size = next_mip_size,
    //         });
    //         mip_size = next_mip_size;
    //     }
    // }

    // render_task_graph.add_task(DepthOfFieldTask {
    //     .uses = {
    //         .u_target_image = color_image,
    //         .u_depth_image = depth_image,
    //         .u_color_image = depth_of_field_image
    //     },
    //     .context = context
    // });

    render_task_graph.add_task(GenerateLuminanceHistogramTask {
        .uses = {
            .u_hdr_image = color_image,
            .u_auto_exposure_buffer = auto_exposure_buffer
        },
        .context = context
    });

    render_task_graph.add_task(ResolveLuminanceHistogramTask {
        .uses = {
            .u_auto_exposure_buffer = auto_exposure_buffer
        },
        .context = context
    });

    render_task_graph.add_task(TemporalAntiAliasingTask {
        .uses = {
            .u_target_image = resolved_image,
            .u_current_color_image = color_image,
            .u_previous_color_image = previous_color_image,
            .u_current_velocity_image = velocity_image,
            .u_previous_velocity_image = previous_velocity_image,
            .u_depth_image = depth_image
        },
        .context = context
    });

    render_task_graph.add_task(CopyImageTask {
        .uses = {
            .u_target_image = previous_color_image,
            .u_current_image = resolved_image
        },
        .context = context,
        .type = "velocity"
    });

    render_task_graph.add_task(CopyImageTask {
        .uses = {
            .u_target_image = previous_velocity_image,
            .u_current_image = velocity_image
        },
        .context = context,
        .type = "color"
    });

    // render_task_graph.add_task(DisplayAttachmentTask {
    //     .uses = {
    //         .u_target_image = swapchain_image,
    //         .u_displayed_image_1 = albedo_image,
    //         .u_displayed_image_2 = emissive_image,
    //         .u_displayed_image_3 = normal_image
    //     },
    //     .context = context
    // });

    render_task_graph.add_task(ToneMappingTask {
        .uses = {
            .u_target_image = swapchain_image,
            .u_color_image = resolved_image,
            .u_auto_exposure_buffer = auto_exposure_buffer
        },
        .context = context
    });

    render_task_graph.add_task({
        .uses = {
            daxa::ImageColorAttachment<>{swapchain_image},
        },
        .task = [this](daxa::TaskInterface ti) {
            auto cmd_list = ti.get_command_list();
            auto size = ti.get_device().info_image(ti.uses[swapchain_image].image()).size;
            imgui_renderer.record_commands(ImGui::GetDrawData(), cmd_list, ti.uses[swapchain_image].image(), size.x, size.y);
        },
        .name = "ImGui Draw",
    });

    render_task_graph.submit({});
    render_task_graph.present({});
    render_task_graph.complete({});

};