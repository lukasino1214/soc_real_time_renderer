#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>
#include <daxa/utils/task_graph.inl>

#include "../shared.inl"

#if __cplusplus
#include "../../context.hpp"

DAXA_DECL_TASK_USES_BEGIN(BlitImageToImage, 2)
DAXA_TASK_USE_IMAGE(u_target_image, REGULAR_2D, TRANSFER_WRITE)
DAXA_TASK_USE_IMAGE(u_color_image, REGULAR_2D, TRANSFER_READ)
DAXA_DECL_TASK_USES_END()

struct BlitImageToImageTask {
    DAXA_USE_TASK_HEADER(BlitImageToImage)

    Context* context = {};

    void callback(daxa::TaskInterface ti) {
        auto cmd = ti.get_command_list();
        context->gpu_metrics[name]->start(cmd);

        i32 size_x = static_cast<i32>(ti.get_device().info_image(uses.u_target_image.image()).size.x);
        i32 size_y = static_cast<i32>(ti.get_device().info_image(uses.u_target_image.image()).size.y);

        cmd.blit_image_to_image(daxa::ImageBlitInfo {
            .src_image = uses.u_color_image.image(),
            .dst_image = uses.u_target_image.image(),
            .src_slice = {
                .mip_level = 0,
                .base_array_layer = 0,
                .layer_count = 1,
            },
            .src_offsets = {{{0, 0, 0}, {size_x, size_y, 1}}},
            .dst_slice = {
                .mip_level = 0,
                .base_array_layer = 0,
                .layer_count = 1,
            },
            .dst_offsets = {{{0, 0, 0}, {size_x, size_y, 1}}},
            .filter = daxa::Filter::LINEAR,
        });

        context->gpu_metrics[name]->end(cmd);
    }
};

DAXA_DECL_TASK_USES_BEGIN(MipMapping, 2)
DAXA_TASK_USE_IMAGE(u_higher_mip, REGULAR_2D, TRANSFER_WRITE)
DAXA_TASK_USE_IMAGE(u_lower_mip, REGULAR_2D, TRANSFER_READ)
DAXA_DECL_TASK_USES_END()

struct MipMappingTask {
    DAXA_USE_TASK_HEADER(MipMapping)

    Context* context = {};
    u32 mip = {};
    std::array<i32, 3> mip_size = {};
    std::array<i32, 3> next_mip_size = {};

    void callback(daxa::TaskInterface ti) {
        auto cmd = ti.get_command_list();
        std::string current_name = std::string{MipMappingTask::NAME} + " - " + std::to_string(mip);
        context->gpu_metrics[current_name]->start(cmd);

        cmd.blit_image_to_image({
            .src_image = uses.u_lower_mip.image(),
            .dst_image = uses.u_higher_mip.image(),
            .src_slice = {
                .mip_level = mip,
                .base_array_layer = 0,
                .layer_count = 1,
            },
            .src_offsets = {{{0, 0, 0}, {mip_size[0], mip_size[1], mip_size[2]}}},
            .dst_slice = {
                .mip_level = mip + 1,
                .base_array_layer = 0,
                .layer_count = 1,
            },
            .dst_offsets = {{{0, 0, 0}, {next_mip_size[0], next_mip_size[1], next_mip_size[2]}}},
            .filter = daxa::Filter::LINEAR,
        });

        context->gpu_metrics[current_name]->end(cmd);
    }
};

#endif

#if __cplusplus || defined(DepthOfField_SHADER)

DAXA_DECL_TASK_USES_BEGIN(DepthOfField, 2)
DAXA_TASK_USE_IMAGE(u_target_image, REGULAR_2D, COLOR_ATTACHMENT)
DAXA_TASK_USE_IMAGE(u_depth_image, REGULAR_2D, FRAGMENT_SHADER_SAMPLED)
DAXA_TASK_USE_IMAGE(u_color_image, REGULAR_2D, FRAGMENT_SHADER_SAMPLED)
DAXA_DECL_TASK_USES_END()
#endif

#if __cplusplus
#include "../../context.hpp"

struct DepthOfFieldTask {
    DAXA_USE_TASK_HEADER(DepthOfField)

