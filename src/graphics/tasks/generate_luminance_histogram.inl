#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>
#include <daxa/utils/task_graph.inl>

#define WORKGROUP_SIZE 32

#include "../shared.inl"

#if __cplusplus || defined(GenerateLuminanceHistogram_SHADER)

DAXA_DECL_TASK_USES_BEGIN(GenerateLuminanceHistogram, 2)
DAXA_TASK_USE_IMAGE(u_hdr_image, REGULAR_2D, COMPUTE_SHADER_SAMPLED)
DAXA_TASK_USE_BUFFER(u_auto_exposure_buffer, daxa_BufferPtr(AutoExposure), COMPUTE_SHADER_WRITE)
DAXA_DECL_TASK_USES_END()

#endif

#if __cplusplus
#include "../../context.hpp"


struct GenerateLuminanceHistogramTask {
    DAXA_USE_TASK_HEADER(GenerateLuminanceHistogram)

    inline static const daxa::ComputePipelineCompileInfo PIPELINE_COMPILE_INFO = {
        .shader_info = daxa::ShaderCompileInfo{
            .source = daxa::ShaderFile{"src/graphics/tasks/generate_luminance_histogram.inl"},
            .compile_options = { .defines = { { std::string{GenerateLuminanceHistogramTask::NAME} + "_SHADER", "1" } } }
        },
        .name = std::string{GenerateLuminanceHistogramTask::NAME}
    };

    Context* context = {};
    
    void callback(daxa::TaskInterface ti) {
        auto cmd = ti.get_command_list();
        context->gpu_metrics[name]->start(cmd);
        cmd.set_uniform_buffer(context->shader_globals_set_info);
        cmd.set_uniform_buffer(ti.uses.get_uniform_buffer_info());
        cmd.set_pipeline(*context->compute_pipelines.at(PIPELINE_COMPILE_INFO.name));

        auto size = ti.get_device().info_image(uses.u_hdr_image.image()).size;
        cmd.dispatch((size.x + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE, (size.y + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE, 1);
        context->gpu_metrics[name]->end(cmd);
    }
};
#endif

#if defined(GenerateLuminanceHistogram_SHADER)

layout(local_size_x = WORKGROUP_SIZE, local_size_y = WORKGROUP_SIZE) in;
shared u32 shared_buckets[AUTO_EXPOSURE_BIN_COUNT];

f32 remap(float val, float start1, float end1, float start2, float end2) {
    return (val - start1) / (end1 - start1) * (end2 - start2) + start2;
}

void main() {
    const u32 local_id = gl_LocalInvocationIndex;
    shared_buckets[local_id] = 0;
    
    barrier();
    u32vec3 global_id = gl_GlobalInvocationID;
    if(all(lessThan(global_id.xy, globals.resolution))) {
        const f32vec3 color = texelFetch(daxa_sampler2D(u_hdr_image, globals.linear_sampler), i32vec2(global_id.xy), 0).xyz;
        f32 luminance = dot(color, f32vec3(0.2126, 0.7152, 0.0722));
        if(luminance < 1e-3) { luminance = 0; }
        const f32 mapped = remap(log2(luminance), globals.log_min_luminance, globals.log_max_luminance, 1.0, f32(AUTO_EXPOSURE_BIN_COUNT - 1));
        const u32 index = clamp(i32(mapped), 0, AUTO_EXPOSURE_BIN_COUNT - 1);
        atomicAdd(shared_buckets[index], 1);
    }

    barrier();
    if(local_id < AUTO_EXPOSURE_BIN_COUNT) {
        atomicAdd(deref(u_auto_exposure_buffer).histogram_buckets[local_id], shared_buckets[local_id]);
    }
}

#endif

#undef WORKGROUP_SIZE