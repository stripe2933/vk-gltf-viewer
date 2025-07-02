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
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

static_assert(sizeof(vk_gltf_viewer::vulkan::shader_type::Accessor) == 16);
static_assert(offsetof(vk_gltf_viewer::vulkan::shader_type::Accessor, componentType) == 8);
static_assert(offsetof(vk_gltf_viewer::vulkan::shader_type::Accessor, componentCount) == 9);
static_assert(offsetof(vk_gltf_viewer::vulkan::shader_type::Accessor, byteStride) == 10);