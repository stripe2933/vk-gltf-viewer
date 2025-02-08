module;

#include <cstddef>

export module vk_gltf_viewer:vulkan.shader_type.Node;

import std;

namespace vk_gltf_viewer::vulkan::shader_type {
    export struct Node {
        std::uint32_t instancedTransformStartIndex;
        std::uint32_t morphTargetWeightStartIndex;
    };

    static_assert(sizeof(Node) == 8);
    static_assert(offsetof(Node, instancedTransformStartIndex) == 0);
    static_assert(offsetof(Node, morphTargetWeightStartIndex) == 4);
}