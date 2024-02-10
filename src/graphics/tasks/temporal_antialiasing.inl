#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>
#include <daxa/utils/task_graph.inl>

#include "../shared.inl"

#if __cplusplus
#include "../../context.hpp"

DAXA_DECL_TASK_USES_BEGIN(CopyImage, 2)
DAXA_TASK_USE_IMAGE(u_target_image, REGULAR_2D, TRANSFER_WRITE)
DAXA_TASK_USE_IMAGE(u_current_image, REGULAR_2D, TRANSFER_READ)
DAXA_DECL_TASK_USES_END()

struct CopyImageTask {
    DAXA_USE_TASK_HEADER(CopyImage)

    Context* context = {};
    std::string type = {};

    void callback(daxa::TaskInterface ti) {
        auto cmd = ti.get_command_list();
        context->gpu_metrics[name + " - " + type]->start(cmd);

        u32 size_x = ti.get_device().info_image(uses.u_target_image.image()).size.x;
        u32 size_y = ti.get_device().info_image(uses.u_target_image.image()).size.y;

        cmd.copy_image_to_image(daxa::ImageCopyInfo {
            .src_image = uses.u_current_image.image(),
            .dst_image = uses.u_target_image.image(),
            .extent = { size_x, size_y, 1 }
        });

        context->gpu_metrics[name + " - " + type]->end(cmd);
    }
};
#endif

#if __cplusplus || defined(TemporalAntiAliasing_SHADER)
DAXA_DECL_TASK_USES_BEGIN(TemporalAntiAliasing, 2)
DAXA_TASK_USE_IMAGE(u_target_image, REGULAR_2D, COLOR_ATTACHMENT)
DAXA_TASK_USE_IMAGE(u_current_color_image, REGULAR_2D, COMPUTE_SHADER_SAMPLED)
DAXA_TASK_USE_IMAGE(u_previous_color_image, REGULAR_2D, COMPUTE_SHADER_SAMPLED)
DAXA_TASK_USE_IMAGE(u_current_velocity_image, REGULAR_2D, COMPUTE_SHADER_SAMPLED)
DAXA_TASK_USE_IMAGE(u_previous_velocity_image, REGULAR_2D, COMPUTE_SHADER_SAMPLED)
DAXA_TASK_USE_IMAGE(u_depth_image, REGULAR_2D, COMPUTE_SHADER_SAMPLED)
DAXA_DECL_TASK_USES_END()
#endif

#if __cplusplus
#include "../../context.hpp"

struct TemporalAntiAliasingTask {
    DAXA_USE_TASK_HEADER(TemporalAntiAliasing)

    inline static const daxa::RasterPipelineCompileInfo PIPELINE_COMPILE_INFO = daxa::RasterPipelineCompileInfo {
        .vertex_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/temporal_antialiasing.inl" }, },
            .compile_options = { .defines = { { std::string{TemporalAntiAliasingTask::NAME} + "_SHADER", "1" } } }
        },
        .fragment_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/temporal_antialiasing.inl" }, },
            .compile_options = { .defines = { { std::string{TemporalAntiAliasingTask::NAME} + "_SHADER", "1" } } }
        },
        .color_attachments = {{ .format = daxa::Format::R16G16B16A16_SFLOAT }},
        .depth_test = {
            .depth_attachment_format = daxa::Format::D32_SFLOAT,
            .enable_depth_test = false,
            .enable_depth_write = false,
            .depth_test_compare_op = daxa::CompareOp::LESS_OR_EQUAL
        },
        .raster = {
            .face_culling = daxa::FaceCullFlagBits::NONE
        },
        .name = std::string{TemporalAntiAliasingTask::NAME}
    };

    // inline static const daxa::ComputePipelineCompileInfo PIPELINE_COMPILE_INFO = {
    //     .shader_info = daxa::ShaderCompileInfo{
    //         .source = daxa::ShaderFile{"src/graphics/tasks/temporal_antialiasing.inl"},
    //         .compile_options = { .defines = { { std::string{TemporalAntiAliasingTask::NAME} + "_SHADER", "1" } } }
    //     },
    //     .name = std::string{TemporalAntiAliasingTask::NAME}
    // };

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

        // cmd.set_pipeline(*context->compute_pipelines.at(PIPELINE_COMPILE_INFO.name));
        // cmd.dispatch((size_x + 8 - 1) / 8, (size_x + 4 - 1) / 4, 1); 

        context->gpu_metrics[name]->end(cmd);
    }
};

