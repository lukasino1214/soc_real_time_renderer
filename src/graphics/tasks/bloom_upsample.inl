#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>
#include <daxa/utils/task_graph.inl>

#if __cplusplus || defined(BloomUpsample_SHADER)

DAXA_DECL_TASK_USES_BEGIN(BloomUpsample, 2)
DAXA_TASK_USE_IMAGE(u_higher_mip, REGULAR_2D, COLOR_ATTACHMENT)
DAXA_TASK_USE_IMAGE(u_lower_mip, REGULAR_2D, FRAGMENT_SHADER_SAMPLED)
DAXA_DECL_TASK_USES_END()

#endif

#if __cplusplus
#include "../../context.hpp"


struct BloomUpsampleTask {
    DAXA_USE_TASK_HEADER(BloomUpsample)

    inline static const daxa::RasterPipelineCompileInfo PIPELINE_COMPILE_INFO = daxa::RasterPipelineCompileInfo {
        .vertex_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/bloom_upsample.inl" }, },
            .compile_options = { .defines = { { std::string{BloomUpsampleTask::NAME} + "_SHADER", "1" } } }
        },
        .fragment_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/bloom_upsample.inl" }, },
            .compile_options = { .defines = { { std::string{BloomUpsampleTask::NAME} + "_SHADER", "1" } } }
        },
        .color_attachments = { 
            daxa::RenderAttachment { 
                .format = daxa::Format::R16G16B16A16_SFLOAT,
                .blend = daxa::BlendInfo {
                    .blend_enable = true,
                    .src_color_blend_factor = daxa::BlendFactor::ONE,
                    .dst_color_blend_factor = daxa::BlendFactor::ONE,
                    .color_blend_op = daxa::BlendOp::ADD,
                    .src_alpha_blend_factor = daxa::BlendFactor::ONE,
                    .dst_alpha_blend_factor = daxa::BlendFactor::ONE,
                    .alpha_blend_op = daxa::BlendOp::ADD,
                }
            }
        },
        .raster = {
            .face_culling = daxa::FaceCullFlagBits::NONE
        },
        .name = std::string{BloomUpsampleTask::NAME}
    };

    Context* context = {};
    u32 index = {};
    
    void callback(daxa::TaskInterface ti) {
        auto cmd = ti.get_command_list();
        std::string current_name = std::string{BloomUpsampleTask::NAME} + " - " + std::to_string(index);
        context->gpu_metrics[current_name]->start(cmd);

        auto higher_size = ti.get_device().info_image(uses.u_higher_mip.image()).size;

        cmd.begin_renderpass( daxa::RenderPassBeginInfo {
            .color_attachments = { daxa::RenderAttachmentInfo {
                .image_view = uses.u_higher_mip.view(),
                .load_op = daxa::AttachmentLoadOp::CLEAR,
                .clear_value = std::array<f32, 4>{0.f, 0.f, 0.f, 1.0f},
            }},
            .render_area = {.x = 0, .y = 0, .width = higher_size.x, .height = higher_size.y},
        });

        cmd.set_uniform_buffer(context->shader_globals_set_info);
        cmd.set_uniform_buffer(ti.uses.get_uniform_buffer_info());
        cmd.set_pipeline(*context->raster_pipelines.at(PIPELINE_COMPILE_INFO.name));
        cmd.draw({ .vertex_count = 3 });
        cmd.end_renderpass();
        context->gpu_metrics[current_name]->end(cmd);
    }
};
#endif

#if defined(BloomUpsample_SHADER)
#include "../shared.inl"

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out f32vec2 out_uv;

void main() {    
    out_uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(out_uv * 2.0f - 1.0f, 0.0f, 1.0f);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) in f32vec2 in_uv;

layout(location = 0) out f32vec4 out_emissive;

void main() {
    f32 x = 1.0f / f32(textureSize(daxa_sampler2D(u_lower_mip, globals.linear_sampler), 0).x);
    f32 y = 1.0f / f32(textureSize(daxa_sampler2D(u_lower_mip, globals.linear_sampler), 0).y);

	// Take 9 samples around current texel:
	// a - b - c
	// d - e - f
	// g - h - i
	// === ('e' is the current texel) ===
	f32vec3 a = texture(daxa_sampler2D(u_lower_mip, globals.linear_sampler), f32vec2(in_uv.x - x, in_uv.y + y)).rgb;
	f32vec3 b = texture(daxa_sampler2D(u_lower_mip, globals.linear_sampler), f32vec2(in_uv.x,     in_uv.y + y)).rgb;
	f32vec3 c = texture(daxa_sampler2D(u_lower_mip, globals.linear_sampler), f32vec2(in_uv.x + x, in_uv.y + y)).rgb;

	f32vec3 d = texture(daxa_sampler2D(u_lower_mip, globals.linear_sampler), f32vec2(in_uv.x - x, in_uv.y)).rgb;
	f32vec3 e = texture(daxa_sampler2D(u_lower_mip, globals.linear_sampler), f32vec2(in_uv.x,     in_uv.y)).rgb;
	f32vec3 f = texture(daxa_sampler2D(u_lower_mip, globals.linear_sampler), f32vec2(in_uv.x + x, in_uv.y)).rgb;

	f32vec3 g = texture(daxa_sampler2D(u_lower_mip, globals.linear_sampler), f32vec2(in_uv.x - x, in_uv.y - y)).rgb;
	f32vec3 h = texture(daxa_sampler2D(u_lower_mip, globals.linear_sampler), f32vec2(in_uv.x,     in_uv.y - y)).rgb;
	f32vec3 i = texture(daxa_sampler2D(u_lower_mip, globals.linear_sampler), f32vec2(in_uv.x + x, in_uv.y - y)).rgb;

	// Apply weighted distribution, by using a 3x3 tent filter:
	//  1   | 1 2 1 |
	// -- * | 2 4 2 |
	// 16   | 1 2 1 |
	out_emissive.rgb = e*4.0;
	out_emissive.rgb += (b+d+f+h)*2.0;
	out_emissive.rgb += (a+c+g+i);
	out_emissive.rgb *= 1.0 / 16.0;
}

#endif
#endif