module;

#include <cstddef>

export module vk_gltf_viewer:vulkan.shader_type.Material;

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
        glm::vec3 emissiveFactor = { 0.f, 0.f, 0.f };
        float alphaCutOff;
        glm::mat3x2 baseColorTextureTransform;
        glm::mat3x2 metallicRoughnessTextureTransform;
        glm::mat3x2 normalTextureTransform;
        glm::mat3x2 occlusionTextureTransform;
        glm::mat3x2 emissiveTextureTransform;
        float ior = 1.5;
        char padding1[4];
    };

    static_assert(sizeof(Material) == 192);
    static_assert(offsetof(Material, baseColorTextureIndex) == 6);
    static_assert(offsetof(Material, baseColorFactor) == 16);
    static_assert(offsetof(Material, emissiveFactor) == 48);
    static_assert(offsetof(Material, baseColorTextureTransform) == 64);
}