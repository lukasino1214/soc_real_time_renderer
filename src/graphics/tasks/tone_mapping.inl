#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>
#include <daxa/utils/task_graph.inl>

#include "../shared.inl"

#if __cplusplus || defined(ToneMapping_SHADER)

DAXA_DECL_TASK_USES_BEGIN(ToneMapping, 2)
DAXA_TASK_USE_IMAGE(u_target_image, REGULAR_2D, COLOR_ATTACHMENT)
DAXA_TASK_USE_IMAGE(u_color_image, REGULAR_2D, FRAGMENT_SHADER_SAMPLED)
DAXA_TASK_USE_BUFFER(u_auto_exposure_buffer, daxa_BufferPtr(AutoExposure), COMPUTE_SHADER_READ)
DAXA_DECL_TASK_USES_END()
#endif

#if __cplusplus
#include "../../context.hpp"

struct ToneMappingTask {
    DAXA_USE_TASK_HEADER(ToneMapping)

    inline static const daxa::RasterPipelineCompileInfo PIPELINE_COMPILE_INFO = daxa::RasterPipelineCompileInfo {
        .vertex_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/tone_mapping.inl" }, },
            .compile_options = { .defines = { { std::string{ToneMappingTask::NAME} + "_SHADER", "1" } } }
        },
        .fragment_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/tone_mapping.inl" }, },
            .compile_options = { .defines = { { std::string{ToneMappingTask::NAME} + "_SHADER", "1" } } }
        },
        .depth_test = {
            .depth_attachment_format = daxa::Format::D32_SFLOAT,
            .enable_depth_test = false,
            .enable_depth_write = false,
            .depth_test_compare_op = daxa::CompareOp::LESS_OR_EQUAL
        },
        .raster = {
            .face_culling = daxa::FaceCullFlagBits::NONE
        },
        .name = std::string{ToneMappingTask::NAME}
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

#if defined(ToneMapping_SHADER)
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

vec3 xyYToXYZ(vec3 xyY) {
  float Y = xyY.z;
  float X = (xyY.x * Y) / xyY.y;
  float Z = ((1.0f - xyY.x - xyY.y) * Y) / xyY.y;

  return vec3(X, Y, Z);
}

vec3 Unproject(vec2 xy) {
  return xyYToXYZ(vec3(xy.x, xy.y, 1));				
}

mat3 PrimariesToMatrix(vec2 xy_red, vec2 xy_green, vec2 xy_blue, vec2 xy_white) {
  vec3 XYZ_red = Unproject(xy_red);
  vec3 XYZ_green = Unproject(xy_green);
  vec3 XYZ_blue = Unproject(xy_blue);
  vec3 XYZ_white = Unproject(xy_white);

  mat3 temp = mat3(XYZ_red.x,	  1.0, XYZ_red.z,
                    XYZ_green.x, 1.f, XYZ_green.z,
                    XYZ_blue.x,  1.0, XYZ_blue.z);
  vec3 scale = inverse(temp) * XYZ_white;

  return mat3(XYZ_red * scale.x, XYZ_green * scale.y, XYZ_blue * scale.z);
}

mat3 ComputeCompressionMatrix(vec2 xyR, vec2 xyG, vec2 xyB, vec2 xyW, float compression) {
  float scale_factor = 1.0 / (1.0 - compression);
  vec2 R = mix(xyW, xyR, scale_factor);
  vec2 G = mix(xyW, xyG, scale_factor);
  vec2 B = mix(xyW, xyB, scale_factor);
  vec2 W = xyW;

  return PrimariesToMatrix(R, G, B, W);
}

float DualSection(float x, float linear, float peak) {
	// Length of linear section
	float S = (peak * linear);
	if (x < S) {
		return x;
	} else {
		float C = peak / (peak - S);
		return peak - (peak - S) * exp((-C * (x - S)) / peak);
	}
}

vec3 DualSection(vec3 x, float linear, float peak) {
	x.x = DualSection(x.x, linear, peak);
	x.y = DualSection(x.y, linear, peak);
	x.z = DualSection(x.z, linear, peak);
	return x;
}

vec3 AgX_DS(vec3 color_srgb, float exposure, float saturation, float linear, float peak, float compression) {
  vec3 workingColor = max(color_srgb, 0.0f) * pow(2.0, exposure);

  mat3 sRGB_to_XYZ = PrimariesToMatrix(vec2(0.64, 0.33),
                                       vec2(0.3, 0.6), 
                                       vec2(0.15, 0.06), 
                                       vec2(0.3127, 0.3290));
  mat3 adjusted_to_XYZ = ComputeCompressionMatrix(vec2(0.64,0.33),
                                                  vec2(0.3,0.6), 
                                                  vec2(0.15,0.06), 
                                                  vec2(0.3127, 0.3290), compression);
  mat3 XYZ_to_adjusted = inverse(adjusted_to_XYZ);
  mat3 sRGB_to_adjusted = sRGB_to_XYZ * XYZ_to_adjusted;

  workingColor = sRGB_to_adjusted * workingColor;
  workingColor = clamp(DualSection(workingColor, linear, peak), 0.0, 1.0);
  
  vec3 luminanceWeight = vec3(0.2126729,  0.7151522,  0.0721750);
  vec3 desaturation = vec3(dot(workingColor, luminanceWeight));
  workingColor = mix(desaturation, workingColor, saturation);
  workingColor = clamp(workingColor, 0.0, 1.0);

  workingColor = inverse(sRGB_to_adjusted) * workingColor;

  return workingColor;
}

void main() {
    f32vec4 color = texture(daxa_sampler2D(u_color_image, globals.linear_sampler), in_uv);

    out_color = f32vec4(AgX_DS(color.rgb, deref(u_auto_exposure_buffer).exposure, globals.saturation, globals.agxDs_linear_section, globals.peak, globals.compression), 1.0);
}

#endif
#endif