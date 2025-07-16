module;

#include <alignment.hpp>

export module vkgltf.bindless.shader_type.Primitive;

import std;
export import vulkan_hpp;

export import vkgltf.bindless.shader_type.Accessor;

namespace vkgltf::shader_type {
    export struct Primitive {
        Accessor positionAccessor;
        Accessor normalAccessor;
        Accessor tangentAccessor;
        std::array<Accessor, 4> texcoordAccessors;
        Accessor color0Accessor;
        vk::DeviceAddress positionMorphTargetAccessorBufferDeviceAddress;
        vk::DeviceAddress normalMorphTargetAccessorBufferDeviceAddress;
        vk::DeviceAddress tangentMorphTargetAccessorBufferDeviceAddress;
        vk::DeviceAddress jointAccessorBufferDeviceAddress;
        vk::DeviceAddress weightAccessorBufferDeviceAddress;
        std::int32_t materialIndex;
        char _padding[4];
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

static_assert(sizeof(vkgltf::shader_type::Primitive) == 176);
ASSERT_ALIGNMENT(vkgltf::shader_type::Primitive, positionAccessor);
ASSERT_ALIGNMENT(vkgltf::shader_type::Primitive, normalAccessor);
ASSERT_ALIGNMENT(vkgltf::shader_type::Primitive, tangentAccessor);
// ASSERT_ALIGNMENT(vkgltf::shader_type::Primitive, texcoordAccessors);
static_assert(offsetof(vkgltf::shader_type::Primitive, texcoordAccessors) % 16 == 0);
ASSERT_ALIGNMENT(vkgltf::shader_type::Primitive, color0Accessor);
ASSERT_ALIGNMENT(vkgltf::shader_type::Primitive, positionMorphTargetAccessorBufferDeviceAddress);
ASSERT_ALIGNMENT(vkgltf::shader_type::Primitive, normalMorphTargetAccessorBufferDeviceAddress);
ASSERT_ALIGNMENT(vkgltf::shader_type::Primitive, tangentMorphTargetAccessorBufferDeviceAddress);
ASSERT_ALIGNMENT(vkgltf::shader_type::Primitive, jointAccessorBufferDeviceAddress);
ASSERT_ALIGNMENT(vkgltf::shader_type::Primitive, weightAccessorBufferDeviceAddress);
ASSERT_ALIGNMENT(vkgltf::shader_type::Primitive, materialIndex);