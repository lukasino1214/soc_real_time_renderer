#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>
#include <daxa/utils/task_graph.inl>

#if __cplusplus || defined(SunShadowDraw_SHADER)
#include "../shared.inl"

DAXA_DECL_TASK_USES_BEGIN(SunShadowDraw, 2)
DAXA_TASK_USE_IMAGE(u_depth_image, REGULAR_2D, DEPTH_ATTACHMENT)
DAXA_DECL_TASK_USES_END()

struct SunShadowDrawPush {
    daxa_BufferPtr(Vertex) vertices;
};

#endif


#if __cplusplus
#include "../../context.hpp"
#include "../../ecs/scene.hpp"
#include "../../ecs/entity.hpp"
#include "../../ecs/components.hpp"


struct SunShadowDrawTask {
    DAXA_USE_TASK_HEADER(SunShadowDraw)

    inline static const daxa::RasterPipelineCompileInfo PIPELINE_COMPILE_INFO = daxa::RasterPipelineCompileInfo {
        .vertex_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/sun_shadow_draw.inl" }, },
            .compile_options = { .defines = { { std::string{SunShadowDrawTask::NAME} + "_SHADER", "1" } } }
        },
        .fragment_shader_info = daxa::ShaderCompileInfo {
            .source = daxa::ShaderSource { daxa::ShaderFile { .path = "src/graphics/tasks/sun_shadow_draw.inl" }, },
            .compile_options = { .defines = { { std::string{SunShadowDrawTask::NAME} + "_SHADER", "1" } } }
        },
        .depth_test = {
            .depth_attachment_format = daxa::Format::D32_SFLOAT,
            .enable_depth_test = true,
            .enable_depth_write = true,
            .depth_test_compare_op = daxa::CompareOp::LESS_OR_EQUAL
        },
        .raster = {
            .face_culling = daxa::FaceCullFlagBits::BACK_BIT,
            .depth_bias_enable = true,
            .depth_bias_constant_factor = 1.25f, 
            .depth_bias_clamp = 0.0f,
            .depth_bias_slope_factor = 1.75f
        },
        .push_constant_size = sizeof(SunShadowDrawPush),
        .name = std::string{SunShadowDrawTask::NAME}
    };

    Context* context = {};
    Scene* scene {};
    
    void callback(daxa::TaskInterface ti) {
        auto cmd = ti.get_command_list();
        context->gpu_metrics[name]->start(cmd);

        u32 size_x = ti.get_device().info_image(uses.u_depth_image.image()).size.x;
        u32 size_y = ti.get_device().info_image(uses.u_depth_image.image()).size.y;

        cmd.begin_renderpass( daxa::RenderPassBeginInfo {
            .depth_attachment = {{
                .image_view = uses.u_depth_image.view(),
                .load_op = daxa::AttachmentLoadOp::CLEAR,
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
                cmd.set_depth_bias(daxa::DepthBiasInfo { .constant_factor = 1.25f, .clamp = 0.0f, .slope_factor = 1.75f });

                auto& mesh = entity.get_component<MeshComponent>();
                for(auto& primitive : mesh.model->primitives) {
                    cmd.push_constant(SunShadowDrawPush {
                        .vertices = ti.get_device().get_device_address(mesh.model->vertex_buffer),
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

#if defined(SunShadowDraw_SHADER)
#include "../shared.inl"

DAXA_DECL_PUSH_CONSTANT(SunShadowDrawPush, push)

#if DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_VERTEX

void main() {    
    const vec4 vertex_position = vec4(deref(push.vertices[gl_VertexIndex]).position, 1);
    gl_Position = globals.sun_info.projection_matrix * globals.sun_info.view_matrix * transform.model_matrix * vertex_position;
}

#elif DAXA_SHADER_STAGE == DAXA_SHADER_STAGE_FRAGMENT

void main() {}

#endif
#endif