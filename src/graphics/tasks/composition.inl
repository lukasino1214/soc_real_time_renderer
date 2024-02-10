#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>
#include <daxa/utils/task_graph.inl>

#include "../shared.inl"

#if __cplusplus || defined(Composition_SHADER)

DAXA_DECL_TASK_USES_BEGIN(Composition, 2)
DAXA_TASK_USE_IMAGE(u_target_image, REGULAR_2D, COLOR_ATTACHMENT)
DAXA_TASK_USE_IMAGE(u_albedo_image, REGULAR_2D, FRAGMENT_SHADER_SAMPLED)
DAXA_TASK_USE_IMAGE(u_emissive_image, REGULAR_2D, FRAGMENT_SHADER_SAMPLED)
DAXA_TASK_USE_IMAGE(u_normal_image, REGULAR_2D, FRAGMENT_SHADER_SAMPLED)
DAXA_TASK_USE_IMAGE(u_depth_image, REGULAR_2D, FRAGMENT_SHADER_SAMPLED)
DAXA_TASK_USE_IMAGE(u_ssao_image, REGULAR_2D, FRAGMENT_SHADER_SAMPLED)
DAXA_TASK_USE_IMAGE(u_shadow_image, REGULAR_2D, FRAGMENT_SHADER_SAMPLED)
DAXA_TASK_USE_IMAGE(u_ssr_image, REGULAR_2D, FRAGMENT_SHADER_SAMPLED)
DAXA_TASK_USE_IMAGE(u_metallic_roughness_image, REGULAR_2D, FRAGMENT_SHADER_SAMPLED)
DAXA_TASK_USE_IMAGE(u_clouds_image, REGULAR_2D, FRAGMENT_SHADER_SAMPLED)
DAXA_DECL_TASK_USES_END()

#endif

#if __cplusplus
#include "../../context.hpp"


struct CompositionTask {
    DAXA_USE_TASK_HEADER(Composition)

