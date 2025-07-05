module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer.vulkan.buffer.Primitives;

import std;
export import fastgltf;
export import vku;

import vk_gltf_viewer.helpers.ranges;
export import vk_gltf_viewer.vulkan.buffer.PrimitiveAttributes;
export import vk_gltf_viewer.vulkan.shader_type.Primitive;

namespace vk_gltf_viewer::vulkan::buffer {
    export class Primitives final : public vku::AllocatedBuffer {
    public:
        Primitives(
            const fastgltf::Asset &asset,
            const PrimitiveAttributes &primitiveAttributes,
            vma::Allocator allocator
        );

        /**
         * @brief Get index of the primitive stored in the buffer.
         * @param primitive Primitive to get the index of.
         * @return Index of the primitive in the buffer.
         */
        [[nodiscard]] std::size_t getPrimitiveIndex(const fastgltf::Primitive &primitive) const;

        /**
         * @brief Get primitive from index.
         * @param index Index of the primitive to get.
         * @return Reference to the primitive at the given index.
         */
        [[nodiscard]] const fastgltf::Primitive &getPrimitive(std::size_t index) const noexcept;

        template <auto shader_type::Primitive::*accessor>
        bool update(
            const fastgltf::Primitive &primitive,
            const std::remove_cvref_t<std::invoke_result_t<decltype(accessor), shader_type::Primitive&>> &data,
            vk::CommandBuffer transferCommandBuffer
        ) {
            // Obtain byte offset and size of the field to be updated.
            static constexpr shader_type::Primitive dummy{};
            constexpr auto fieldAddress = &std::invoke(accessor, dummy);
            const vk::DeviceSize byteOffset
                = sizeof(shader_type::Primitive) * getPrimitiveIndex(primitive)
                + reinterpret_cast<std::uintptr_t>(fieldAddress) - reinterpret_cast<std::uintptr_t>(&dummy);
            constexpr vk::DeviceSize byteSize = sizeof(data);
            static_assert(byteSize % 4 == 0 && "Data size bytes must be multiple of 4.");

            const vk::MemoryPropertyFlags memoryPropertyFlags = allocator.getAllocationMemoryProperties(allocation);
            if (vku::contains(memoryPropertyFlags, vk::MemoryPropertyFlagBits::eHostVisible)) {
                // If buffer allocation memory is host-visible, we can update it directly.
                allocator.copyMemoryToAllocation(&data, allocation, byteOffset, byteSize);
                return false;
            }
            else {
                // If buffer allocation memory is not host-visible, we have to record a command to update the buffer.
                transferCommandBuffer.updateBuffer(*this, byteOffset, byteSize, &data);
                return true;
            }
        }

    private:
        std::vector<const fastgltf::Primitive*> orderedPrimitives;
        std::unordered_map<const fastgltf::Primitive*, std::size_t> primitiveIndices;

        Primitives(
            const PrimitiveAttributes &primitiveAttributes,
            vma::Allocator allocator,
            std::vector<const fastgltf::Primitive*> orderedPrimitives
        );
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

[[nodiscard]] std::vector<const fastgltf::Primitive*> createOrderedPrimitives(const fastgltf::Asset &asset) {
    std::vector<const fastgltf::Primitive*> result;
    for (const fastgltf::Mesh &mesh : asset.meshes) {
        for (const fastgltf::Primitive &primitive : mesh.primitives) {
            result.push_back(&primitive);
        }
    }
    return result;
}

vk_gltf_viewer::vulkan::buffer::Primitives::Primitives(
    const fastgltf::Asset &asset,
    const PrimitiveAttributes &primitiveAttributes,
    vma::Allocator allocator
) : Primitives { primitiveAttributes, allocator, createOrderedPrimitives(asset) } { }

std::size_t vk_gltf_viewer::vulkan::buffer::Primitives::getPrimitiveIndex(const fastgltf::Primitive &primitive) const {
    return primitiveIndices.at(&primitive);
}

const fastgltf::Primitive & vk_gltf_viewer::vulkan::buffer::Primitives::getPrimitive(std::size_t index) const noexcept {
    return *orderedPrimitives[index];
}

vk_gltf_viewer::vulkan::buffer::Primitives::Primitives(
    const PrimitiveAttributes &primitiveAttributes,
    vma::Allocator allocator,
    std::vector<const fastgltf::Primitive*> orderedPrimitives
) : AllocatedBuffer {
        allocator,
        vk::BufferCreateInfo {
            {},
            sizeof(shader_type::Primitive) * orderedPrimitives.size(),
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
        },
        vma::AllocationCreateInfo {
            vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped,
            vma::MemoryUsage::eAutoPreferHost,
        },
    },
    orderedPrimitives { std::move(orderedPrimitives) }{
    std::ranges::transform(
        this->orderedPrimitives, static_cast<shader_type::Primitive*>(allocator.getAllocationInfo(allocation).pMappedData),
        [&](const fastgltf::Primitive *primitive) {
            const auto &accessors = primitiveAttributes.getAccessors(*primitive);
            shader_type::Primitive result {
                .pPositionBuffer = accessors.positionAccessor.bufferAddress,
                .pTexcoordAttributeMappingInfoBuffer = accessors.texcoordAccessorBufferAddress,
                .pJointsAttributeMappingInfoBuffer = accessors.jointsAccessorBufferAddress,
                .pWeightsAttributeMappingInfoBuffer = accessors.weightsAccessorBufferAddress,
                .positionByteStride = static_cast<std::uint8_t>(accessors.positionAccessor.byteStride),
            };

            if (!accessors.positionMorphTargetAccessors.empty()) {
                result.pPositionMorphTargetAccessorBuffer = accessors.positionMorphTargetAccessorBufferAddress;
            }
            if (accessors.normalAccessor) {
                result.pNormalBuffer = accessors.normalAccessor->bufferAddress;
                result.normalByteStride = static_cast<std::uint32_t>(accessors.normalAccessor->byteStride);

                if (!accessors.normalMorphTargetAccessors.empty()) {
                    result.pNormalMorphTargetAccessorBuffer = accessors.normalMorphTargetAccessorBufferAddress;
                }
            }
            if (accessors.tangentAccessor) {
                result.pTangentBuffer = accessors.tangentAccessor->bufferAddress;
                result.tangentByteStride = static_cast<std::uint32_t>(accessors.tangentAccessor->byteStride);

                if (!accessors.tangentMorphTargetAccessors.empty()) {
                    result.pTangentMorphTargetAccessorBuffer = accessors.tangentMorphTargetAccessorBufferAddress;
                }
            }
            if (accessors.colorAccessorAndComponentCount) {
                result.pColorBuffer = accessors.colorAccessorAndComponentCount->first.bufferAddress;
                result.colorByteStride = static_cast<std::uint32_t>(accessors.colorAccessorAndComponentCount->first.byteStride);
            }
            if (primitive->materialIndex) {
                result.materialIndex = static_cast<std::uint32_t>(*primitive->materialIndex) + 1U;
            }

            return result;
        });

    const vk::MemoryPropertyFlags memoryPropertyFlags = allocator.getAllocationMemoryProperties(allocation);
    if (!vku::contains(memoryPropertyFlags, vk::MemoryPropertyFlagBits::eHostCoherent)) {
        allocator.flushAllocation(allocation, 0, size);
    }

    for (const auto &[i, primitive] : this->orderedPrimitives | ranges::views::enumerate) {
        primitiveIndices[primitive] = i;
    }
}