#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>
#include <daxa/utils/task_graph.inl>

#include "../shared.inl"

#if __cplusplus || defined(GBufferGeneration_SHADER)

DAXA_DECL_TASK_USES_BEGIN(GBufferGeneration, 2)
DAXA_TASK_USE_IMAGE(u_albedo_image, REGULAR_2D, COLOR_ATTACHMENT)
DAXA_TASK_USE_IMAGE(u_emissive_image, REGULAR_2D, COLOR_ATTACHMENT)
DAXA_TASK_USE_IMAGE(u_normal_image, REGULAR_2D, COLOR_ATTACHMENT)
DAXA_TASK_USE_IMAGE(u_metallic_roughness_image, REGULAR_2D, COLOR_ATTACHMENT)
DAXA_TASK_USE_IMAGE(u_velocity_image, REGULAR_2D, COLOR_ATTACHMENT)
DAXA_TASK_USE_IMAGE(u_depth_image, REGULAR_2D, DEPTH_ATTACHMENT)
DAXA_DECL_TASK_USES_END()

struct GBufferGenerationPush {
    daxa_BufferPtr(Vertex) vertices;
    daxa_BufferPtr(Material) material;
};

#endif

#if __cplusplus
#include "../../context.hpp"
#include "../../ecs/scene.hpp"
#include "../../ecs/entity.hpp"
#include "../../ecs/components.hpp"


struct GBufferGenerationTask {
    DAXA_USE_TASK_HEADER(GBufferGeneration)

