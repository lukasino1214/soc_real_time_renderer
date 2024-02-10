#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>
#include <daxa/utils/task_graph.inl>

#include "../shared.inl"

#if __cplusplus || defined(ResolveLuminanceHistogram_SHADER)

DAXA_DECL_TASK_USES_BEGIN(ResolveLuminanceHistogram, 2)
DAXA_TASK_USE_BUFFER(u_auto_exposure_buffer, daxa_BufferPtr(AutoExposure), COMPUTE_SHADER_READ_WRITE)
DAXA_DECL_TASK_USES_END()

#endif

#if __cplusplus
#include "../../context.hpp"


struct ResolveLuminanceHistogramTask {
    DAXA_USE_TASK_HEADER(ResolveLuminanceHistogram)

    inline static const daxa::ComputePipelineCompileInfo PIPELINE_COMPILE_INFO = {
        .shader_info = daxa::ShaderCompileInfo{
            .source = daxa::ShaderFile{"src/graphics/tasks/resolve_luminance_histogram.inl"},
            .compile_options = { .defines = { { std::string{ResolveLuminanceHistogramTask::NAME} + "_SHADER", "1" } } }
        },
        .name = std::string{ResolveLuminanceHistogramTask::NAME}
    };

    Context* context = {};
    
    void callback(daxa::TaskInterface ti) {
        auto cmd = ti.get_command_list();
        context->gpu_metrics[name]->start(cmd);
        cmd.set_uniform_buffer(context->shader_globals_set_info);
        cmd.set_uniform_buffer(ti.uses.get_uniform_buffer_info());
        cmd.set_pipeline(*context->compute_pipelines.at(PIPELINE_COMPILE_INFO.name));

        cmd.dispatch(256, 1, 1);
        context->gpu_metrics[name]->end(cmd);
    }
};
#endif

#if defined(ResolveLuminanceHistogram_SHADER)
#extension GL_EXT_debug_printf : enable

layout(local_size_x = AUTO_EXPOSURE_BIN_COUNT) in;
shared u32 shared_buckets[AUTO_EXPOSURE_BIN_COUNT];

f32 remap(float val, float start1, float end1, float start2, float end2) {
    return (val - start1) / (end1 - start1) * (end2 - start2) + start2;
}

void main() {
    const u32 local_id = gl_GlobalInvocationID.x;
    const u32 bin_count = deref(u_auto_exposure_buffer).histogram_buckets[local_id];
    shared_buckets[local_id] = bin_count * local_id;
    deref(u_auto_exposure_buffer).histogram_buckets[local_id] = 0;
    barrier();

    u32 threshold = AUTO_EXPOSURE_BIN_COUNT / 2;
    for(i32 i = i32(log2(AUTO_EXPOSURE_BIN_COUNT)); i > 0; i--) {
        if(local_id < threshold) {
            shared_buckets[local_id] += shared_buckets[local_id + threshold];
        }
        threshold /= 2;
        barrier();
    }

    if(local_id == 0) {
        const u32 num_black_pixels = bin_count;
        const f32 log2_mean_luminance = remap(f32(shared_buckets[0]) / max(f32(globals.resolution.x * globals.resolution.y) - num_black_pixels, 1.0), 1.0, 
        AUTO_EXPOSURE_BIN_COUNT, globals.log_min_luminance, globals.log_max_luminance);
        
        const f32 exposure_target = log2(globals.target_luminance / exp2(log2_mean_luminance));
        const f32 alpha = clamp(1 - exp(-globals.delta_time * globals.adjustment_speed), 0.0, 1.0);
        deref(u_auto_exposure_buffer).exposure = mix(deref(u_auto_exposure_buffer).exposure, exposure_target, alpha);
    }
}

#endif