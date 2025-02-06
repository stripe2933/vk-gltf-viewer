module;

#include <cstddef>

export module vk_gltf_viewer:vulkan.shader_type.Node;

import std;
export import vulkan_hpp;

namespace vk_gltf_viewer::vulkan::shader_type {
    export struct Node {
        vk::DeviceAddress morphTargetWeightsStartAddress;
        std::uint32_t morphTargetWeightsCount;
        std::uint32_t instancedTransformStartIndex;
    };

    static_assert(sizeof(Node) == 16);
    static_assert(offsetof(Node, morphTargetWeightsStartAddress) == 0);
    static_assert(offsetof(Node, morphTargetWeightsCount) == 8);
    static_assert(offsetof(Node, instancedTransformStartIndex) == 12);
}