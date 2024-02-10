#pragma once

#include <daxa/daxa.hpp>
using namespace daxa::types;

struct GPUMetricPool {
    GPUMetricPool(const daxa::Device& _device);
    ~GPUMetricPool();

private:
    friend struct GPUMetric;

    daxa::Device device = {};
    daxa::TimelineQueryPool timeline_query_pool = {};
    u32 query_count = 0;
    f64 timestamp_period = 0.0;
};

struct GPUMetric {
    GPUMetric(GPUMetricPool* _gpu_metric_pool);
    ~GPUMetric();

    void start(daxa::CommandList& cmd_list);
    void end(daxa::CommandList& cmd_list);

    f64 time_elapsed = {};
private:
    GPUMetricPool* gpu_metric_pool = {};
    u32 index = 0;
};