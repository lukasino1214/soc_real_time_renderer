#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>
#include <daxa/utils/task_graph.inl>

#include "../shared.inl"

#if __cplusplus || defined(DrawTerrain_SHADER)

DAXA_DECL_TASK_USES_BEGIN(DrawTerrain, 2)
DAXA_TASK_USE_BUFFER(u_vertices, daxa_BufferPtr(f32vec2), VERTEX_SHADER_READ)
DAXA_TASK_USE_BUFFER(u_indices, daxa_BufferPtr(u32), VERTEX_SHADER_READ)
DAXA_TASK_USE_IMAGE(u_terrain_normal_image, REGULAR_2D, FRAGMENT_SHADER_SAMPLED)
DAXA_TASK_USE_IMAGE(u_albedo_image, REGULAR_2D, COLOR_ATTACHMENT)
DAXA_TASK_USE_IMAGE(u_normal_image, REGULAR_2D, COLOR_ATTACHMENT)
DAXA_TASK_USE_IMAGE(u_velocity_image, REGULAR_2D, COLOR_ATTACHMENT)
DAXA_TASK_USE_IMAGE(u_depth_image, REGULAR_2D, DEPTH_ATTACHMENT)
DAXA_DECL_TASK_USES_END()

struct DrawTerrainPush {
    TextureId texture_heightmap;
    TextureId texture_albedomap;
};

#endif

#if __cplusplus
#include "../../context.hpp"
#include "../texture.hpp"


struct DrawTerrainTask {
    DAXA_USE_TASK_HEADER(DrawTerrain)

