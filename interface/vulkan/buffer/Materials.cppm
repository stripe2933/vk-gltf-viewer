module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.buffer.Materials;

import std;
export import fastgltf;
export import glm;
import :helpers.optional;
import :helpers.ranges;
export import :vulkan.Gpu;

[[nodiscard]] std::pair<glm::mat2, glm::vec2> getTextureTransformMatrixPair(const fastgltf::TextureTransform &transform) noexcept {
    const float c = std::cos(transform.rotation), s = std::sin(transform.rotation);
    return {
        { // Note: column major. A row in code actually means a column in the matrix.
            transform.uvScale[0] * c, transform.uvScale[0] * -s,
            transform.uvScale[1] * s, transform.uvScale[1] * c },
        { transform.uvOffset[0], transform.uvOffset[1] },
    };
}

namespace vk_gltf_viewer::vulkan::buffer {
    export class Materials {
    public:
        struct GpuMaterial {
            std::uint8_t baseColorTexcoordIndex;
            std::uint8_t metallicRoughnessTexcoordIndex;
            std::uint8_t normalTexcoordIndex;
            std::uint8_t occlusionTexcoordIndex;
            std::uint8_t emissiveTexcoordIndex;
            char padding0[1];
            std::int16_t baseColorTextureIndex = -1;
            std::int16_t metallicRoughnessTextureIndex = -1;
            std::int16_t normalTextureIndex = -1;
            std::int16_t occlusionTextureIndex = -1;
            std::int16_t emissiveTextureIndex = -1;
            glm::vec4 baseColorFactor = { 1.f, 1.f, 1.f, 1.f };
            float metallicFactor = 1.f;
            float roughnessFactor = 1.f;
            float normalScale = 1.f;
            float occlusionStrength = 1.f;
            glm::vec3 emissiveFactor = { 0.f, 0.f, 0.f };
            float alphaCutOff;
            glm::mat2 baseColorTextureTransformUpperLeft2x2;
            glm::vec2 baseColorTextureTransformOffset;
            glm::mat2 metallicRoughnessTextureTransformUpperLeft2x2;
            glm::vec2 metallicRoughnessTextureTransformOffset;
            glm::mat2 normalTextureTransformUpperLeft2x2;
            glm::vec2 normalTextureTransformOffset;
            glm::mat2 occlusionTextureTransformUpperLeft2x2;
            glm::vec2 occlusionTextureTransformOffset;
            glm::mat2 emissiveTextureTransformUpperLeft2x2;
            glm::vec2 emissiveTextureTransformOffset;
            char padding1[8];
        };
        static_assert(sizeof(GpuMaterial) == 192);

        Materials(
            const fastgltf::Asset &asset,
            const Gpu &gpu [[clang::lifetimebound]]
        ) : useFallbackMaterialAtZero { determineUseFallbackMaterialAtZero(asset) },
            buffer { createBuffer(asset, gpu) },
            descriptorInfo { buffer, 0, vk::WholeSize } { }

        [[nodiscard]] const vk::DescriptorBufferInfo &getDescriptorInfo() const noexcept {
            return descriptorInfo;
        }

        [[nodiscard]] std::uint32_t padMaterialIndex(std::uint32_t materialIndex) const noexcept {
            return materialIndex + useFallbackMaterialAtZero;
        }

    private:
        bool useFallbackMaterialAtZero;
        vku::AllocatedBuffer buffer;
        vk::DescriptorBufferInfo descriptorInfo;