    inline static const daxa::RasterPipelineCompileInfo PIPELINE_COMPILE_INFO = daxa::RasterPipelineCompileInfo {
        .vertex_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/g_buffer_generation.inl" }, },
            .compile_options = { .defines = { { std::string{GBufferGenerationTask::NAME} + "_SHADER", "1" } } }
        },
        .fragment_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/g_buffer_generation.inl" }, },
            .compile_options = { .defines = { { std::string{GBufferGenerationTask::NAME} + "_SHADER", "1" } } }
        },
        .color_attachments = {
            { .format = daxa::Format::R16G16B16A16_SFLOAT },
            { .format = daxa::Format::R16G16B16A16_SFLOAT },
            { .format = daxa::Format::R16G16B16A16_SFLOAT },
            { .format = daxa::Format::R16G16B16A16_SFLOAT },
            { .format = daxa::Format::R16G16B16A16_SFLOAT },
        },
        .depth_test = {
            .depth_attachment_format = daxa::Format::D32_SFLOAT,
            .enable_depth_test = true,
            .enable_depth_write = false,
            .depth_test_compare_op = daxa::CompareOp::LESS_OR_EQUAL
        },
        .raster = {
            .face_culling = daxa::FaceCullFlagBits::FRONT_BIT
        },
        .push_constant_size = sizeof(GBufferGenerationPush),
        .name = std::string{GBufferGenerationTask::NAME}
    };

    Context* context = {};
    Scene* scene {};
    
    void callback(daxa::TaskInterface ti) {
        auto cmd = ti.get_command_list();
        context->gpu_metrics[name]->start(cmd);

        u32 size_x = ti.get_device().info_image(uses.u_albedo_image.image()).size.x;
        u32 size_y = ti.get_device().info_image(uses.u_albedo_image.image()).size.y;

        cmd.begin_renderpass( daxa::RenderPassBeginInfo {
            .color_attachments = { 
                daxa::RenderAttachmentInfo {
                    .image_view = uses.u_albedo_image.view(),
                    .load_op = daxa::AttachmentLoadOp::CLEAR,
                    .clear_value = std::array<f32, 4>{0.2f, 0.4f, 1.0f, 1.0f},
                },
                daxa::RenderAttachmentInfo {
                    .image_view = uses.u_emissive_image.view(),
                    .load_op = daxa::AttachmentLoadOp::CLEAR,
                    .clear_value = std::array<f32, 4>{0.f, 0.f, 0.f, 1.0f},
                },
                daxa::RenderAttachmentInfo {
                    .image_view = uses.u_normal_image.view(),
                    .load_op = daxa::AttachmentLoadOp::CLEAR,
                    .clear_value = std::array<f32, 4>{0.f, 0.f, 0.f, 1.0f},
                },
                daxa::RenderAttachmentInfo {
                    .image_view = uses.u_metallic_roughness_image.view(),
                    .load_op = daxa::AttachmentLoadOp::CLEAR,
                    .clear_value = std::array<f32, 4>{0.f, 0.f, 0.f, 1.0f},
                },
                daxa::RenderAttachmentInfo {
                    .image_view = uses.u_velocity_image.view(),
                    .load_op = daxa::AttachmentLoadOp::CLEAR,
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

        scene->iterate([&](Entity entity){
            if(entity.has_component<MeshComponent>() && entity.has_component<TransformComponent>()) {
                cmd.set_uniform_buffer(context->shader_globals_set_info);
                cmd.set_uniform_buffer(entity.get_component<TransformComponent>().buffer_info);
                cmd.set_uniform_buffer(ti.uses.get_uniform_buffer_info());
                cmd.set_pipeline(*context->raster_pipelines.at(PIPELINE_COMPILE_INFO.name));

                auto& mesh = entity.get_component<MeshComponent>();
                for(auto& primitive : mesh.model->primitives) {
                    cmd.push_constant(GBufferGenerationPush {
                        .vertices = ti.get_device().get_device_address(mesh.model->vertex_buffer),
                        .material = ti.get_device().get_device_address(mesh.model->material_buffer) + primitive.material_index * sizeof(Material),
                    });

                    if(primitive.index_count > 0) {
                        cmd.set_index_buffer(mesh.model->index_buffer, 0);
                        cmd.draw_indexed({
                            .index_count = primitive.index_count,
                            .instance_count = 1,
                            .first_index = primitive.first_index,
                            .vertex_offset = static_cast<i32>(primitive.first_vertex),
                            .first_instance = 0,
                        });
                    } else {
                        cmd.draw({
                            .vertex_count = primitive.vertex_count,
                            .instance_count = 1,
                            .first_vertex = primitive.first_vertex,
                            .first_instance = 0
                        });
                    }
                }
            }
        });

        cmd.end_renderpass();
        context->gpu_metrics[name]->end(cmd);
    }
};
#endif

#if defined(GBufferGeneration_SHADER)
#include "../shared.inl"

DAXA_DECL_PUSH_CONSTANT(GBufferGenerationPush, push)

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

layout(location = 0) out f32vec2 out_uv;
layout(location = 1) out f32vec3 out_normal;
layout(location = 2) out f32vec3 out_position;
layout(location = 3) out f32vec4 out_current_position_clip;
layout(location = 4) out f32vec4 out_previous_position_clip;

void main() {    
    out_uv = deref(push.vertices[gl_VertexIndex]).uv;
    out_normal = normalize(f32mat3x3(transform.normal_matrix) * deref(push.vertices[gl_VertexIndex]).normal);
    const f32vec4 vertex_position = transform.model_matrix * vec4(deref(push.vertices[gl_VertexIndex]).position, 1);
    out_position = vertex_position.xyz;
    out_current_position_clip = globals.camera_projection_matrix * globals.camera_view_matrix * vertex_position;
    out_previous_position_clip = globals.camera_previous_projection_matrix * globals.camera_previous_view_matrix * vertex_position;
    gl_Position = out_current_position_clip;
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

layout(location = 0) in f32vec2 in_uv;
layout(location = 1) in f32vec3 in_normal;
layout(location = 2) in f32vec3 in_position;
layout(location = 3) in f32vec4 in_current_position_clip;
layout(location = 4) in f32vec4 in_previous_position_clip;

layout(location = 0) out f32vec4 out_albedo;
layout(location = 1) out f32vec4 out_emissive;
layout(location = 2) out f32vec4 out_normal;
layout(location = 3) out f32vec4 out_metallic_roughness;
layout(location = 4) out f32vec4 out_velocity;

void main() {
    f32vec3 emissive = f32vec3(0.0f);
    if(deref(push.material).has_emissive_image == 1) { emissive = sample_texture(deref(push.material).emissive_image, in_uv).rgb; }
    out_emissive = f32vec4(emissive, 1.0f);

    out_albedo = f32vec4(sample_texture(deref(push.material).albedo_image, in_uv).rgb + emissive, 1.0f);
    

    f32vec3 normal = normalize(in_normal);
    if(deref(push.material).has_normal_image == 1) {
        f32vec3 tangent_normal = sample_texture(deref(push.material).normal_image, in_uv).xyz * 2.0 - 1.0;

        f32vec3 Q1  = dFdx(in_position);
        f32vec3 Q2  = dFdy(in_position);
        f32vec2 st1 = dFdx(in_uv);
        f32vec2 st2 = dFdy(in_uv);

        f32vec3 N = normalize(normal);

        f32vec3 T  = normalize(Q1*st2.t - Q2*st1.t);
        f32vec3 B  = normalize(cross(N, T));
        mat3 TBN = mat3(T, B, N);

        normal = normalize(TBN * tangent_normal);
    }

    out_normal = f32vec4(normal, 1.0f);

    f32vec2 metallic_roughness = f32vec2(0.0f);
    if(deref(push.material).has_metallic_roughness_image == 1) {
        // gltf spec channel G - roughness and B - metallic
        // I mapped the roughness to R and metallic to B channel
        metallic_roughness = sample_texture(deref(push.material).metallic_roughness_image, in_uv).gb;
    }

    out_metallic_roughness = f32vec4(metallic_roughness , 0.0f, 1.0f);

    f32vec2 previous_position_div = f32vec2((in_previous_position_clip.xy / in_previous_position_clip.w) * 0.5 + 0.5);
    f32vec2 currrent_position_div = f32vec2((in_current_position_clip.xy / in_current_position_clip.w) * 0.5 + 0.5);
    f32vec2 velocity = currrent_position_div - previous_position_div;
    out_velocity = f32vec4(velocity, 0.0f, 1.0f);
}

#endif
#endif