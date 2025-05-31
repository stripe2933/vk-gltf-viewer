module;

#include <cstddef>

export module vk_gltf_viewer.vulkan.shader_type.Accessor;

import std;
export import vulkan_hpp;

namespace vk_gltf_viewer::vulkan::shader_type {
    /**
     * @brief Representation of <tt>fastgltf::Accessor</tt> in GPU-compatible layout.
     *
     * This have buffer device address where the data is started, data's component type, component count, and byte stride.
     */
    export struct Accessor {
        vk::DeviceAddress bufferAddress;
        std::uint8_t componentType;
        std::uint8_t componentCount;
        std::uint8_t byteStride;
        char _padding_[5];
    };

    static_assert(sizeof(Accessor) == 16);
    static_assert(offsetof(Accessor, componentType) == 8);
    static_assert(offsetof(Accessor, componentCount) == 9);
    static_assert(offsetof(Accessor, byteStride) == 10);
}