module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.buffer.CombinedIndices;

import std;
export import fastgltf;

import vk_gltf_viewer.gltf.AssetExternalBuffers;
import vk_gltf_viewer.helpers.fastgltf;
import vk_gltf_viewer.helpers.functional;
import vk_gltf_viewer.helpers.type_map;
import vk_gltf_viewer.vulkan.buffer;
export import vk_gltf_viewer.vulkan.Gpu;
export import vk_gltf_viewer.vulkan.buffer.StagingBufferStorage;
import vk_gltf_viewer.vulkan.trait.PostTransferObject;

namespace vk_gltf_viewer::vulkan::buffer {
    export class CombinedIndices : trait::PostTransferObject {
    public:
        CombinedIndices(
            const fastgltf::Asset &asset,
            const Gpu &gpu LIFETIMEBOUND,
            StagingBufferStorage &stagingBufferStorage,
            const gltf::AssetExternalBuffers &adapter
        );

        [[nodiscard]] vk::Buffer getIndexBuffer(vk::IndexType indexType) const;

        /**
         * @brief Get the index information of the primitive.
         * @param primitive Primitive to get the index information.
         * @return Pair of index type and first index.
         */
        [[nodiscard]] std::pair<vk::IndexType, std::uint32_t> getIndexInfo(const fastgltf::Primitive &primitive) const;

