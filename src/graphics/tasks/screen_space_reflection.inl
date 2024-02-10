#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>
#include <daxa/utils/task_graph.inl>

#include "../shared.inl"


#if __cplusplus || defined(ScreenSpaceReflection_SHADER)

DAXA_DECL_TASK_USES_BEGIN(ScreenSpaceReflection, 2)
DAXA_TASK_USE_IMAGE(u_ssr_image, REGULAR_2D, COLOR_ATTACHMENT)
DAXA_TASK_USE_IMAGE(u_albedo_image, REGULAR_2D, FRAGMENT_SHADER_SAMPLED)
DAXA_TASK_USE_IMAGE(u_normal_image, REGULAR_2D, FRAGMENT_SHADER_SAMPLED)
DAXA_TASK_USE_IMAGE(u_metallic_roughness_image, REGULAR_2D, FRAGMENT_SHADER_SAMPLED)
DAXA_TASK_USE_IMAGE(u_depth_image, REGULAR_2D, FRAGMENT_SHADER_SAMPLED)
DAXA_TASK_USE_IMAGE(u_min_hiz, REGULAR_2D, FRAGMENT_SHADER_SAMPLED)
DAXA_TASK_USE_IMAGE(u_max_hiz, REGULAR_2D, FRAGMENT_SHADER_SAMPLED)
DAXA_DECL_TASK_USES_END()

#endif

#if __cplusplus
#include "../../context.hpp"

struct ScreenSpaceReflectionTask {
    DAXA_USE_TASK_HEADER(ScreenSpaceReflection)

    inline static const daxa::RasterPipelineCompileInfo PIPELINE_COMPILE_INFO = daxa::RasterPipelineCompileInfo {
        .vertex_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/screen_space_reflection.inl" }, },
            .compile_options = { .defines = { { std::string{ScreenSpaceReflectionTask::NAME} + "_SHADER", "1" } } }
        },
        .fragment_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/screen_space_reflection.inl" }, },
            .compile_options = { .defines = { { std::string{ScreenSpaceReflectionTask::NAME} + "_SHADER", "1" } } }
        },
        .color_attachments = { { .format = daxa::Format::R16G16B16A16_SFLOAT, } },
        .raster = {
            .face_culling = daxa::FaceCullFlagBits::NONE
        },
        .name = std::string{ScreenSpaceReflectionTask::NAME}
    };

    Context* context = {};

    void callback(daxa::TaskInterface ti) {
        auto cmd = ti.get_command_list();
        context->gpu_metrics[name]->start(cmd);

        u32 size_x = ti.get_device().info_image(uses.u_ssr_image.image()).size.x;
        u32 size_y = ti.get_device().info_image(uses.u_ssr_image.image()).size.y;

        cmd.begin_renderpass( daxa::RenderPassBeginInfo {
            .color_attachments = { daxa::RenderAttachmentInfo {
                .image_view = uses.u_ssr_image.view(),
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
    };
};

#endif

#if defined(ScreenSpaceReflection_SHADER)
#include "../shared.inl"

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out f32vec2 out_uv;

void main() {    
    out_uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(out_uv * 2.0f - 1.0f, 0.0f, 1.0f);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

f32vec3 get_view_position_from_depth(f32vec2 uv, f32 depth) {
    f32vec4 clipSpacePosition = f32vec4(uv * 2.0 - 1.0, depth, 1.0);
    f32vec4 viewSpacePosition = globals.camera_inverse_projection_matrix * clipSpacePosition;

    viewSpacePosition /= viewSpacePosition.w;

    return viewSpacePosition.xyz;
}

const float rayStep = 0.5f;
const int iterationCount = 50;
const float distanceBias = 0.05f;
const bool enableSSR = false;
const int sampleCount = 4;
const bool isSamplingEnabled = false;
const bool isExponentialStepEnabled = true;
const bool isAdaptiveStepEnabled = true;
const bool isBinarySearchEnabled = true;
const bool debugDraw = false;
const float samplingCoefficient = 0.0;

float random (vec2 uv) {
	return fract(sin(dot(uv, vec2(12.9898, 78.233))) * 43758.5453123); //simple random function
}

vec2 generateProjectedPosition(vec3 pos){
	vec4 samplePosition = globals.camera_projection_matrix * vec4(pos, 1.f);
	samplePosition.xy = (samplePosition.xy / samplePosition.w) * 0.5 + 0.5;
	return samplePosition.xy;
}

f32vec3 ssr(f32vec3 position, f32vec3 reflection) {
    vec3 step = rayStep * reflection;
	vec3 marchingPosition = position + step;
	float delta;
	float depthFromScreen;
	vec2 screenPosition;

    int i = 0;
    for(; i < iterationCount; i++) {
        screenPosition = generateProjectedPosition(marchingPosition);
		depthFromScreen = abs(get_view_position_from_depth(screenPosition, texture(daxa_sampler2D(u_depth_image, globals.linear_sampler), screenPosition).x).z);
		delta = abs(marchingPosition.z) - depthFromScreen;
		if (abs(delta) < distanceBias) {
			return texture(daxa_sampler2D(u_albedo_image, globals.linear_sampler), screenPosition).xyz;
		}

		if (delta > 0) {
			break;
		}

        float directionSign = sign(abs(marchingPosition.z) - depthFromScreen);
        //this is sort of adapting step, should prevent lining reflection by doing sort of iterative converging
        //some implementation doing it by binary search, but I found this idea more cheaty and way easier to implement
        step = step * (1.0 - rayStep * max(directionSign, 0.0));
        marchingPosition += step * (-directionSign);
        step *= 1.05;
    }

    for(; i < iterationCount; i++){
        
        step *= 0.5;
        marchingPosition = marchingPosition - step * sign(delta);
        
        screenPosition = generateProjectedPosition(marchingPosition);
        depthFromScreen = abs(get_view_position_from_depth(screenPosition, texture(daxa_sampler2D(u_depth_image, globals.linear_sampler), screenPosition).x).z);
        delta = abs(marchingPosition.z) - depthFromScreen;
        
        if (abs(delta) < distanceBias) {
            return texture(daxa_sampler2D(u_albedo_image, globals.linear_sampler), screenPosition).xyz;
        }
    }

    return f32vec3(0.0);
}

layout(location = 0) in f32vec2 in_uv;

layout(location = 0) out f32vec4 out_ssr;

void main() {
    f32vec2 roughness_metallic = texture(daxa_sampler2D(u_metallic_roughness_image, globals.linear_sampler), in_uv).xy;

    if(roughness_metallic.y < 0.01f) {
        out_ssr = f32vec4(texture(daxa_sampler2D(u_albedo_image, globals.linear_sampler), in_uv).xyz, 1.0f);
        return;
    }

    f32vec3 view_space_position = get_view_position_from_depth(in_uv, texture(daxa_sampler2D(u_depth_image, globals.linear_sampler), in_uv).r);
    f32vec3 view_space_normal = f32vec3(globals.camera_view_matrix * f32vec4(texture(daxa_sampler2D(u_normal_image, globals.linear_sampler), in_uv).xyz, 0.0));
    f32vec3 reflection_direction = normalize(reflect(view_space_position, normalize(view_space_normal)));
    out_ssr = f32vec4(ssr(view_space_position, reflection_direction), 1.0);
    if(out_ssr == f32vec4(0.0)) {
        out_ssr = f32vec4(texture(daxa_sampler2D(u_albedo_image, globals.linear_sampler), in_uv).xyz, 1.0f);
    }
}

#endif
#endif