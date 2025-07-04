module;

#include <cstddef>

export module vk_gltf_viewer.vulkan.shader_type.Accessor;

import std;
export import vulkan_hpp;

namespace vk_gltf_viewer::vulkan::shader_type {
    /**
     * @brief Representation of <tt>fastgltf::Accessor</tt> in GPU-compatible layout.
     *
     * This have buffer device address where the data is started, data's component type and byte stride.
     */
    export struct Accessor {
        vk::DeviceAddress bufferAddress;
        std::uint32_t componentType;
        std::uint32_t byteStride;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

static_assert(sizeof(vk_gltf_viewer::vulkan::shader_type::Accessor) == 16);
static_assert(offsetof(vk_gltf_viewer::vulkan::shader_type::Accessor, componentType) == 8);