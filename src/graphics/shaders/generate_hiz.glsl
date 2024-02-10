#extension GL_EXT_debug_printf : enable
#include "../shared.inl"

DAXA_DECL_PUSH_CONSTANT(PUSH, push)

DAXA_DECL_BUFFER_REFERENCE Counter {
    coherent u32 value;
};

layout(local_size_x = GENERATE_HIZ_X, local_size_y = GENERATE_HIZ_Y) in;

DAXA_DECL_IMAGE_ACCESSOR(image2D, coherent, image2DCoherent)

shared bool shared_last_workgroup;
shared f32 shared_mins[2][GENERATE_HIZ_Y][GENERATE_HIZ_X];

void downsample_64x64(u32vec2 local_index, u32vec2 grid_index, u32vec2 min_mip_size, int src_mip, int mip_count) {
    const f32vec2 inv_size = 1.0f / f32vec2(min_mip_size);
    f32vec4 quad_values = f32vec4(0,0,0,0);

    [[unroll]]
    for (u32 quad_i = 0; quad_i < 4; ++quad_i) {
        i32vec2 sub_index = i32vec2(quad_i >> 1, quad_i & 1);
        i32vec2 src_index = i32vec2((grid_index * 16 + local_index) * 2 + sub_index) * 2;
        f32vec4 depth;

        if (src_mip == -1) {
            depth = textureGather(daxa_sampler2D(push.src, globals.linear_sampler), (f32vec2(src_index) + 1.0f) * inv_size, 0);
        } else {
            depth.x = imageLoad(daxa_access(image2DCoherent, push.mips[src_mip]), min(src_index + i32vec2(0,0), i32vec2(min_mip_size) - 1)).x;
            depth.y = imageLoad(daxa_access(image2DCoherent, push.mips[src_mip]), min(src_index + i32vec2(0,1), i32vec2(min_mip_size) - 1)).x;
            depth.z = imageLoad(daxa_access(image2DCoherent, push.mips[src_mip]), min(src_index + i32vec2(1,0), i32vec2(min_mip_size) - 1)).x;
            depth.w = imageLoad(daxa_access(image2DCoherent, push.mips[src_mip]), min(src_index + i32vec2(1,1), i32vec2(min_mip_size) - 1)).x;
        }

        const f32 min_depth = OPERATION(OPERATION(depth.x, depth.y), OPERATION(depth.z, depth.w));
        i32vec2 dst_index = i32vec2((grid_index * 16 + local_index) * 2) + sub_index;

        imageStore(daxa_image2D(push.mips[src_mip + 1]), dst_index, f32vec4(min_depth,0,0,0));
        quad_values[quad_i] = min_depth;
    }

    {
        const f32 min_depth = OPERATION(OPERATION(quad_values.x, quad_values.y), OPERATION(quad_values.z, quad_values.w));
        i32vec2 dst_index = i32vec2(grid_index * 16 + local_index);

        imageStore(daxa_image2D(push.mips[src_mip + 2]), dst_index, f32vec4(min_depth,0,0,0));
        shared_mins[0][local_index.y][local_index.x] = min_depth;
    }

    const u32vec2 global_dst_offset = (u32vec2(GENERATE_HIZ_WINDOW_X,GENERATE_HIZ_WINDOW_Y) * grid_index.xy) / 2;
    
    [[unroll]]
    for (u32 i = 2; i < mip_count; ++i) {
        const u32 ping_pong_src_index = (i & 1u);
        const u32 ping_pong_dst_index = ((i+1) & 1u);

        memoryBarrierShared();
        barrier();

        const bool active_thread = local_index.x < (GENERATE_HIZ_WINDOW_X>>(i+1)) && local_index.y < (GENERATE_HIZ_WINDOW_Y>>(i+1));
        if(active_thread) {
            const u32vec2 global_dst_offset_mip = global_dst_offset >> i;
            const u32vec2 src_index = local_index * 2;
            const f32vec4 depth = f32vec4(
                shared_mins[ping_pong_src_index][src_index.y + 0][src_index.x + 0],
                shared_mins[ping_pong_src_index][src_index.y + 0][src_index.x + 1],
                shared_mins[ping_pong_src_index][src_index.y + 1][src_index.x + 0],
                shared_mins[ping_pong_src_index][src_index.y + 1][src_index.x + 1]
            );

            const f32 min_depth = OPERATION(OPERATION(depth.x, depth.y), OPERATION(depth.z, depth.w));
            const u32 dst_mip = src_mip + i + 1;

            if (dst_mip == 6) {
                imageStore(daxa_access(image2DCoherent, push.mips[dst_mip]), i32vec2(global_dst_offset_mip + local_index), f32vec4(min_depth,0,0,0));
            } else {
                imageStore(daxa_image2D(push.mips[dst_mip]), i32vec2(global_dst_offset_mip + local_index), f32vec4(min_depth,0,0,0));
            }

            shared_mins[ping_pong_dst_index][local_index.y][local_index.x] = min_depth;
        }
    }
}

void main() {
    downsample_64x64(gl_LocalInvocationID.xy, gl_WorkGroupID.xy, globals.resolution, -1, 6);

    if (gl_LocalInvocationID.x == 0 && gl_LocalInvocationID.y == 0) {
        const u32 finished_workgroups = atomicAdd((Counter(push.counter_address)).value, 1) + 1;
        shared_last_workgroup = push.total_workgroup_count == finished_workgroups;
    }

    barrier();

    if (shared_last_workgroup) {
        downsample_64x64(gl_LocalInvocationID.xy, u32vec2(0,0), globals.resolution >> 6, 5, i32(push.mip_count - 6));
    }
}