#endif

#if defined(TemporalAntiAliasing_SHADER)
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
    f32vec4[9] neighbors;
    f32[9] gauss_weights = f32[](
        1.0/16.0, 1.0/8.0, 1.0/16.0, 
        1.0/ 8.0, 1.0/4.0, 1.0/ 8.0,
        1.0/16.0, 1.0/8.0, 1.0/16.0
    );

    f32vec4 blurred_col = f32vec4(0.0);
    f32 closest_depth = 1.0;

    f32vec2 depth_uv = in_uv;
    f32vec4 min_color = f32vec4(10.0e5);
    f32vec4 max_color = f32vec4(-10.0e5); 

    f32vec2 pixel_offset_uv = 1.0 / f32vec2(globals.resolution);

    for(i32 y = 1; y > -2; y--) {
        for(i32 x = 1; x > -2; x--) {
            i32 index = ((y + 1) * 3) + (x + 1);
            f32vec2 uv_offset = pixel_offset_uv * f32vec2(x, y);

            neighbors[index] = texture(daxa_sampler2D(u_current_color_image, globals.linear_sampler), in_uv + uv_offset);
            f32 depth = texture(daxa_sampler2D(u_depth_image, globals.linear_sampler), in_uv + uv_offset).r;

            closest_depth = min(depth, closest_depth);
            depth_uv = i32(closest_depth == depth) * (in_uv + uv_offset) + i32(closest_depth != depth) * depth_uv;

            min_color = min(neighbors[index], min_color);
            max_color = max(neighbors[index], max_color);

            blurred_col += gauss_weights[index] * neighbors[index];
        } 
    }

    f32vec4 color = neighbors[5];
    f32vec2 velocity = texture(daxa_sampler2D(u_current_velocity_image, globals.linear_sampler), depth_uv).xy;
    f32 accum_factor = min(0.1, f32(globals.frame_counter));

    f32vec2 vel_shift_uv = in_uv - velocity;
    f32vec4 accumulation_color = texture(daxa_sampler2D(u_previous_color_image, globals.linear_sampler), vel_shift_uv);

    if(any(lessThan(vel_shift_uv, f32vec2(0.0))) || any(greaterThan(vel_shift_uv, f32vec2(1.0)))) {
        accum_factor = 1.0;
    }

    accumulation_color = clamp(accumulation_color, min_color, max_color);
    out_color = color * accum_factor + accumulation_color * (1 - accum_factor);

    f32vec2 prev_velocity = texture(daxa_sampler2D(u_previous_velocity_image, globals.linear_sampler), vel_shift_uv).xy;
    f32 velocity_len = length(prev_velocity - velocity);
    f32 velocity_disocclusion = clamp((velocity_len - 0.001) * 10.0, 0.0, 1.0);
    out_color = mix(out_color, blurred_col, velocity_disocclusion);

    // f32vec3 current_color = texture(daxa_sampler2D(u_current_color_image, globals.linear_sampler), in_uv).xy;
    // f32vec2 velocity = texture(daxa_sampler2D(u_current_velocity_image, globals.linear_sampler), in_uv).xy;
    // f32vec2 reprojected_uv = in_uv - velocity;
    // f32vec3 reprojected_color = texture(daxa_sampler2D(u_previous_color_image, globals.linear_sampler), in_uv).xy;
    // out_color = mix(current_color, reprojected_color, 0.8);

}

#endif

#endif