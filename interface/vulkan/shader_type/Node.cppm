module;

#include <cstddef>

export module vk_gltf_viewer:vulkan.shader_type.Node;

import std;
export import glm;

namespace vk_gltf_viewer::vulkan::shader_type {
    export struct Node {
        glm::mat4 worldTransform;
        std::uint32_t instancedTransformStartIndex;
        std::uint32_t morphTargetWeightStartIndex;
        std::uint32_t skinJointIndexStartIndex;
        std::uint32_t _padding_;
    };

    static_assert(sizeof(Node) == 80);
    static_assert(offsetof(Node, instancedTransformStartIndex) == 64);
    static_assert(offsetof(Node, morphTargetWeightStartIndex) == 68);
    static_assert(offsetof(Node, skinJointIndexStartIndex) == 72);
}