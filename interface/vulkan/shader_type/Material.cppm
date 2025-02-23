module;

#include <cstddef>

export module vk_gltf_viewer:vulkan.shader_type.Material;

import std;
export import glm;

namespace vk_gltf_viewer::vulkan::shader_type {
    export struct Material {
        std::uint32_t baseColorPackedTextureInfo;
        std::uint32_t metallicRoughnessPackedTextureInfo;
        std::uint32_t normalPackedTextureInfo;
        std::uint32_t occlusionPackedTextureInfo;
        std::uint32_t emissivePackedTextureInfo;
        float metallicFactor = 1.f;
        float roughnessFactor = 1.f;
        float normalScale = 1.f;
        glm::vec4 baseColorFactor = { 1.f, 1.f, 1.f, 1.f };
        glm::vec3 emissiveFactor = { 0.f, 0.f, 0.f };
        float occlusionStrength = 1.f;
        float alphaCutOff;
        char _padding_[4];
        glm::mat3x2 baseColorTextureTransform;
        glm::mat3x2 metallicRoughnessTextureTransform;
        glm::mat3x2 normalTextureTransform;
        glm::mat3x2 occlusionTextureTransform;
        glm::mat3x2 emissiveTextureTransform;
    };

    static_assert(sizeof(Material) == 192);
    static_assert(offsetof(Material, baseColorFactor) == 32);
    static_assert(offsetof(Material, emissiveFactor) == 48);
    static_assert(offsetof(Material, baseColorTextureTransform) == 72);
}