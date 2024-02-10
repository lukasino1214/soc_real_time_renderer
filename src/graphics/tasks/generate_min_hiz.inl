#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>
#include <daxa/utils/task_graph.inl>

#define GENERATE_HIZ_X 16
#define GENERATE_HIZ_Y 16
#define GENERATE_HIZ_LEVELS_PER_DISPATCH 12
#define GENERATE_HIZ_WINDOW_X 64
#define GENERATE_HIZ_WINDOW_Y 64

struct GenerateMinHizPush {
    daxa_ImageViewId src;
    daxa_ImageViewId mips[GENERATE_HIZ_LEVELS_PER_DISPATCH];
    daxa_u32 mip_count;
    daxa_u64 counter_address;
    daxa_u32 total_workgroup_count;
};

#if __cplusplus
#include "../../context.hpp"

struct GenerateMinHIZTask {
    inline static std::string_view NAME = "GenerateMinHIZ";

    inline static const daxa::ComputePipelineCompileInfo PIPELINE_COMPILE_INFO = {
        .shader_info = daxa::ShaderCompileInfo{
            .source = daxa::ShaderFile{"src/graphics/tasks/generate_min_hiz.inl"},
            .compile_options = { .defines = { { std::string{GenerateMinHIZTask::NAME} + "_SHADER", "1" } } }
        },
        .push_constant_size = sizeof(GenerateMinHizPush),
        .name = std::string{GenerateMinHIZTask::NAME}
    };

    static daxa::TaskImageView build(Context* context, daxa::TaskGraph& task_graph, daxa::TaskImageView src_depth) {
        const u32vec2 hiz_size = u32vec2{static_cast<u32>(context->shader_global_block.globals.resolution.x / 2), static_cast<u32>(context->shader_global_block.globals.resolution.y / 2)};
        const u32 mip_count = static_cast<u32>(std::ceil(std::log2(std::max(hiz_size.x, hiz_size.y))));
        daxa::TaskImageView hiz = task_graph.create_transient_image({
            .format = daxa::Format::R32_SFLOAT,
            .size = { hiz_size.x, hiz_size.y, 1 },
            .mip_level_count = mip_count,
            .array_layer_count = 1,
            .sample_count = 1,
            .name = "min hiz",
        });

        using namespace daxa::task_resource_uses;
        const u32 mips_this_dispatch = std::min(mip_count, static_cast<u32>(GENERATE_HIZ_LEVELS_PER_DISPATCH));

        std::vector<daxa::GenericTaskResourceUse> uses = {};
        daxa::TaskImageView src_view = src_depth.view({.base_mip_level = 0});
        uses.push_back(ImageComputeShaderSampled<>{ src_view });
        daxa::TaskImageView dst_views[GENERATE_HIZ_LEVELS_PER_DISPATCH] = { };
        for (u32 i = 0; i < mips_this_dispatch; ++i) {
            dst_views[i] = hiz.view({.base_mip_level = i});
            uses.push_back(ImageComputeShaderStorageWriteOnly<>{ dst_views[i] });                                 
        }

        task_graph.add_task({
            .uses = uses,
            .task = [=](daxa::TaskInterface ti) {
                auto cmd = ti.get_command_list();
                context->gpu_metrics[std::string{GenerateMinHIZTask::NAME}]->start(cmd);
                cmd.set_uniform_buffer(context->shader_globals_set_info);
                cmd.set_uniform_buffer(ti.uses.get_uniform_buffer_info());
                cmd.set_pipeline(*context->compute_pipelines.at(NAME));

                const u32 dispatch_x = (static_cast<u32>(context->shader_global_block.globals.resolution.x) + GENERATE_HIZ_WINDOW_X - 1) / GENERATE_HIZ_WINDOW_X;
                const u32 dispatch_y = (static_cast<u32>(context->shader_global_block.globals.resolution.y) + GENERATE_HIZ_WINDOW_Y - 1) / GENERATE_HIZ_WINDOW_Y;
                auto counter_alloc = ti.get_allocator().allocate(sizeof(u32), sizeof(u32)).value();
                
                *reinterpret_cast<u32*>(counter_alloc.host_address) = 0;
                
                GenerateMinHizPush push { 
                    .src = ti.uses[src_view].view(),
                    .mips = {},
                    .mip_count = mips_this_dispatch,
                    .counter_address = counter_alloc.device_address,
                    .total_workgroup_count = dispatch_x * dispatch_y,
                };

                for (u32 i = 0; i < mips_this_dispatch; ++i) {
                    push.mips[i] = ti.uses[dst_views[i]].view();
                }

                cmd.push_constant(push);
                cmd.dispatch(dispatch_x, dispatch_y, 1);
                context->gpu_metrics[std::string{GenerateMinHIZTask::NAME}]->end(cmd);
            },
            .name = "generate min hiz",
        });

        return hiz.view({.level_count = mip_count});
    }
};
#endif

#if defined(GenerateMinHIZ_SHADER)
#define OPERATION min
#define PUSH GenerateMinHizPush
#include "../shaders/generate_hiz.glsl"
#endif

#undef GENERATE_HIZ_X
#undef GENERATE_HIZ_Y
#undef GENERATE_HIZ_LEVELS_PER_DISPATCH
#undef GENERATE_HIZ_WINDOW_X
#undef GENERATE_HIZ_WINDOW_Y