module;

#include <alignment.hpp>

export module vkgltf.bindless.shader_type.Accessor;

import std;
export import vulkan_hpp;

namespace vkgltf::shader_type {
    export struct Accessor {
        vk::DeviceAddress bufferAddress;
        std::uint32_t componentType;
        std::uint32_t byteStride;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

static_assert(sizeof(vkgltf::shader_type::Accessor) == 16);
ASSERT_ALIGNMENT(vkgltf::shader_type::Accessor, bufferAddress);
ASSERT_ALIGNMENT(vkgltf::shader_type::Accessor, componentType);
ASSERT_ALIGNMENT(vkgltf::shader_type::Accessor, byteStride);