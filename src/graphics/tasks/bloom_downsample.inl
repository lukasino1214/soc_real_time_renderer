#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>
#include <daxa/utils/task_graph.inl>

#if __cplusplus || defined(BloomDownsample_SHADER)

DAXA_DECL_TASK_USES_BEGIN(BloomDownsample, 2)
DAXA_TASK_USE_IMAGE(u_higher_mip, REGULAR_2D, COLOR_ATTACHMENT)
DAXA_TASK_USE_IMAGE(u_lower_mip, REGULAR_2D, FRAGMENT_SHADER_SAMPLED)
DAXA_DECL_TASK_USES_END()

#endif

#if __cplusplus
#include "../../context.hpp"


struct BloomDownsampleTask {
    DAXA_USE_TASK_HEADER(BloomDownsample)

    inline static const daxa::RasterPipelineCompileInfo PIPELINE_COMPILE_INFO = daxa::RasterPipelineCompileInfo {
        .vertex_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/bloom_downsample.inl" }, },
            .compile_options = { .defines = { { std::string{BloomDownsampleTask::NAME} + "_SHADER", "1" } } }
        },
        .fragment_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/bloom_downsample.inl" }, },
            .compile_options = { .defines = { { std::string{BloomDownsampleTask::NAME} + "_SHADER", "1" } } }
        },
        .color_attachments = { 
            daxa::RenderAttachment { 
                .format = daxa::Format::R16G16B16A16_SFLOAT,
            }
        },
        .raster = {
            .face_culling = daxa::FaceCullFlagBits::NONE
        },
        .name = std::string{BloomDownsampleTask::NAME}
    };

    Context* context = {};
    u32 index = {};
    
    void callback(daxa::TaskInterface ti) {
        auto cmd = ti.get_command_list();
        std::string current_name = std::string{BloomDownsampleTask::NAME} + " - " + std::to_string(index);
        context->gpu_metrics[current_name]->start(cmd);

        auto lower_size = ti.get_device().info_image(uses.u_lower_mip.image()).size;

        cmd.begin_renderpass( daxa::RenderPassBeginInfo {
            .color_attachments = { daxa::RenderAttachmentInfo {
                .image_view = uses.u_lower_mip.view(),
                .load_op = daxa::AttachmentLoadOp::CLEAR,
                .clear_value = std::array<f32, 4>{0.f, 0.f, 0.f, 1.0f},
            }},
            .render_area = {.x = 0, .y = 0, .width = lower_size.x, .height = lower_size.y},
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

#if defined(BloomDownsample_SHADER)
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

f32vec3 PowVec3(f32vec3 v, f32 p) {
    return f32vec3(pow(v.x, p), pow(v.y, p), pow(v.z, p));
}

const f32 invGamma = 1.0 / 2.2;
f32vec3 ToSRGB(f32vec3 v)   { return PowVec3(v, invGamma); }

f32 sRGBToLuma(f32vec3 col) {
    //return dot(col, f32vec3(0.2126f, 0.7152f, 0.0722f));
	return dot(col, f32vec3(0.299f, 0.587f, 0.114f));
}

f32 KarisAverage(f32vec3 col) {
	// Formula is 1 / (1 + luma)
	f32 luma = sRGBToLuma(ToSRGB(col)) * 0.25f;
	return 1.0f / (1.0f + luma);
}

void main() {
    f32vec2 srcTexelSize = 1.0 / f32vec2(textureSize(daxa_sampler2D(u_higher_mip, globals.linear_sampler), 0));
	f32 x = srcTexelSize.x;
	f32 y = srcTexelSize.y;

	// Take 13 samples around current texel:
	// a - b - c
	// - j - k -
	// d - e - f
	// - l - m -
	// g - h - i
	// === ('e' is the current texel) ===
	f32vec3 a = texture(daxa_sampler2D(u_higher_mip, globals.linear_sampler), f32vec2(in_uv.x - 2*x, in_uv.y + 2*y)).rgb;
	f32vec3 b = texture(daxa_sampler2D(u_higher_mip, globals.linear_sampler), f32vec2(in_uv.x,       in_uv.y + 2*y)).rgb;
	f32vec3 c = texture(daxa_sampler2D(u_higher_mip, globals.linear_sampler), f32vec2(in_uv.x + 2*x, in_uv.y + 2*y)).rgb;

	f32vec3 d = texture(daxa_sampler2D(u_higher_mip, globals.linear_sampler), f32vec2(in_uv.x - 2*x, in_uv.y)).rgb;
	f32vec3 e = texture(daxa_sampler2D(u_higher_mip, globals.linear_sampler), f32vec2(in_uv.x,       in_uv.y)).rgb;
	f32vec3 f = texture(daxa_sampler2D(u_higher_mip, globals.linear_sampler), f32vec2(in_uv.x + 2*x, in_uv.y)).rgb;

	f32vec3 g = texture(daxa_sampler2D(u_higher_mip, globals.linear_sampler), f32vec2(in_uv.x - 2*x, in_uv.y - 2*y)).rgb;
	f32vec3 h = texture(daxa_sampler2D(u_higher_mip, globals.linear_sampler), f32vec2(in_uv.x,       in_uv.y - 2*y)).rgb;
	f32vec3 i = texture(daxa_sampler2D(u_higher_mip, globals.linear_sampler), f32vec2(in_uv.x + 2*x, in_uv.y - 2*y)).rgb;

	f32vec3 j = texture(daxa_sampler2D(u_higher_mip, globals.linear_sampler), f32vec2(in_uv.x - x, in_uv.y + y)).rgb;
	f32vec3 k = texture(daxa_sampler2D(u_higher_mip, globals.linear_sampler), f32vec2(in_uv.x + x, in_uv.y + y)).rgb;
	f32vec3 l = texture(daxa_sampler2D(u_higher_mip, globals.linear_sampler), f32vec2(in_uv.x - x, in_uv.y - y)).rgb;
	f32vec3 m = texture(daxa_sampler2D(u_higher_mip, globals.linear_sampler), f32vec2(in_uv.x + x, in_uv.y - y)).rgb;


    out_emissive.rgb = e*0.125;                // ok
    out_emissive.rgb += (a+c+g+i)*0.03125;     // ok
    out_emissive.rgb += (b+d+f+h)*0.0625;      // ok
    out_emissive.rgb += (j+k+l+m)*0.125;       // ok
}

#endif
#endif