    inline static const daxa::RasterPipelineCompileInfo PIPELINE_COMPILE_INFO = daxa::RasterPipelineCompileInfo {
        .vertex_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/depth_of_field.inl" }, },
            .compile_options = { .defines = { { std::string{DepthOfFieldTask::NAME} + "_SHADER", "1" } } }
        },
        .fragment_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/depth_of_field.inl" }, },
            .compile_options = { .defines = { { std::string{DepthOfFieldTask::NAME} + "_SHADER", "1" } } }
        },
        .color_attachments = {{.format = daxa::Format::R16G16B16A16_SFLOAT}},
        .depth_test = {
            .depth_attachment_format = daxa::Format::D32_SFLOAT,
            .enable_depth_test = false,
            .enable_depth_write = false,
            .depth_test_compare_op = daxa::CompareOp::LESS_OR_EQUAL
        },
        .raster = {
            .face_culling = daxa::FaceCullFlagBits::NONE
        },
        .name = std::string{DepthOfFieldTask::NAME}
    };

    Context* context = {};

    void callback(daxa::TaskInterface ti) {
        auto cmd = ti.get_command_list();
        context->gpu_metrics[name]->start(cmd);

        u32 size_x = ti.get_device().info_image(uses.u_target_image.image()).size.x;
        u32 size_y = ti.get_device().info_image(uses.u_target_image.image()).size.y;

        cmd.begin_renderpass( daxa::RenderPassBeginInfo {
            .color_attachments = { daxa::RenderAttachmentInfo {
                .image_view = uses.u_target_image.view(),
                .load_op = daxa::AttachmentLoadOp::CLEAR,
                .clear_value = std::array<f32, 4>{0.f, 0.f, 0.f, 1.0f},
            }},
            .render_area = {.x = 0, .y = 0, .width = size_x, .height = size_y},
        });

        cmd.set_uniform_buffer(context->shader_globals_set_info);
        cmd.set_uniform_buffer(ti.uses.get_uniform_buffer_info());
        cmd.set_pipeline(*context->raster_pipelines.at(PIPELINE_COMPILE_INFO.name));
        cmd.draw({ .vertex_count = 3 });
        cmd.end_renderpass();

        context->gpu_metrics[name]->end(cmd);
    }
};

#endif

#if defined(DepthOfField_SHADER)
#include "../shared.inl"

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out f32vec2 out_uv;

void main() {    
    out_uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(out_uv * 2.0f - 1.0f, 0.0f, 1.0f);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) out f32vec4 out_color;
layout(location = 0) in f32vec2 in_uv;

void main() {
    f32 depth = textureLod(daxa_sampler2D(u_depth_image, globals.linear_sampler), in_uv, 0).r;
    f32vec2 offset = 1.0 / f32vec2(textureSize(daxa_sampler2D(u_depth_image, globals.linear_sampler), 0).xy);
    if(depth < 1.0) {
        f32 object_distance = -globals.camera_far_clip * globals.camera_near_clip / (depth * (globals.camera_far_clip - globals.camera_near_clip) - globals.camera_far_clip);
        
        // f32 coc_scale = (globals.aperture * globals.focal_length * globals.plane_in_focus * (globals.camera_far_clip - globals.camera_near_clip)) / ((globals.plane_in_focus - globals.focal_length) * globals.camera_near_clip * globals.camera_far_clip);
        // f32 coc_bias = (globals.aperture * globals.focal_length * (globals.camera_near_clip - globals.plane_in_focus)) / ((globals.plane_in_focus * globals.focal_length) * globals.camera_near_clip);

        // f32 coc = abs(depth * coc_scale + coc_bias);

        f32 coc = abs(globals.aperture * (globals.focal_length * (object_distance - globals.plane_in_focus)) / (object_distance * (globals.plane_in_focus - globals.focal_length)));
        f32 max_coc = abs(globals.aperture * (globals.focal_length * (globals.camera_far_clip - globals.plane_in_focus)) / (object_distance * (globals.plane_in_focus - globals.focal_length)));
        coc = coc / max_coc;

        out_color = f32vec4(textureGrad(daxa_sampler2D(u_color_image, globals.depth_of_field_sampler), in_uv + f32vec2(offset.x, 0), coc.xx, coc.xx).rgb, 1.0) * 0.25 +
                    f32vec4(textureGrad(daxa_sampler2D(u_color_image, globals.depth_of_field_sampler), in_uv - f32vec2(offset.x, 0), coc.xx, coc.xx).rgb, 1.0) * 0.25 +
                    f32vec4(textureGrad(daxa_sampler2D(u_color_image, globals.depth_of_field_sampler), in_uv + f32vec2(0, offset.y), coc.xx, coc.xx).rgb, 1.0) * 0.25 +
                    f32vec4(textureGrad(daxa_sampler2D(u_color_image, globals.depth_of_field_sampler), in_uv - f32vec2(0, offset.y), coc.xx, coc.xx).rgb, 1.0) * 0.25;
        //out_color = textureLod(daxa_sampler2D(u_color_image, globals.linear_sampler), in_uv, 0);
    } else {
        out_color = texture(daxa_sampler2D(u_color_image, globals.linear_sampler), in_uv);
    }
}

#endif
#endif