        [[nodiscard]] static bool determineUseFallbackMaterialAtZero(const fastgltf::Asset &asset) noexcept {
            // If any primitive of the asset is missing the material, we have use the fallback material.
            const auto primitives = asset.meshes | std::views::transform(&fastgltf::Mesh::primitives) | std::views::join;
            for (const fastgltf::Primitive &primitive : primitives) {
                if (!primitive.materialIndex.has_value()) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] vku::AllocatedBuffer createBuffer(
            const fastgltf::Asset &asset,
            const Gpu &gpu
        ) const {
            vku::AllocatedBuffer buffer = vku::MappedBuffer {
                gpu.allocator,
                std::from_range, ranges::views::concat(
                    to_range(value_if(useFallbackMaterialAtZero, []() { return GpuMaterial{}; })), // fallback material if required
                    asset.materials | std::views::transform([&](const fastgltf::Material& material) {
                        GpuMaterial gpuMaterial {
                            .baseColorFactor = glm::gtc::make_vec4(material.pbrData.baseColorFactor.data()),
                            .metallicFactor = material.pbrData.metallicFactor,
                            .roughnessFactor = material.pbrData.roughnessFactor,
                            .emissiveFactor = glm::gtc::make_vec3(material.emissiveFactor.data()),
                            .alphaCutOff = material.alphaCutoff,
                        };

                        if (const auto& baseColorTexture = material.pbrData.baseColorTexture) {
                            gpuMaterial.baseColorTexcoordIndex = baseColorTexture->texCoordIndex;
                            gpuMaterial.baseColorTextureIndex = static_cast<std::int16_t>(baseColorTexture->textureIndex);

                            if (const auto &transform = baseColorTexture->transform) {
                                std::tie(gpuMaterial.baseColorTextureTransformUpperLeft2x2, gpuMaterial.baseColorTextureTransformOffset)
                                    = getTextureTransformMatrixPair(*transform);
                                if (transform->texCoordIndex) {
                                    gpuMaterial.baseColorTexcoordIndex = *transform->texCoordIndex;
                                }
                            }
                        }
                        if (const auto& metallicRoughnessTexture = material.pbrData.metallicRoughnessTexture) {
                            gpuMaterial.metallicRoughnessTexcoordIndex = metallicRoughnessTexture->texCoordIndex;
                            gpuMaterial.metallicRoughnessTextureIndex = static_cast<std::int16_t>(metallicRoughnessTexture->textureIndex);

                            if (const auto &transform = metallicRoughnessTexture->transform) {
                                std::tie(gpuMaterial.metallicRoughnessTextureTransformUpperLeft2x2, gpuMaterial.metallicRoughnessTextureTransformOffset)
                                    = getTextureTransformMatrixPair(*transform);
                                if (transform->texCoordIndex) {
                                    gpuMaterial.metallicRoughnessTexcoordIndex = *transform->texCoordIndex;
                                }
                            }
                        }
                        if (const auto& normalTexture = material.normalTexture) {
                            gpuMaterial.normalTexcoordIndex = normalTexture->texCoordIndex;
                            gpuMaterial.normalTextureIndex = static_cast<std::int16_t>(normalTexture->textureIndex);
                            gpuMaterial.normalScale = normalTexture->scale;

                            if (const auto &transform = normalTexture->transform) {
                                std::tie(gpuMaterial.normalTextureTransformUpperLeft2x2, gpuMaterial.normalTextureTransformOffset)
                                    = getTextureTransformMatrixPair(*transform);
                                if (transform->texCoordIndex) {
                                    gpuMaterial.normalTexcoordIndex = *transform->texCoordIndex;
                                }
                            }
                        }
                        if (const auto& occlusionTexture = material.occlusionTexture) {
                            gpuMaterial.occlusionTexcoordIndex = occlusionTexture->texCoordIndex;
                            gpuMaterial.occlusionTextureIndex = static_cast<std::int16_t>(occlusionTexture->textureIndex);
                            gpuMaterial.occlusionStrength = occlusionTexture->strength;

                            if (const auto &transform = occlusionTexture->transform) {
                                std::tie(gpuMaterial.occlusionTextureTransformUpperLeft2x2, gpuMaterial.occlusionTextureTransformOffset)
                                    = getTextureTransformMatrixPair(*transform);
                                if (transform->texCoordIndex) {
                                    gpuMaterial.occlusionTexcoordIndex = *transform->texCoordIndex;
                                }
                            }
                        }
                        if (const auto& emissiveTexture = material.emissiveTexture) {
                            gpuMaterial.emissiveTexcoordIndex = emissiveTexture->texCoordIndex;
                            gpuMaterial.emissiveTextureIndex = static_cast<std::int16_t>(emissiveTexture->textureIndex);

                            if (const auto &transform = emissiveTexture->transform) {
                                std::tie(gpuMaterial.emissiveTextureTransformUpperLeft2x2, gpuMaterial.emissiveTextureTransformOffset)
                                    = getTextureTransformMatrixPair(*transform);
                                if (transform->texCoordIndex) {
                                    gpuMaterial.emissiveTexcoordIndex = *transform->texCoordIndex;
                                }
                            }
                        }

                        return gpuMaterial;
                    })),
                gpu.isUmaDevice ? vk::BufferUsageFlagBits::eStorageBuffer : vk::BufferUsageFlagBits::eTransferSrc,
            }.unmap();

            if (gpu.isUmaDevice || vku::contains(gpu.allocator.getAllocationMemoryProperties(buffer.allocation), vk::MemoryPropertyFlagBits::eDeviceLocal)) {
                return buffer;
            }

            vku::AllocatedBuffer dstBuffer{ gpu.allocator, vk::BufferCreateInfo {
                {},
                buffer.size,
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
            } };

            const vk::raii::CommandPool transferCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.transfer } };
            const vk::raii::Fence fence { gpu.device, vk::FenceCreateInfo{} };
            vku::executeSingleCommand(*gpu.device, *transferCommandPool, gpu.queues.transfer, [&](vk::CommandBuffer cb) {
                cb.copyBuffer(buffer, dstBuffer, vk::BufferCopy { 0, 0, dstBuffer.size });
            }, *fence);

            std::ignore = gpu.device.waitForFences(*fence, true, ~0ULL); // TODO: failure handling

            return dstBuffer;
        }
    };
}