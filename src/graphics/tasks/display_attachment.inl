#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>
#include <daxa/utils/task_graph.inl>

#include "../shared.inl"

#if __cplusplus || defined(DisplayAttachment_SHADER)

DAXA_DECL_TASK_USES_BEGIN(DisplayAttachment, 2)
DAXA_TASK_USE_IMAGE(u_target_image, REGULAR_2D, COLOR_ATTACHMENT)
DAXA_TASK_USE_IMAGE(u_displayed_image_1, REGULAR_2D, FRAGMENT_SHADER_SAMPLED)
DAXA_TASK_USE_IMAGE(u_displayed_image_2, REGULAR_2D, FRAGMENT_SHADER_SAMPLED)
DAXA_TASK_USE_IMAGE(u_displayed_image_3, REGULAR_2D, FRAGMENT_SHADER_SAMPLED)
DAXA_DECL_TASK_USES_END()

#endif

#if __cplusplus
#include "../../context.hpp"


struct DisplayAttachmentTask {
    DAXA_USE_TASK_HEADER(DisplayAttachment)

    inline static const daxa::RasterPipelineCompileInfo PIPELINE_COMPILE_INFO = daxa::RasterPipelineCompileInfo {
        .vertex_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/display_attachment.inl" }, },
            .compile_options = { .defines = { { std::string{DisplayAttachment::NAME} + "_SHADER", "1" } } }
        },
        .fragment_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/display_attachment.inl" }, },
            .compile_options = { .defines = { { std::string{DisplayAttachment::NAME} + "_SHADER", "1" } } }
        },
        .raster = {
            .face_culling = daxa::FaceCullFlagBits::NONE
        },
        .name = std::string{DisplayAttachment::NAME}
    };

    Context* context = {};
    
    void callback(daxa::TaskInterface ti) {
        auto cmd = ti.get_command_list();

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
    }
};
#endif

#if defined(DisplayAttachment_SHADER)
#include "../shared.inl"

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out f32vec2 out_uv;

void main() {    
    out_uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(out_uv * 2.0f - 1.0f, 0.0f, 1.0f);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) in f32vec2 in_uv;

layout(location = 0) out f32vec4 out_color;

void main() {
    //out_color = f32vec4(texture(daxa_sampler2D(u_displayed_image, globals.nearest_sampler), in_uv).rgb * 0.5 + 0.5, 1.0);
    out_color = f32vec4(texture(daxa_sampler2D(u_displayed_image_2, globals.nearest_sampler), in_uv).rgb, 1.0);
}

#endif
#endif