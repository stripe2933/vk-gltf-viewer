module;

#include <cstddef>

export module vk_gltf_viewer:vulkan.shader_type.Primitive;

import std;
export import glm;
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
        char _padding0[8];
        glm::vec3 min;
        char _padding1[4];
        glm::vec3 max;
        char _padding2[4];
    };

    static_assert(sizeof(Primitive) == 128);
    static_assert(offsetof(Primitive, positionByteStride) == 80);
    static_assert(offsetof(Primitive, materialIndex) == 84);
    static_assert(offsetof(Primitive, min) == 96);
    static_assert(offsetof(Primitive, max) == 112);
}