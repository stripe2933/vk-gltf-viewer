module;

#include <alignment.hpp>

export module vkgltf.bindless.shader_type.Node;

import std;
export import fastgltf;
export import vulkan_hpp;

namespace vkgltf::shader_type {
    export struct Node {
        fastgltf::math::fmat4x4 worldTransform;
        vk::DeviceAddress pInstancedWorldTransformBuffer;
        vk::DeviceAddress pMorphTargetWeightBuffer;
        vk::DeviceAddress pSkinJointIndexBuffer;
        vk::DeviceAddress pInverseBindMatrixBuffer;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

static_assert(sizeof(vkgltf::shader_type::Node) == 96);
ASSERT_ALIGNMENT(vkgltf::shader_type::Node, pInstancedWorldTransformBuffer);
ASSERT_ALIGNMENT(vkgltf::shader_type::Node, pMorphTargetWeightBuffer);
ASSERT_ALIGNMENT(vkgltf::shader_type::Node, pSkinJointIndexBuffer);
ASSERT_ALIGNMENT(vkgltf::shader_type::Node, pInverseBindMatrixBuffer);