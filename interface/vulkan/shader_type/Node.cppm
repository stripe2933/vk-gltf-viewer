module;

#include <cstddef>

export module vk_gltf_viewer.vulkan.shader_type.Node;

import std;
export import glm;
export import vulkan_hpp;

namespace vk_gltf_viewer::vulkan::shader_type {
    export struct Node {
        glm::mat4 worldTransform;
        vk::DeviceAddress pInstancedWorldTransformBuffer;
        vk::DeviceAddress pMorphTargetWeightBuffer;
        vk::DeviceAddress pSkinJointIndexBuffer;
        vk::DeviceAddress pInverseBindMatrixBuffer;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

static_assert(sizeof(vk_gltf_viewer::vulkan::shader_type::Node) % 16 == 0);
static_assert(offsetof(vk_gltf_viewer::vulkan::shader_type::Node, pInstancedWorldTransformBuffer) == 64);
static_assert(offsetof(vk_gltf_viewer::vulkan::shader_type::Node, pMorphTargetWeightBuffer) == 72);