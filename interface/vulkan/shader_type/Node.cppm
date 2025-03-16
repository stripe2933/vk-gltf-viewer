module;

#include <cstddef>

export module vk_gltf_viewer:vulkan.shader_type.Node;

import std;

namespace vk_gltf_viewer::vulkan::shader_type {
    export struct Node {
        std::uint32_t instancedTransformStartIndex;
        std::uint32_t morphTargetWeightStartIndex;
        std::uint32_t skinJointIndexStartIndex;
        std::uint32_t _padding_;
    };

    static_assert(sizeof(Node) == 16);
    static_assert(offsetof(Node, instancedTransformStartIndex) == 0);
    static_assert(offsetof(Node, morphTargetWeightStartIndex) == 4);
    static_assert(offsetof(Node, skinJointIndexStartIndex) == 8);
}