    inline static const daxa::RasterPipelineCompileInfo PIPELINE_COMPILE_INFO = daxa::RasterPipelineCompileInfo {
        .vertex_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/draw_terrain.inl" }, },
            .compile_options = { .defines = { { std::string{DrawTerrainTask::NAME} + "_SHADER", "1" } } }
        },
        .tesselation_control_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/draw_terrain.inl" }, },
            .compile_options = { .defines = { { std::string{DrawTerrainTask::NAME} + "_SHADER", "1" } } }
        },
        .tesselation_evaluation_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/draw_terrain.inl" }, },
            .compile_options = { .defines = { { std::string{DrawTerrainTask::NAME} + "_SHADER", "1" } } }
        },
        .fragment_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/draw_terrain.inl" }, },
            .compile_options = { .defines = { { std::string{DrawTerrainTask::NAME} + "_SHADER", "1" } } }
        },
        .color_attachments = {
            { .format = daxa::Format::R16G16B16A16_SFLOAT },
            { .format = daxa::Format::R16G16B16A16_SFLOAT },
            { .format = daxa::Format::R16G16B16A16_SFLOAT },
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
            .face_culling = daxa::FaceCullFlagBits::FRONT_BIT
        },
        .tesselation = { .control_points = 4 },
        .push_constant_size = sizeof(DrawTerrainPush),
        .name = std::string{DrawTerrainTask::NAME}
    };

    Context* context = {};
    u32 terrain_index_size = {};
    Texture* terrain_heightmap = {};
    Texture* terrain_albedomap = {};
    
    void callback(daxa::TaskInterface ti) {
        auto cmd = ti.get_command_list();
        context->gpu_metrics[name]->start(cmd);

        u32 size_x = ti.get_device().info_image(uses.u_depth_image.image()).size.x;
        u32 size_y = ti.get_device().info_image(uses.u_depth_image.image()).size.y;

        cmd.begin_renderpass( daxa::RenderPassBeginInfo {
            .color_attachments = { 
                daxa::RenderAttachmentInfo {
                    .image_view = uses.u_albedo_image.view(),
                    .load_op = daxa::AttachmentLoadOp::LOAD,
                    .clear_value = std::array<f32, 4>{0.2f, 0.4f, 1.0f, 1.0f},
                },
                daxa::RenderAttachmentInfo {
                    .image_view = uses.u_normal_image.view(),
                    .load_op = daxa::AttachmentLoadOp::LOAD,
                    .clear_value = std::array<f32, 4>{0.f, 0.f, 0.f, 1.0f},
                },
                daxa::RenderAttachmentInfo {
                    .image_view = uses.u_velocity_image.view(),
                    .load_op = daxa::AttachmentLoadOp::LOAD,
                    .clear_value = std::array<f32, 4>{0.f, 0.f, 0.f, 1.0f},
                },
            },
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
        cmd.push_constant(DrawTerrainPush { 
            .texture_heightmap = terrain_heightmap->get_texture_id(),
            .texture_albedomap = terrain_albedomap->get_texture_id(),
        });
        cmd.draw_indexed({ .index_count =  terrain_index_size });

        cmd.end_renderpass();
        context->gpu_metrics[name]->end(cmd);
    }
};
#endif

#if defined(DrawTerrain_SHADER)
#extension GL_EXT_debug_printf : enable
#include "../shared.inl"

DAXA_DECL_PUSH_CONSTANT(DrawTerrainPush, push)

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out f32vec2 out_uv;

void main() {   
    const f32vec2 uv = deref(u_vertices[gl_VertexIndex]);
    out_uv = uv;
    gl_Position = globals.camera_projection_view_matrix * f32vec4(uv.x * globals.terrain_scale.x - globals.terrain_offset.x, globals.terrain_offset.y, uv.y * globals.terrain_scale.y - globals.terrain_offset.z, 1.0);
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
layout(location = 0) out f32vec2 out_uv;
layout(location = 1) out f32vec3 out_position;
layout(location = 2) out f32vec3 out_normal;

void main() {
    f32 u = gl_TessCoord.x;
    f32 v = gl_TessCoord.y;

    vec4 p0 = (gl_in[1].gl_Position - gl_in[0].gl_Position) * u + gl_in[0].gl_Position;
    vec4 p1 = (gl_in[3].gl_Position - gl_in[2].gl_Position) * u + gl_in[2].gl_Position;
    gl_Position = (p1 - p0) * v + p0;

    f32vec2 uv0 = (in_uv[1] - in_uv[0]) * u + in_uv[0];
    f32vec2 uv1 = (in_uv[3] - in_uv[2]) * u + in_uv[2];
    out_uv = f32vec2((uv1 - uv0) * v + uv0);

    const f32 sampled_height = texture(daxa_sampler2D(push.texture_heightmap.image_id, globals.linear_sampler), f32vec2(out_uv.xy)).r;
    const f32 adjusted_height = (sampled_height - globals.terrain_midpoint) * globals.terrain_height_scale;

    gl_Position += globals.terrain_y_clip_trick * adjusted_height;
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) in f32vec2 in_uv;
layout(location = 1) in f32vec3 in_position;
layout(location = 2) in f32vec3 in_normal;

layout(location = 0) out f32vec4 out_albedo;
layout(location = 1) out f32vec4 out_normal;
layout(location = 2) out f32vec4 out_velocity;

void main() {
    out_albedo = f32vec4(sample_texture(push.texture_albedomap, in_uv).rgb, 1.0f);

    f32vec3 tangent_normal = texture(daxa_sampler2D(u_terrain_normal_image, globals.linear_sampler), f32vec2(in_uv)).xyz;

    f32vec3 Q1  = dFdx(in_position);
    f32vec3 Q2  = dFdy(in_position);
    f32vec2 st1 = dFdx(in_uv);
    f32vec2 st2 = dFdy(in_uv);

    f32vec3 N = normalize(tangent_normal);

    f32vec3 T  = normalize(Q1*st2.t - Q2*st1.t);
    f32vec3 B  = normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);

    out_normal = f32vec4(normalize(tangent_normal), 1.0f);
    out_velocity = f32vec4(0.0f);
}

#endif
#endif
