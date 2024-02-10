#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>
#include <daxa/utils/task_graph.inl>

#if __cplusplus || defined(HeightToNormal_SHADER)

DAXA_DECL_TASK_USES_BEGIN(HeightToNormal, 2)
DAXA_TASK_USE_IMAGE(u_normal_target, REGULAR_2D, COMPUTE_SHADER_STORAGE_WRITE_ONLY)
DAXA_TASK_USE_IMAGE(u_heightmap, REGULAR_2D, COMPUTE_SHADER_STORAGE_READ_ONLY)
DAXA_DECL_TASK_USES_END()

#endif

#if __cplusplus
#include "../../context.hpp"


struct HeightToNormalTask {
    DAXA_USE_TASK_HEADER(HeightToNormal)

    inline static const daxa::ComputePipelineCompileInfo PIPELINE_COMPILE_INFO = {
        .shader_info = daxa::ShaderCompileInfo{
            .source = daxa::ShaderFile{"src/graphics/tasks/height_to_normal.inl"},
            .compile_options = { .defines = { { std::string{HeightToNormalTask::NAME} + "_SHADER", "1" } } }
        },
        .name = std::string{HeightToNormalTask::NAME}
    };

    Context* context = {};

    static constexpr u32 threadsX = 8;
    static constexpr u32 threadsY = 4;

    u32vec2 image_dimension = {};

    void callback(daxa::TaskInterface ti) {
        auto cmd = ti.get_command_list();
        cmd.set_uniform_buffer(context->shader_globals_set_info);
        cmd.set_uniform_buffer(ti.uses.get_uniform_buffer_info());
        cmd.set_pipeline(*context->compute_pipelines.at(PIPELINE_COMPILE_INFO.name));
        cmd.dispatch((image_dimension.x + threadsX - 1) / threadsX, (image_dimension.y + threadsY - 1) / threadsY, 1);
    };
};
#endif

#if defined(HeightToNormal_SHADER)
#include "../shared.inl"

layout (local_size_x = 8, local_size_y = 4) in;

void main() {
    u32vec2 texture_size = textureSize(daxa_sampler2D(u_heightmap, globals.nearest_sampler), 0);
	if(!all(lessThan(gl_GlobalInvocationID.xy, texture_size))) { return; }

    i32vec2 up_pos    = clamp(i32vec2(gl_GlobalInvocationID.xy) + i32vec2(0, 1) , i32vec2(0, 0), i32vec2(texture_size) - i32vec2(1, 1));
    i32vec2 down_pos  = clamp(i32vec2(gl_GlobalInvocationID.xy) + i32vec2(0, -1), i32vec2(0, 0), i32vec2(texture_size) - i32vec2(1, 1));
    i32vec2 right_pos = clamp(i32vec2(gl_GlobalInvocationID.xy) + i32vec2(1, 0) , i32vec2(0, 0), i32vec2(texture_size) - i32vec2(1, 1));
    i32vec2 left_pos  = clamp(i32vec2(gl_GlobalInvocationID.xy) + i32vec2(-1, 0), i32vec2(0, 0), i32vec2(texture_size) - i32vec2(1, 1));

    f32 sample_up    = imageLoad(daxa_image2D(u_heightmap), up_pos).r;    
    f32 sample_down  = imageLoad(daxa_image2D(u_heightmap), down_pos).r;    
    f32 sample_right = imageLoad(daxa_image2D(u_heightmap), right_pos).r;    
    f32 sample_left  = imageLoad(daxa_image2D(u_heightmap), left_pos).r;

    f32vec2 new_up_pos = f32vec2(up_pos) / f32vec2(texture_size);
    f32vec2 new_down_pos = f32vec2(down_pos) / f32vec2(texture_size);
    f32vec2 new_right_pos = f32vec2(right_pos) / f32vec2(texture_size);
    f32vec2 new_left_pos = f32vec2(left_pos) / f32vec2(texture_size);

    f32vec3 norm_pos_up = f32vec3(new_up_pos.x, sample_up, new_up_pos.y);
    f32vec3 norm_pos_down = f32vec3(new_down_pos.x, sample_down, new_down_pos.y);
    f32vec3 norm_pos_right = f32vec3(new_right_pos.x, sample_right, new_right_pos.y);
    f32vec3 norm_pos_left = f32vec3(new_left_pos.x, sample_left, new_left_pos.y);

    f32vec3 vertical_dir = normalize(norm_pos_up - norm_pos_down);
    f32vec3 horizontal_dir = normalize(norm_pos_right - norm_pos_left);

    f32vec3 normal = normalize(cross(vertical_dir, horizontal_dir));

    imageStore(daxa_image2D(u_normal_target), i32vec2(gl_GlobalInvocationID.xy), f32vec4(normal, 1.0));
}

#endif