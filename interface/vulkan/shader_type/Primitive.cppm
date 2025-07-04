module;

#include <cstddef>

export module vk_gltf_viewer.vulkan.shader_type.Primitive;

import std;
export import vulkan_hpp;

namespace vk_gltf_viewer::vulkan::shader_type {
    export struct Primitive {
        vk::DeviceAddress pPositionBuffer;
        vk::DeviceAddress pPositionMorphTargetAccessorBuffer;
        vk::DeviceAddress pNormalBuffer;
        vk::DeviceAddress pNormalMorphTargetAccessorBuffer;
        vk::DeviceAddress pTangentBuffer;
        vk::DeviceAddress pTangentMorphTargetAccessorBuffer;
        vk::DeviceAddress pColorBuffer;
        vk::DeviceAddress pTexcoordAttributeMappingInfoBuffer;
        vk::DeviceAddress pJointsAttributeMappingInfoBuffer;
        vk::DeviceAddress pWeightsAttributeMappingInfoBuffer;
        std::uint8_t positionByteStride;
        std::uint8_t normalByteStride;
        std::uint8_t tangentByteStride;
        std::uint8_t colorByteStride;
        std::uint32_t materialIndex;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

static_assert(sizeof(vk_gltf_viewer::vulkan::shader_type::Primitive) % 8 == 0);
static_assert(offsetof(vk_gltf_viewer::vulkan::shader_type::Primitive, positionByteStride) == 80);
static_assert(offsetof(vk_gltf_viewer::vulkan::shader_type::Primitive, materialIndex) == 84);