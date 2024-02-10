#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>
#include <daxa/utils/task_graph.inl>

#include "../shared.inl"

#if __cplusplus || defined(SunShadowDrawTerrain_SHADER)

DAXA_DECL_TASK_USES_BEGIN(SunShadowDrawTerrain, 2)
DAXA_TASK_USE_BUFFER(u_vertices, daxa_BufferPtr(f32vec2), VERTEX_SHADER_READ)
DAXA_TASK_USE_BUFFER(u_indices, daxa_BufferPtr(u32), VERTEX_SHADER_READ)
DAXA_TASK_USE_IMAGE(u_depth_image, REGULAR_2D, DEPTH_ATTACHMENT)
DAXA_DECL_TASK_USES_END()

struct SunShadowDrawTerrainPush {
    TextureId texture_heightmap;
};

#endif

#if __cplusplus
#include "../../context.hpp"
#include "../texture.hpp"


struct SunShadowDrawTerrainTask {
    DAXA_USE_TASK_HEADER(SunShadowDrawTerrain)

    inline static const daxa::RasterPipelineCompileInfo PIPELINE_COMPILE_INFO = daxa::RasterPipelineCompileInfo {
        .vertex_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/sun_shadow_draw_terrain.inl" }, },
            .compile_options = { .defines = { { std::string{SunShadowDrawTerrainTask::NAME} + "_SHADER", "1" } } }
        },
        .tesselation_control_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/sun_shadow_draw_terrain.inl" }, },
            .compile_options = { .defines = { { std::string{SunShadowDrawTerrainTask::NAME} + "_SHADER", "1" } } }
        },
        .tesselation_evaluation_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/sun_shadow_draw_terrain.inl" }, },
            .compile_options = { .defines = { { std::string{SunShadowDrawTerrainTask::NAME} + "_SHADER", "1" } } }
        },
        .fragment_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/sun_shadow_draw_terrain.inl" }, },
            .compile_options = { .defines = { { std::string{SunShadowDrawTerrainTask::NAME} + "_SHADER", "1" } } }
        },
        .depth_test = {
            .depth_attachment_format = daxa::Format::D32_SFLOAT,
            .enable_depth_test = true,
            .enable_depth_write = true,
            .depth_test_compare_op = daxa::CompareOp::LESS_OR_EQUAL
        },
        .raster = {
            .primitive_topology = daxa::PrimitiveTopology::PATCH_LIST,
            .primitive_restart_enable = false,
            .polygon_mode = daxa::PolygonMode::FILL,
            .face_culling = daxa::FaceCullFlagBits::BACK_BIT
        },
        .tesselation = { .control_points = 4 },
        .push_constant_size = sizeof(SunShadowDrawTerrainPush),
        .name = std::string{SunShadowDrawTerrainTask::NAME}
    };

    Context* context = {};
    u32 terrain_index_size = {};
    Texture* terrain_heightmap = {};
    
    void callback(daxa::TaskInterface ti) {
        auto cmd = ti.get_command_list();
        context->gpu_metrics[name]->start(cmd);

        u32 size_x = ti.get_device().info_image(uses.u_depth_image.image()).size.x;
        u32 size_y = ti.get_device().info_image(uses.u_depth_image.image()).size.y;

        cmd.begin_renderpass( daxa::RenderPassBeginInfo {
            .depth_attachment = {{
                .image_view = uses.u_depth_image.view(),
                .load_op = daxa::AttachmentLoadOp::LOAD,
                .clear_value = daxa::DepthValue{1.0f, 0},
            }},
            .render_area = {.x = 0, .y = 0, .width = size_x, .height = size_y},
        });

        cmd.set_uniform_buffer(context->shader_globals_set_info);
        cmd.set_uniform_buffer(ti.uses.get_uniform_buffer_info());
        cmd.set_pipeline(*context->raster_pipelines.at(PIPELINE_COMPILE_INFO.name));
        cmd.set_index_buffer(uses.u_indices.buffer(), 0);
        cmd.push_constant(SunShadowDrawTerrainPush { 
            .texture_heightmap = terrain_heightmap->get_texture_id(),
        });
        cmd.draw_indexed({ .index_count =  terrain_index_size });

        cmd.end_renderpass();
        context->gpu_metrics[name]->end(cmd);
    }
};
#endif

#if defined(SunShadowDrawTerrain_SHADER)
#extension GL_EXT_debug_printf : enable
#include "../shared.inl"

DAXA_DECL_PUSH_CONSTANT(SunShadowDrawTerrainPush, push)

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out f32vec2 out_uv;

void main() {   
    const f32vec2 uv = deref(u_vertices[gl_VertexIndex]);
    out_uv = uv;
    gl_Position = globals.sun_info.projection_view_matrix * f32vec4(uv.x * globals.terrain_scale.x, 0.0, uv.y * globals.terrain_scale.y, 1.0);
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_TESSELATION_CONTROL

layout(location = 0) in f32vec2 in_uv[];
layout(location = 0) out f32vec2 out_uv[];

layout (vertices = 4) out;

void main() {
    if(gl_InvocationID == 0) {
        gl_TessLevelOuter[0] = globals.terrain_max_tess_level;
        gl_TessLevelOuter[1] = globals.terrain_max_tess_level;
        gl_TessLevelOuter[2] = globals.terrain_max_tess_level;
        gl_TessLevelOuter[3] = globals.terrain_max_tess_level;

        gl_TessLevelInner[0] = max(gl_TessLevelOuter[0], gl_TessLevelOuter[2]);
        gl_TessLevelInner[1] = max(gl_TessLevelOuter[1], gl_TessLevelOuter[3]);
    }

    out_uv[gl_InvocationID] = in_uv[gl_InvocationID];
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_TESSELATION_EVALUATION

layout(location = 0) in f32vec2 in_uv[];

layout (quads, fractional_odd_spacing , cw) in;

void main() {
    f32 u = gl_TessCoord.x;
    f32 v = gl_TessCoord.y;

    vec4 p0 = (gl_in[1].gl_Position - gl_in[0].gl_Position) * u + gl_in[0].gl_Position;
    vec4 p1 = (gl_in[3].gl_Position - gl_in[2].gl_Position) * u + gl_in[2].gl_Position;
    gl_Position = (p1 - p0) * v + p0;

    f32vec2 uv0 = (in_uv[1] - in_uv[0]) * u + in_uv[0];
    f32vec2 uv1 = (in_uv[3] - in_uv[2]) * u + in_uv[2];
    f32vec2 out_uv = f32vec2((uv1 - uv0) * v + uv0);

    const f32 sampled_height = texture(daxa_sampler2D(push.texture_heightmap.image_id, globals.linear_sampler), f32vec2(out_uv.xy)).r;
    const f32 adjusted_height = (sampled_height - globals.terrain_midpoint) * globals.terrain_height_scale;

    gl_Position += globals.sun_info.terrain_y_clip_trick * adjusted_height;
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

void main() {}

#endif
#endif
