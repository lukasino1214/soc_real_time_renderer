#pragma once
#include <pch.hpp>

template <typename T> 
struct ScrollingBuffer {
    ScrollingBuffer(u32 _capacity = 2000) : capacity{_capacity} {
        data = std::make_unique<T[]>(capacity);
    }

    void push(const T &v) {
        data[offset] = v;
        offset = (offset + 1) % capacity;
        if (size < capacity) { size++; }
    }

    auto get_current() -> T& { return data[static_cast<u32>(std::max(static_cast<i32>(offset) - 1, 0))]; }
    void clear() { std::fill_n(data.get(), capacity, T{}); }

    u32 capacity;
    u32 offset = 0;
    u32 size = 0;
    std::unique_ptr<T[]> data;
};