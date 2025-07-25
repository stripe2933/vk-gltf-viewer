module;

#include <cstddef>

export module vk_gltf_viewer.vulkan.shader_type.Material;

import std;
export import glm;

namespace vk_gltf_viewer::vulkan::shader_type {
    export struct Material {
        std::uint8_t baseColorTexcoordIndex;
        std::uint8_t metallicRoughnessTexcoordIndex;
        std::uint8_t normalTexcoordIndex;
        std::uint8_t occlusionTexcoordIndex;
        std::uint8_t emissiveTexcoordIndex;
        char padding0[1];
        std::uint16_t baseColorTextureIndex = 0;
        std::uint16_t metallicRoughnessTextureIndex = 0;
        std::uint16_t normalTextureIndex = 0;
        std::uint16_t occlusionTextureIndex = 0;
        std::uint16_t emissiveTextureIndex = 0;
        glm::vec4 baseColorFactor = { 1.f, 1.f, 1.f, 1.f };
        float metallicFactor = 1.f;
        float roughnessFactor = 1.f;
        float normalScale = 1.f;
        float occlusionStrength = 1.f;
        glm::vec3 emissive = { 0.f, 0.f, 0.f }; // emissiveStrength * emissiveFactor
        float alphaCutOff;
        glm::mat3x2 baseColorTextureTransform = { 1.f, 0.f, 0.f, 1.f, 0.f, 0.f };
        glm::mat3x2 metallicRoughnessTextureTransform = { 1.f, 0.f, 0.f, 1.f, 0.f, 0.f };
        glm::mat3x2 normalTextureTransform = { 1.f, 0.f, 0.f, 1.f, 0.f, 0.f };
        glm::mat3x2 occlusionTextureTransform = { 1.f, 0.f, 0.f, 1.f, 0.f, 0.f };
        glm::mat3x2 emissiveTextureTransform = { 1.f, 0.f, 0.f, 1.f, 0.f, 0.f };
        float ior = 1.5;
        char padding1[4];
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

static_assert(sizeof(vk_gltf_viewer::vulkan::shader_type::Material) == 192);
static_assert(offsetof(vk_gltf_viewer::vulkan::shader_type::Material, baseColorTextureIndex) == 6);
static_assert(offsetof(vk_gltf_viewer::vulkan::shader_type::Material, baseColorFactor) == 16);
static_assert(offsetof(vk_gltf_viewer::vulkan::shader_type::Material, emissive) == 48);
static_assert(offsetof(vk_gltf_viewer::vulkan::shader_type::Material, baseColorTextureTransform) == 64);