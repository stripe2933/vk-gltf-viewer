module;

#include <cstddef>
#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.buffer.Primitives;

import std;
export import fastgltf;
import :gltf.OrderedPrimitives;
import :helpers.concepts;
import :helpers.fastgltf;
import :helpers.functional;
export import :vulkan.buffer.PrimitiveAttributes;
export import :vulkan.buffer.StagingBufferStorage;
export import :vulkan.Gpu;
import :vulkan.shader_type.Primitive;
import :vulkan.trait.PostTransferObject;

#define FWD(...) static_cast<decltype(__VA_ARGS__) &&>(__VA_ARGS__)
#define LIFT(...) [&](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

namespace vk_gltf_viewer::vulkan::buffer {
    export class Primitives : trait::PostTransferObject {
    public:
        Primitives(
            const gltf::OrderedPrimitives &orderedPrimitives,
            const PrimitiveAttributes &primitiveAttributes,
            const Gpu &gpu,
            StagingBufferStorage &stagingBufferStorage
        ) : PostTransferObject { stagingBufferStorage },
            buffer { createBuffer(orderedPrimitives, primitiveAttributes, gpu.allocator) },
            descriptorInfo { visit_as<vk::Buffer>(buffer), 0, vk::WholeSize }{ }

        [[nodiscard]] const vk::DescriptorBufferInfo &getDescriptorInfo() const noexcept {
            return descriptorInfo;
        }

        /**
         * @brief Update \p primitive's material index inside the GPU buffer.
         * @param primitiveIndex Index of the primitive.
         * @param materialIndex New material index.
         * @param transferCommandBuffer If buffer is not host-visible memory and so is unable to be updated from the host, this command buffer will be used for recording the buffer update command. Then, its execution MUST be synchronized to be available to the <tt>primitiveBuffer</tt>'s usage. Otherwise, this parameter is not used.
         * @return <tt>true</tt> if the buffer is not host-visible memory and the update command is recorded, <tt>false</tt> otherwise.
         */
        bool updateMaterial(
            std::size_t primitiveIndex,
            std::uint32_t materialIndex,
            vk::CommandBuffer transferCommandBuffer
        ) {
            return std::visit(multilambda {
                [&](vku::MappedBuffer &primitiveBuffer) {
                    primitiveBuffer.asRange<shader_type::Primitive>()[primitiveIndex].materialIndex = materialIndex + 1;
                    return false;
                },
                [&](vk::Buffer primitiveBuffer) {
                    transferCommandBuffer.updateBuffer<std::uint32_t>(
                        primitiveBuffer,
                        sizeof(shader_type::Primitive) * primitiveIndex + offsetof(shader_type::Primitive, materialIndex),
                        materialIndex + 1);
                    return true;
                }
            }, buffer);
        }

    private:
        std::variant<vku::AllocatedBuffer, vku::MappedBuffer> buffer;
        vk::DescriptorBufferInfo descriptorInfo;

        [[nodiscard]] std::variant<vku::AllocatedBuffer, vku::MappedBuffer> createBuffer(
            const gltf::OrderedPrimitives &orderedPrimitives,
            const PrimitiveAttributes &primitiveAttributes,
            vma::Allocator allocator
        ) const {
            vku::MappedBuffer buffer {
                allocator,
                std::from_range, orderedPrimitives | std::views::transform([&](const fastgltf::Primitive *pPrimitive) {
                    const auto &accessors = primitiveAttributes.getAccessors(*pPrimitive);
                    shader_type::Primitive result {
                        .pPositionBuffer = accessors.positionAccessor.bufferAddress,
                        .pTexcoordAttributeMappingInfoBuffer = accessors.texcoordAccessorBufferAddress,
                        .pJointsAttributeMappingInfoBuffer = accessors.jointsAccessorBufferAddress,
                        .pWeightsAttributeMappingInfoBuffer = accessors.weightsAccessorBufferAddress,
                        .positionByteStride = accessors.positionAccessor.byteStride,
                        .materialIndex = to_optional(pPrimitive->materialIndex)
                            .transform([](auto index) { return static_cast<std::uint32_t>(index) + 1U; })
                            .value_or(0U),
                    };
                    if (!accessors.positionMorphTargetAccessors.empty()) {
                        result.pPositionMorphTargetAccessorBuffer = accessors.positionMorphTargetAccessorBufferAddress;
                    }
                    if (accessors.normalAccessor) {
                        result.pNormalBuffer = accessors.normalAccessor->bufferAddress;
                        result.normalByteStride = accessors.normalAccessor->byteStride;

                        if (!accessors.normalMorphTargetAccessors.empty()) {
                            result.pNormalMorphTargetAccessorBuffer = accessors.normalMorphTargetAccessorBufferAddress;
                        }
                    }
                    if (accessors.tangentAccessor) {
                        result.pTangentBuffer = accessors.tangentAccessor->bufferAddress;
                        result.tangentByteStride = accessors.tangentAccessor->byteStride;

                        if (!accessors.tangentMorphTargetAccessors.empty()) {
                            result.pTangentMorphTargetAccessorBuffer = accessors.tangentMorphTargetAccessorBufferAddress;
                        }
                    }
                    if (accessors.colorAccessor) {
                        result.pColorBuffer = accessors.colorAccessor->bufferAddress;
                        result.colorByteStride = accessors.colorAccessor->byteStride;
                    }

                    return result;
                }),
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
            };

            // If staging doesn't have to be done, preserve the mapped state.
            if (!StagingBufferStorage::needStaging(buffer)) {
                return std::variant<vku::AllocatedBuffer, vku::MappedBuffer> { std::in_place_type<vku::MappedBuffer>, std::move(buffer) };
            }

            vku::AllocatedBuffer unmappedBuffer = std::move(buffer).unmap();
            stagingBufferStorage.get().stage(unmappedBuffer, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst);
            return unmappedBuffer;
        }
    };
}