    private:
        std::unordered_map<const fastgltf::Primitive*, std::pair<vk::IndexType, std::uint32_t>> indexInfos;
        std::unordered_map<vk::IndexType, vku::AllocatedBuffer> bufferByIndexType;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

constexpr type_map indexTypeMap {
    make_type_map_entry<std::uint8_t>(fastgltf::ComponentType::UnsignedByte),
    make_type_map_entry<std::uint16_t>(fastgltf::ComponentType::UnsignedShort),
    make_type_map_entry<std::uint32_t>(fastgltf::ComponentType::UnsignedInt),
};

[[nodiscard]] vk::IndexType getIndexType(fastgltf::ComponentType componentType) {
    switch (componentType) {
    case fastgltf::ComponentType::UnsignedByte:
        return vk::IndexType::eUint8KHR;
    case fastgltf::ComponentType::UnsignedShort:
        return vk::IndexType::eUint16;
    case fastgltf::ComponentType::UnsignedInt:
        return vk::IndexType::eUint32;
    default:
        // glTF Specification:
        // The indices accessor MUST have SCALAR type and an unsigned integer component type.
        throw std::invalid_argument { "Invalid component type for index buffer." };
    }
}

[[nodiscard]] std::size_t getIndexTypeSize(vk::IndexType indexType) {
    switch (indexType) {
    case vk::IndexType::eUint8KHR:
        return sizeof(std::uint8_t);
    case vk::IndexType::eUint16:
        return sizeof(std::uint16_t);
    case vk::IndexType::eUint32:
        return sizeof(std::uint32_t);
    default:
        throw std::invalid_argument { "Invalid index type." };
    }
}

vk_gltf_viewer::vulkan::buffer::CombinedIndices::CombinedIndices(
    const fastgltf::Asset &asset,
    const Gpu &gpu,
    StagingBufferStorage &stagingBufferStorage,
    const gltf::AssetExternalBuffers &adapter
) : PostTransferObject { stagingBufferStorage } {
    auto primitives = asset.meshes
        | std::views::transform(&fastgltf::Mesh::primitives)
        | std::views::join;

    // Primitives that are having an indices accessor.
    auto indexedPrimitives = primitives | std::views::filter([](const fastgltf::Primitive &primitive) {
        return primitive.indicesAccessor.has_value();
    });

    // Primitives whose type is LINE_LOOP.
    auto lineLoopPrimitives = primitives | std::views::filter([](const fastgltf::Primitive &primitive) {
        // As GL_LINE_LOOP does not supported in Vulkan natively, it should be emulated as line strip, with
        // additional first vertex at the end, using indexed draw.
        return primitive.type == fastgltf::PrimitiveType::LineLoop;
    });

    // Index data is either
    // - span of the buffer view region, or
    // - generated indices if accessor data layout couldn't be represented in Vulkan buffer.
    std::vector<std::unique_ptr<std::byte[]>> generatedIndexBytes;
    std::unordered_map<vk::IndexType, std::vector<std::pair<const fastgltf::Primitive*, std::span<const std::byte>>>> indexBufferBytesByType;

    // Get buffer view bytes from indexedPrimitives and group them by index type.
    for (const fastgltf::Primitive &primitive : indexedPrimitives) {
        const fastgltf::Accessor &accessor = asset.accessors[*primitive.indicesAccessor];
        if (!accessor.sparse && accessor.bufferViewIndex &&
            (accessor.componentType != fastgltf::ComponentType::UnsignedByte || gpu.supportUint8Index)) {
            indexBufferBytesByType[getIndexType(accessor.componentType)]
                .emplace_back(&primitive, getByteRegion(asset, accessor, adapter));
        }
        else {
            // Copy accessor data as uint16_t if GPU does not support VK_KHR_index_type_uint8.
            fastgltf::ComponentType componentType = accessor.componentType;
            if (componentType == fastgltf::ComponentType::UnsignedByte && !gpu.supportUint8Index) {
                componentType = fastgltf::ComponentType::UnsignedShort;
            }

            visit([&]<typename T>(std::type_identity<T>) {
                const std::size_t dataSize = sizeof(T) * accessor.count;
                std::unique_ptr<std::byte[]> indexBytes = std::make_unique_for_overwrite<std::byte[]>(dataSize);
                copyFromAccessor<T>(asset, accessor, indexBytes.get(), adapter);

                indexBufferBytesByType[vk::IndexTypeValue<T>::value].emplace_back(
                    &primitive,
                    std::span { generatedIndexBytes.emplace_back(std::move(indexBytes)).get(), dataSize });
            }, indexTypeMap.get_variant(componentType));
        }
    }

    for (const fastgltf::Primitive &primitive : lineLoopPrimitives) {
        if (primitive.indicesAccessor) {
            // If LINE_LOOP primitive has an indices accessor (whose indices are i1...in), new indices are
            // generated like: [i1, i2, ..., in, i1].
            const fastgltf::Accessor &accessor = asset.accessors[*primitive.indicesAccessor];

            // Copy accessor data as uint16_t if GPU does not support VK_KHR_index_type_uint8.
            fastgltf::ComponentType componentType = accessor.componentType;
            if (componentType == fastgltf::ComponentType::UnsignedByte && !gpu.supportUint8Index) {
                componentType = fastgltf::ComponentType::UnsignedShort;
            }

            visit([&]<typename T>(std::type_identity<T>) {
                const std::size_t dataSize = sizeof(T) * (accessor.count + 1); // +1 for i1 at the end
                std::unique_ptr<std::byte[]> indexBytes = std::make_unique_for_overwrite<std::byte[]>(dataSize);

                // Copy accessor to [i1, i2, ..., in].
                copyFromAccessor<T>(asset, accessor, indexBytes.get(), adapter);

                // Additional index i1 at the end.
                std::ranges::copy(
                    std::bit_cast<std::array<std::byte, sizeof(T)>>(getAccessorElement<T>(asset, accessor, 0, adapter)),
                    &indexBytes[sizeof(T) * accessor.count]);

                indexBufferBytesByType[vk::IndexTypeValue<T>::value].emplace_back(
                    &primitive,
                    std::span { generatedIndexBytes.emplace_back(std::move(indexBytes)).get(), dataSize });
            }, indexTypeMap.get_variant(componentType));
        }
        else {
            // If LINE_LOOP primitive does not have an indices accessor, it is emulated with indexed drawing,
            // whose indices are [0, 1, ..., n, 0] (n is total vertex count).
            const std::size_t drawCount = asset.accessors[primitive.findAttribute("POSITION")->accessorIndex].count;

            fastgltf::ComponentType componentType = fastgltf::ComponentType::UnsignedInt;
            if (gpu.supportUint8Index && drawCount < 256) {
                componentType = fastgltf::ComponentType::UnsignedByte;
            }
            else if (drawCount < 65536) {
                componentType = fastgltf::ComponentType::UnsignedShort;
            }

            visit([&]<typename T>(std::type_identity<T>) {
                const std::size_t dataSize = sizeof(T) * (drawCount + 1); // +1 for 0 at the end
                std::unique_ptr<std::byte[]> indexBytes = std::make_unique_for_overwrite<std::byte[]>(dataSize);

                // Generate indices as [0, 1, ..., n].
                T *cursor = reinterpret_cast<T*>(indexBytes.get());
                std::iota(cursor, cursor + drawCount, T { 0 });

                // Additional index 0 at the end.
                *(cursor + drawCount) = 0;

                indexBufferBytesByType[vk::IndexTypeValue<T>::value].emplace_back(
                    &primitive,
                    std::span { generatedIndexBytes.emplace_back(std::move(indexBytes)).get(), dataSize });
            }, indexTypeMap.get_variant(componentType));
        }
    }

    bufferByIndexType.insert_range(indexBufferBytesByType | std::views::transform(decomposer([&](vk::IndexType indexType, const auto &primitiveAndIndexBytesPairs) {
        auto [buffer, copyOffsets] = buffer::createCombinedBuffer<true>(
            gpu.allocator,
            primitiveAndIndexBytesPairs | std::views::values,
            vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferSrc,
            vma::MemoryUsage::eAutoPreferHost);
        if (StagingBufferStorage::needStaging(buffer)) {
            stagingBufferStorage.stage(buffer, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer);
        }

        for (auto [pPrimitive, offset] : std::views::zip(primitiveAndIndexBytesPairs | std::views::keys, copyOffsets)) {
            indexInfos.try_emplace(pPrimitive, indexType, static_cast<std::uint32_t>(offset / getIndexTypeSize(indexType)));
        }

        return std::pair { indexType, std::move(buffer) };
    })));
}

vk::Buffer vk_gltf_viewer::vulkan::buffer::CombinedIndices::getIndexBuffer(vk::IndexType indexType) const {
    return bufferByIndexType.at(indexType);
}

std::pair<vk::IndexType, std::uint32_t> vk_gltf_viewer::vulkan::buffer::CombinedIndices::getIndexInfo(const fastgltf::Primitive &primitive) const {
    return indexInfos.at(&primitive);
}