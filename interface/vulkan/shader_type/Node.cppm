module;

#include <cstddef>

export module vk_gltf_viewer:vulkan.shader_type.Node;

import std;
export import glm;
export import vulkan_hpp;

namespace vk_gltf_viewer::vulkan::shader_type {
    export struct Node {
        glm::mat4 worldTransform;
        vk::DeviceAddress pInstancedWorldTransforms;
        std::uint32_t morphTargetWeightStartIndex;
        std::uint32_t skinJointIndexStartIndex;
    };

    static_assert(sizeof(Node) == 80);
    static_assert(offsetof(Node, pInstancedWorldTransforms) == 64);
    static_assert(offsetof(Node, morphTargetWeightStartIndex) == 72);
    static_assert(offsetof(Node, skinJointIndexStartIndex) == 76);
}