module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.buffer.CombinedIndices;

import std;
export import fastgltf;
import :helpers.functional;
import :helpers.type_map;
import :vulkan.buffer;
export import :vulkan.buffer.StagingBufferStorage;
export import :vulkan.Gpu;
import :vulkan.trait.PostTransferObject;

constexpr type_map indexTypeMap {
    make_type_map_entry<std::uint8_t>(fastgltf::ComponentType::UnsignedByte),
    make_type_map_entry<std::uint16_t>(fastgltf::ComponentType::UnsignedShort),
    make_type_map_entry<std::uint32_t>(fastgltf::ComponentType::UnsignedInt),
};

[[nodiscard]] constexpr vk::IndexType getIndexType(fastgltf::ComponentType componentType) {
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

[[nodiscard]] constexpr std::size_t getIndexTypeSize(vk::IndexType indexType) {
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

namespace vk_gltf_viewer::vulkan::buffer {
    export class CombinedIndices : trait::PostTransferObject {
    public:
        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        CombinedIndices(
            const fastgltf::Asset &asset,
            const Gpu &gpu LIFETIMEBOUND,
            StagingBufferStorage &stagingBufferStorage,
            const BufferDataAdapter &adapter = {}
        ) : PostTransferObject { stagingBufferStorage } {
            auto primitives = asset.meshes
                | std::views::transform(&fastgltf::Mesh::primitives)
                | std::views::join;

            // Primitives that are having an indices accessor.
            auto indexedPrimitives = primitives | std::views::filter([](const fastgltf::Primitive &primitive) {
                return primitive.indicesAccessor.has_value();
            });

            // Primitives whose type is LINE_LOOP.
            auto lineLoopPrimitives = indexedPrimitives | std::views::filter([](const fastgltf::Primitive &primitive) {
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
                if (isAccessorBufferViewCompatibleWithIndexBuffer(accessor, asset, gpu)) {
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
                    const fastgltf::ComponentType componentType = [&]() {
                        if (gpu.supportUint8Index && drawCount < 256) {
                            return fastgltf::ComponentType::UnsignedByte;
                        }
                        else if (drawCount < 65536) {
                            return fastgltf::ComponentType::UnsignedShort;
                        }
                        else {
                            return fastgltf::ComponentType::UnsignedInt;
                        }
                    }();

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
                    vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferSrc);
                if (StagingBufferStorage::needStaging(buffer)) {
                    stagingBufferStorage.stage(buffer, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer);
                }

                for (auto [pPrimitive, offset] : std::views::zip(primitiveAndIndexBytesPairs | std::views::keys, copyOffsets)) {
                    indexInfos.try_emplace(pPrimitive, indexType, static_cast<std::uint32_t>(offset / getIndexTypeSize(indexType)));
                }

                return std::pair { indexType, std::move(buffer) };
            })));
        }

        [[nodiscard]] vk::Buffer getIndexBuffer(vk::IndexType indexType) const {
            return bufferByIndexType.at(indexType);
        }

        /**
         * @brief Get the index information of the primitive.
         * @param primitive Primitive to get the index information.
         * @return Pair of index type and first index.
         */
        [[nodiscard]] std::pair<vk::IndexType, std::uint32_t> getIndexInfo(const fastgltf::Primitive &primitive) const {
            return indexInfos.at(&primitive);
        }

    private:
        std::unordered_map<const fastgltf::Primitive*, std::pair<vk::IndexType, std::uint32_t>> indexInfos;
        std::unordered_map<vk::IndexType, vku::AllocatedBuffer> bufferByIndexType;

        [[nodiscard]] static bool isAccessorBufferViewCompatibleWithIndexBuffer(
            const fastgltf::Accessor &accessor,
            const fastgltf::Asset &asset,
            const Gpu &gpu
        ) noexcept {
            if (accessor.sparse) return false;

            // Accessor without buffer view has to be treated as zeros.
            if (!accessor.bufferViewIndex) return false;

            // Vulkan does not support interleaved index buffer.
            if (const auto& byteStride = asset.bufferViews[*accessor.bufferViewIndex].byteStride) {
                // Is accessor strided?
                if (*byteStride != getElementByteSize(accessor.type, accessor.componentType)) return false;
            }

            // Accessor data is unsigned byte and the device does not support it.
            if (accessor.componentType == fastgltf::ComponentType::UnsignedByte && !gpu.supportUint8Index) return false;

            return true;
        }
    };
}