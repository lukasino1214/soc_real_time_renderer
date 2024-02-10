#include "context.hpp"


Context::Context(const AppWindow &window)
    : instance{daxa::create_instance({})},
        device{instance.create_device(daxa::DeviceInfo{
            .enable_buffer_device_address_capture_replay = true,
            .name = "my device"})},
        swapchain{window.create_swapchain(this->device)},
        pipeline_manager{daxa::PipelineManagerInfo{
            .device = device,
            .shader_compile_options = {
                .root_paths = {
                    DAXA_SHADER_INCLUDE_DIR,
                    "./",
                },
                .language = daxa::ShaderLanguage::GLSL,
                .enable_debug_info = true,
            },
            .name = "pipeline_manager",
        }},
        transient_mem{daxa::TransferMemoryPoolInfo{
            .device = this->device,
            .capacity = 4096,
            .name = "transient memory pool",
        }},
        shader_global_block{}, 
        shader_globals_buffer{device.create_buffer(daxa::BufferInfo{
            .size = static_cast<u32>(((static_cast<i32>(sizeof(ShaderGlobalsBlock)) + 256 - 1) / 256) * 256 * swapchain.info().max_allowed_frames_in_flight),
            .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_SEQUENTIAL_WRITE | daxa::MemoryFlagBits::DEDICATED_MEMORY,
            .name = "globals",
        })},
        shader_globals_set_info{} {
    gpu_metric_pool = std::make_unique<GPUMetricPool>(device);
}

Context::~Context() {
    device.destroy_buffer(shader_globals_buffer);
}
