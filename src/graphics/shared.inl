#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>

struct PointLight {
    f32vec3 position;
    f32vec3 color;
    f32 intensity;
};

DAXA_DECL_BUFFER_PTR(PointLight)

struct SpotLight {
    f32vec3 position;
    f32vec3 direction;
    f32vec3 color;
    f32 intensity;
    f32 cut_off;
    f32 outer_cut_off;
};

DAXA_DECL_BUFFER_PTR(SpotLight)

struct SunInfo {
    f32mat4x4 projection_matrix;
    f32mat4x4 view_matrix;
    f32mat4x4 projection_view_matrix;
    f32vec4 terrain_y_clip_trick;
    f32vec3 position;
    f32vec3 direction;
    f32 exponential_factor;
    f32 darkening_factor;
    f32 bias;
    f32 intensity;
    daxa_ImageViewId shadow_image;
    daxa_SamplerId shadow_sampler;
};

#define AUTO_EXPOSURE_BIN_COUNT 256
struct AutoExposure {
    f32 exposure;
    u32 histogram_buckets[AUTO_EXPOSURE_BIN_COUNT];
};

DAXA_DECL_BUFFER_PTR(AutoExposure)

struct ShaderGlobals {
    f32mat4x4 camera_projection_matrix;
    f32mat4x4 camera_inverse_projection_matrix;
    f32mat4x4 camera_view_matrix;
    f32mat4x4 camera_inverse_view_matrix;
    f32mat4x4 camera_projection_view_matrix;
    f32mat4x4 camera_inverse_projection_view_matrix;

    f32mat4x4 camera_previous_projection_matrix;
    f32mat4x4 camera_previous_inverse_projection_matrix;
    f32mat4x4 camera_previous_view_matrix;
    f32mat4x4 camera_previous_inverse_view_matrix;
    f32mat4x4 camera_previous_projection_view_matrix;
    f32mat4x4 camera_previous_inverse_projection_view_matrix;

    f32vec2 jitter;
    f32vec2 previous_jitter;

    f32vec3 camera_position;
    f32 camera_near_clip;
    f32 camera_far_clip;

    daxa_SamplerId linear_sampler;
    daxa_SamplerId nearest_sampler;
    i32vec2 resolution;
    f32 elapsed_time;
    f32 delta_time;
    u32 frame_counter;

    // configs ....

    // sun info
    SunInfo sun_info;

    // light
    u32 point_light_count;
    u32 spot_light_count;
    PointLight point_lights[128];
    SpotLight spot_lights[128];

    // terrain
    f32vec3 terrain_offset;
    f32vec2 terrain_scale;
    f32 terrain_height_scale;
    f32 terrain_midpoint;
    f32 terrain_delta;
    f32 terrain_min_depth;
    f32 terrain_max_depth;
    i32 terrain_min_tess_level;
    i32 terrain_max_tess_level;
    f32vec4 terrain_y_clip_trick;
    f32vec4 terrain_previous_y_clip_trick;

    // bloom
    f32 filter_radius;
    
    // ssao
    f32 ssao_bias;
    f32 ssao_radius;
    i32 ssao_kernel_size;

    // composition
    f32vec3 ambient;
    f32 ambient_occlussion_strength;
    f32 emissive_bloom_strength;

    // depth of field
    daxa_SamplerId depth_of_field_sampler;
    f32 focal_length;
    f32 plane_in_focus;
    f32 aperture;

    // auto exposure
    f32 adjustment_speed;
    f32 log_min_luminance;
    f32 log_max_luminance;
    f32 target_luminance;
    daxa_BufferPtr(AutoExposure) auto_exposure_buffer_ptr;

    // tone mapping
    f32 saturation;
    f32 agxDs_linear_section;
    f32 peak;
    f32 compression;
};

DAXA_DECL_BUFFER_PTR(ShaderGlobals)

#define SHADER_GLOBALS_SLOT 0

DAXA_DECL_UNIFORM_BUFFER(SHADER_GLOBALS_SLOT) ShaderGlobalsBlock {
    ShaderGlobals globals;
};

struct TransformInfo {
    f32mat4x4 model_matrix;
    f32mat4x4 normal_matrix;
};

#define TRANSFORM_INFO_SLOT 1

DAXA_DECL_UNIFORM_BUFFER(TRANSFORM_INFO_SLOT) TransformInfoBlock {
    TransformInfo transform;
};

struct TextureId {
    daxa_ImageViewId image_id;
    daxa_SamplerId sampler_id;
};

#define sample_texture(tex, uv) texture(daxa_sampler2D(tex.image_id, tex.sampler_id), uv)

struct Material {
    TextureId albedo_image;
    i32 has_albedo_image;
    TextureId metallic_roughness_image;
    i32 has_metallic_roughness_image;
    TextureId normal_image;
    i32 has_normal_image;
    TextureId occlusion_image;
    i32 has_occlusion_image;
    TextureId emissive_image;
    i32 has_emissive_image;
};

DAXA_DECL_BUFFER_PTR(Material)

struct Primitive {
    u32 first_index;
    u32 first_vertex;
    u32 index_count;
    u32 vertex_count;
    u32 material_index;
};

struct Vertex {
    f32vec3 position;
    f32vec3 normal;
    f32vec2 uv;
    f32vec4 tangent;
};

DAXA_DECL_BUFFER_PTR(Vertex)