    inline static const daxa::RasterPipelineCompileInfo PIPELINE_COMPILE_INFO = daxa::RasterPipelineCompileInfo {
        .vertex_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/composition.inl" }, },
            .compile_options = { .defines = { { std::string{CompositionTask::NAME} + "_SHADER", "1" } } }
        },
        .fragment_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/composition.inl" }, },
            .compile_options = { .defines = { { std::string{CompositionTask::NAME} + "_SHADER", "1" } } }
        },
        .color_attachments = { { .format = daxa::Format::R16G16B16A16_SFLOAT }},
        .depth_test = {
            .depth_attachment_format = daxa::Format::D32_SFLOAT,
            .enable_depth_test = false,
            .enable_depth_write = false,
            .depth_test_compare_op = daxa::CompareOp::LESS_OR_EQUAL
        },
        .raster = {
            .face_culling = daxa::FaceCullFlagBits::NONE
        },
        .name = std::string{CompositionTask::NAME}
    };

    Context* context = {};
    
    void callback(daxa::TaskInterface ti) {
        auto cmd = ti.get_command_list();
        context->gpu_metrics[name]->start(cmd);

        u32 size_x = ti.get_device().info_image(uses.u_albedo_image.image()).size.x;
        u32 size_y = ti.get_device().info_image(uses.u_albedo_image.image()).size.y;

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

#if defined(Composition_SHADER)
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

const i32 NUM_STEPS_INT = 2;
const f32 NUM_STEPS = f32(NUM_STEPS_INT);
const f32 G = 0.7f;
const f32 PI = 3.14159265359f;
const mat4 DITHER_PATTERN = mat4
    (f32vec4(0.0f, 0.5f, 0.125f, 0.625f),
     f32vec4(0.75f, 0.22f, 0.875f, 0.375f),
     f32vec4(0.1875f, 0.6875f, 0.0625f, 0.5625f),
     f32vec4(0.9375f, 0.4375f, 0.8125f, 0.3125f));

f32 calculate_scattering(f32 cos_theta) {
    return (1.0 - G * G) / (4.0 * PI * pow(1.0 + G * G - 2.0 * G * cos_theta, 1.5));
}

f32vec3 get_world_position_from_depth(f32vec2 uv, f32 depth) {
    f32vec4 clip_space_position = f32vec4(uv * 2.0 - 1.0, depth, 1.0);
    f32vec4 view_space_position = globals.camera_inverse_projection_matrix * clip_space_position;

    view_space_position /= view_space_position.w;
    f32vec4 world_space_position = globals.camera_inverse_view_matrix * view_space_position;

    return world_space_position.xyz;
}

f32vec3 calculate_point_light(PointLight light, f32vec3 frag_color, f32vec3 normal, f32vec3 position, f32vec3 camera_position) {
    f32vec3 frag_position = position.xyz;
    f32vec3 light_dir = normalize(light.position - frag_position);

    f32 distance = length(light.position.xyz - frag_position);
    f32 attenuation = 1.0f / (distance * distance);

    f32vec3 view_dir = normalize(camera_position - frag_position);
    f32vec3 halfway_dir = normalize(light_dir + view_dir);

    f32 diffuse = max(dot(normal, light_dir), 0);
    f32 normal_half = acos(dot(halfway_dir, normal));
    f32 exponent = normal_half * 1.0;
    exponent = -(exponent * exponent);
    return frag_color * light.color * (diffuse + exp(exponent)) * attenuation * light.intensity;
}

f32vec3 calculate_spot_light(SpotLight light, f32vec3 frag_color, f32vec3 normal, f32vec3 position, f32vec3 camera_position) {
    f32vec3 frag_position = position.xyz;
    f32vec3 light_dir = normalize(light.position - frag_position);

    f32 theta = dot(light_dir, normalize(-light.direction)); 
    f32 epsilon = (light.cut_off - light.outer_cut_off);
    f32 intensity = clamp((theta - light.outer_cut_off) / epsilon, 0, 1.0);

    f32 distance = length(light.position - frag_position);
    f32 attenuation = 1.0 / (distance * distance); 

    f32vec3 view_dir = normalize(camera_position - frag_position);
    f32vec3 halfway_dir = normalize(light_dir + view_dir);

    f32 diffuse = max(dot(normal, light_dir), 0);
    f32 normal_half = acos(dot(halfway_dir, normal));
    f32 exponent = normal_half / 1.0;
    exponent = -(exponent * exponent);
    return frag_color * light.color * (diffuse + exp(exponent)) * attenuation * light.intensity * intensity;
}

void main() {
    // setup for shadow
    const f32 depth = texture(daxa_sampler2D(u_depth_image, globals.linear_sampler), in_uv).r;
    const f32vec3 vertex_position = get_world_position_from_depth(in_uv, depth);
    const f32vec4 shadow_position = globals.sun_info.projection_matrix * globals.sun_info.view_matrix * f32vec4(vertex_position, 1.0);
    

    // calculate shadow
    f32vec3 proj_coord = shadow_position.xyz / shadow_position.w;
    proj_coord = f32vec3(proj_coord.xy * 0.5 + 0.5, proj_coord.z);
    const f32 shadow_depth = texture(daxa_sampler2D(u_shadow_image, globals.linear_sampler), proj_coord.xy).r;
    const f32 sun_shadow = clamp(pow(exp(globals.sun_info.exponential_factor * (proj_coord.z - shadow_depth)), globals.sun_info.darkening_factor), 0.0f, 1.0f);


    // volumetrics
    f32vec4 shadow_camera_position = globals.sun_info.projection_matrix * globals.sun_info.view_matrix * f32vec4(globals.camera_position, 1.0);
    shadow_camera_position.xyz /= shadow_camera_position.w;

    f32vec3 V = (shadow_position.xyz / shadow_position.w) - shadow_camera_position.xyz;
    f32 stepSize = length(V) / NUM_STEPS;
    V = normalize(V);
    f32vec3 step = V * stepSize;
 
    f32vec3 position = shadow_camera_position.xyz;
    f32 dither_value = DITHER_PATTERN[i32(gl_FragCoord.x) % 4][i32(gl_FragCoord.y) % 4];
 
    float accum_fog = 0.0;
    for (u32 i = 0; i < NUM_STEPS_INT; ++i) {
        f32vec3 clip_space_step = shadow_camera_position.xyz + step * f32(i) + dither_value * step;
        accum_fog += texture(daxa_sampler2DShadow(globals.sun_info.shadow_image, globals.sun_info.shadow_sampler), f32vec3(clip_space_step.xy * 0.5f + 0.5f, clip_space_step.z)).r;
    }

    V = normalize(vertex_position - globals.camera_position);
    f32vec3 volumetric = vec3((accum_fog / NUM_STEPS) * calculate_scattering(dot(V, -globals.sun_info.direction)));
    volumetric = f32vec3(0.0);
    // retrieve from g buffer
    f32vec3 emissive = texture(daxa_sampler2D(u_emissive_image, globals.linear_sampler), in_uv).rgb * globals.emissive_bloom_strength;
    f32vec3 albedo = texture(daxa_sampler2D(u_albedo_image, globals.linear_sampler), in_uv).rgb;
    f32vec3 normal = texture(daxa_sampler2D(u_normal_image, globals.linear_sampler), in_uv).rgb;
    f32 occlusion = pow(texture(daxa_sampler2D(u_ssao_image, globals.linear_sampler), in_uv).r, globals.ambient_occlussion_strength);

    f32vec3 direct = f32vec3(max(0.0, dot(normal, -globals.sun_info.direction)) * sun_shadow);

    for(u32 i = 0; i < globals.point_light_count; i++) {
        direct += calculate_point_light(globals.point_lights[i], albedo, normal, vertex_position, globals.camera_position);
    }

    for(u32 i = 0; i < globals.spot_light_count; i++) {
        direct += calculate_spot_light(globals.spot_lights[i], albedo, normal, vertex_position, globals.camera_position);
    }

    // f32vec2 roughness_metallic = texture(daxa_sampler2D(u_metallic_roughness_image, globals.linear_sampler), in_uv).xy;
    // f32vec3 reflected_albedo = texture(daxa_sampler2D(u_ssr_image, globals.linear_sampler), in_uv).rgb;

    // albedo = mix(albedo, reflected_albedo, roughness_metallic.y * (1.0 - roughness_metallic.x));

    f32vec3 color = (direct + globals.ambient) * albedo * occlusion + volumetric + emissive;

    if(depth == 1.0f) {
        color = texture(daxa_sampler2D(u_clouds_image, globals.linear_sampler), in_uv).rgb;
    }

    out_color = f32vec4(color, 1.0);
}

#endif
#endif