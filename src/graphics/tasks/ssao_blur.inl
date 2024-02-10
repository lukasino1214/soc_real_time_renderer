#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>
#include <daxa/utils/task_graph.inl>

#if __cplusplus || defined(SSAOBlur_SHADER)

DAXA_DECL_TASK_USES_BEGIN(SSAOBlur, 2)
DAXA_TASK_USE_IMAGE(u_target_image, REGULAR_2D, COLOR_ATTACHMENT)
DAXA_TASK_USE_IMAGE(u_ssao_image, REGULAR_2D, FRAGMENT_SHADER_SAMPLED)
DAXA_DECL_TASK_USES_END()

#endif

#if __cplusplus
#include "../../context.hpp"


struct SSAOBlurTask {
    DAXA_USE_TASK_HEADER(SSAOBlur)

    inline static const daxa::RasterPipelineCompileInfo PIPELINE_COMPILE_INFO = daxa::RasterPipelineCompileInfo {
        .vertex_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/ssao_blur.inl" }, },
            .compile_options = { .defines = { { std::string{SSAOBlurTask::NAME} + "_SHADER", "1" } } }
        },
        .fragment_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/ssao_blur.inl" }, },
            .compile_options = { .defines = { { std::string{SSAOBlurTask::NAME} + "_SHADER", "1" } } }
        },
        .color_attachments = { 
            daxa::RenderAttachment { 
                .format = daxa::Format::R8_UNORM,
            }
        },
        .raster = {
            .face_culling = daxa::FaceCullFlagBits::NONE
        },
        .name = std::string{SSAOBlurTask::NAME}
    };

    Context* context = {};
    f32* bias = {}; 
    f32* radius = {};
    i32* kernel_size = {};
    
    void callback(daxa::TaskInterface ti) {
        auto cmd = ti.get_command_list();
        context->gpu_metrics[name]->start(cmd);

        u32 size_x = ti.get_device().info_image(uses.u_target_image.image()).size.x;
        u32 size_y = ti.get_device().info_image(uses.u_target_image.image()).size.y;

        cmd.begin_renderpass( daxa::RenderPassBeginInfo {
            .color_attachments = { daxa::RenderAttachmentInfo {
                .image_view = uses.u_target_image.view(),
                .load_op = daxa::AttachmentLoadOp::CLEAR,
                .clear_value = std::array<f32, 4>{0.f, 0.f, 0.f, 0.0f},
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

#if defined(SSAOBlur_SHADER)
#include "../shared.inl"

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out f32vec2 out_uv;

void main() {    
    out_uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(out_uv * 2.0f - 1.0f, 0.0f, 1.0f);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) in f32vec2 in_uv;

layout(location = 0) out f32 out_ssao;

void main() {
    const i32 blur_range = 2;
	i32 n = 0;
	f32vec2 texel_size = 1.0 / f32vec2(textureSize(daxa_sampler2D(u_ssao_image, globals.linear_sampler), 0));
	f32 result = 0.0;

	for (i32 x = -blur_range; x < blur_range; x++) {
		for (i32 y = -blur_range; y < blur_range; y++) {
			f32vec2 offset = f32vec2(f32(x), f32(y)) * texel_size;
            result += texture(daxa_sampler2D(u_ssao_image, globals.linear_sampler), in_uv + offset).r;
			n++;
		}
	}

	out_ssao = result / f32(n);
}

#endif
#endif