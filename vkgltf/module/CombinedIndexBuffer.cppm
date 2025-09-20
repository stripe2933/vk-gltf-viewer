module;

#include <cassert>

#ifdef USE_DRACO
#include <draco/compression/decode.h>
#endif
#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vkgltf.CombinedIndexBuffer;

import std;
export import fastgltf;
export import vku;

export import vkgltf.StagingBufferStorage;

#define INDEX_SEQ(Is, N, ...) [&]<auto ...Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})

template <typename T, typename... Ts>
concept one_of = (std::same_as<T, Ts> || ...);

/**
 * @brief Generate indices which assembled with \p dstTopology is equivalent to the assembled \p indices with \p srcTopology.
 *
 * Available \p srcTopology and \p dstTopology combinations are:
 * - LineStrip -> Lines
 * - LineLoop -> Lines
 * - LineLoop -> LineStrip
 * - TriangleStrip -> Triangles
 * - TriangleFan -> Triangles
 *
 * @tparam DstT Destination type. Its representation range must be wider than or equal to the \p indices element type.
 * @tparam R Input indices range type
 * @param srcTopology Source primitive topology.
 * @param dstTopology Destination primitive topology.
 * @param indices Input indices which will be assembled with \p srcTopology.
 * @return (index bytes, allocation byte size) pair.
 * @throw std::invalid_argument if \p srcTopology and \p dstTopology combination is not one of the list in above.
 */
template <one_of<std::uint8_t, std::uint16_t, std::uint32_t> DstT, std::ranges::random_access_range R>
[[nodiscard]] std::pair<std::unique_ptr<std::byte[]>, std::size_t> convertIndices(
    fastgltf::PrimitiveType srcTopology,
    fastgltf::PrimitiveType dstTopology,
    R &&indices
) {
    static_assert(sizeof(std::ranges::range_value_t<R>) <= sizeof(DstT), "Destination integer type is not representable with source integer type.");

    if (srcTopology == fastgltf::PrimitiveType::LineStrip && dstTopology == fastgltf::PrimitiveType::Lines) {
        // [i0, i1, ..., i_{n-1}] -> [i0, i1, i1, i2, ..., i_{n-2}, i_{n-1}]
        const std::size_t drawCount = 2 * (indices.size() - 1);
        const std::size_t byteSize = sizeof(DstT) * drawCount;
        auto result = std::make_unique_for_overwrite<std::byte[]>(byteSize);

        auto resultIt = std::span<DstT> { reinterpret_cast<DstT*>(result.get()), drawCount }.begin();
        *resultIt++ = indices[0];
        for (std::size_t i = 1; i < indices.size() - 1; ++i) {
            *resultIt++ = indices[i];
            *resultIt++ = indices[i];
        }
        *resultIt++ = indices.back();

        return { std::move(result), byteSize };
    }
    if (srcTopology == fastgltf::PrimitiveType::LineLoop && dstTopology == fastgltf::PrimitiveType::Lines) {
        // [i0, i1, ..., i_{n-1}] -> [i0, i1, i1, i2, ..., i_{n-2}, i_{n-1}, i_{n-1}, i0]
        const std::size_t drawCount = 2 * indices.size();
        const std::size_t byteSize = sizeof(DstT) * drawCount;
        auto result = std::make_unique_for_overwrite<std::byte[]>(byteSize);

        auto resultIt = std::span<DstT> { reinterpret_cast<DstT*>(result.get()), drawCount }.begin();
        *resultIt++ = indices[0];
        for (std::size_t i = 1; i < indices.size(); ++i) {
            *resultIt++ = indices[i];
            *resultIt++ = indices[i];
        }
        *resultIt++ = indices[0];

        return { std::move(result), byteSize };
    }
    if (srcTopology == fastgltf::PrimitiveType::LineLoop && dstTopology == fastgltf::PrimitiveType::LineStrip) {
        // [i0, i1, ..., i_{n-1}] -> [i0, i1, i2, ..., i_{n-1}, i0]
        const std::size_t drawCount = indices.size() + 1;
        const std::size_t byteSize = sizeof(DstT) * drawCount;
        auto result = std::make_unique_for_overwrite<std::byte[]>(byteSize);

        auto resultIt = std::span<DstT> { reinterpret_cast<DstT*>(result.get()), drawCount }.begin();
        for (std::size_t i = 0; i < indices.size(); ++i) {
            *resultIt++ = indices[i];
        }
        *resultIt++ = indices[0];

        return { std::move(result), byteSize };
    }
    if (srcTopology == fastgltf::PrimitiveType::TriangleStrip && dstTopology == fastgltf::PrimitiveType::Triangles) {
        // [i0, i1, ..., i_{n-1}] -> [i0, i1, i2, i1, i2, i3, ..., i_{n-3}, i_{n-2}, i_{n-1}]
        const std::size_t drawCount = 3 * (indices.size() - 2);
        const std::size_t byteSize = sizeof(DstT) * drawCount;
        auto result = std::make_unique_for_overwrite<std::byte[]>(byteSize);

        auto resultIt = std::span<DstT> { reinterpret_cast<DstT*>(result.get()), drawCount }.begin();
        for (std::size_t i = 0; i < indices.size() - 2; ++i) {
            *resultIt++ = indices[i];
            *resultIt++ = indices[i + 1];
            *resultIt++ = indices[i + 2];
        }

        return { std::move(result), byteSize };
    }
    if (srcTopology == fastgltf::PrimitiveType::TriangleFan && dstTopology == fastgltf::PrimitiveType::Triangles) {
        // [i0, i1, ..., i_{n-1}] -> [i0, i1, i2, i0, i2, i3, ..., i0, i_{n-2}, i_{n-1}]
        const std::size_t drawCount = 3 * (indices.size() - 2);
        const std::size_t byteSize = sizeof(DstT) * drawCount;
        auto result = std::make_unique_for_overwrite<std::byte[]>(byteSize);

        auto resultIt = std::span<DstT> { reinterpret_cast<DstT*>(result.get()), drawCount }.begin();
        for (std::size_t i = 1; i < indices.size() - 1; ++i) {
            *resultIt++ = indices[0];
            *resultIt++ = indices[i];
            *resultIt++ = indices[i + 1];
        }

        return { std::move(result), byteSize };
    }

    throw std::invalid_argument { "Unsupported topology conversion" };
}

namespace vkgltf {
    /**
     * @brief Vulkan buffer of all glTF asset's indices accessors data combined into a single buffer, with alignment of
     * the component type.
     *
     * The diagram below shows the layout of the combined indices buffer:
     *
     * <-- multiple of 4 -->
     * <--------- multiple of 2 --------->
     * <-------------------------- multiple of 1 -------------------------->
     * +---------------------------------------------------------------------------------------------------+
     * | u32 indices | ... | u32 indices | u16 indices | ... | u16 indices | u8 indices | ... | u8 indices |
     * +---------------------------------------------------------------------------------------------------+
     */
    export class CombinedIndexBuffer final : public vku::AllocatedBuffer {
        struct DefaultTopologyConvertFn {
            static fastgltf::PrimitiveType operator()(fastgltf::PrimitiveType type) noexcept {
                return type == fastgltf::PrimitiveType::LineLoop ? fastgltf::PrimitiveType::LineStrip : type;
            }
        };

    public:
        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        class Config {
        public:
            const BufferDataAdapter &adapter;

            /**
             * @brief Use 4-byte buffer allocation when no index data is presented in the asset.
             *
             * If there's no index data to process, the buffer will be empty and <tt>vk::InitializationFailedError</tt>
             * will be thrown as Vulkan does not allow zero-sized buffer creation. Setting this to <tt>true</tt> will
             * allocate a 4-byte buffer if the result buffer size is zero.
             */
            bool avoidZeroSizeBuffer = true;

            /**
             * @brief If <tt>true</tt>, unsigned byte indices will be converted to unsigned short.
             *
             * Can be useful if Vulkan GPU does not support unsigned byte indices.
             */
            bool promoteUnsignedByteToUnsignedShort = true;

            /**
             * @brief Function determines the given primitive's topology needs to be converted and therefore processed
             * in <tt>CombinedIndexBuffer</tt>, regardless of it is indexed or not.
             *
             * If the result of this function invoked with the primitive type is different from passed primitive type,
             * <tt>CombinedIndexBuffer</tt> tries to generate new indices data. For example, if primitive type is
             * <tt>TriangleFan</tt>, its indices accessor data is <tt>[0, 1, 2, 3]</tt> and
             * <tt>topologyConvertFn(fastgltf::PrimitiveType::TriangleFan)</tt> is <tt>Triangles</tt>, it generates new
             * indices data of <tt>[0, 1, 2, 0, 2, 3]</tt>. It is also applied to non-indexed primitive, whose result
             * will be calculated from <tt>[0, 1, ..., (POSITION attribute accessor).count - 1]</tt>.
             *
             * The currently available input/output pairs of this function are restricted as:
             * - LineStrip -> Lines
             * - LineLoop -> Lines
             * - LineLoop -> LineStrip
             * - TriangleStrip -> Triangles
             * - TriangleFan -> Triangles
             * Violating the restriction will throw <tt>std::invalid_argument</tt> exception. TriangleFan -> TriangleStrip
             * is theoretically possible, but not implemented due to the complexity.
             *
             * By default, it returns the input type itself, except for LineLoop, which returns LineStrip, as Vulkan
             * does not support <tt>LINE_LOOP</tt> primitive topology natively (unlike OpenGL).
             */
            std::function<fastgltf::PrimitiveType(fastgltf::PrimitiveType)> topologyConvertFn = DefaultTopologyConvertFn{};

            /**
             * @brief Vulkan buffer usage flags for the buffer creation.
             *
             * If \p stagingInfo is given, <tt>vk::BufferUsageFlagBits::eTransferSrc</tt> is added at the staging buffer
             * creation and the device local buffer will be created with <tt>usageFlags | vk::BufferUsageFlagBits::eTransferDst</tt>
             * usage.
             */
            vk::BufferUsageFlags usageFlags = vk::BufferUsageFlagBits::eIndexBuffer;

            /**
             * @brief Queue family indices that the buffer can be concurrently accessed.
             *
             * If its size is less than 2, <tt>sharingMode</tt> of the buffer will be set to <tt>vk::SharingMode::eExclusive</tt>.
             */
            vk::ArrayProxyNoTemporaries<const std::uint32_t> queueFamilies = {};

            /**
             * @brief VMA allocation creation flags for the buffer allocation.
             *
             * @note <tt>flags</tt> MUST contain either <tt>vma::AllocationCreateFlagBits::eHostAccessSequentialWrite</tt> or
             * <tt>vma::AllocationCreateFlagBits::eHostAccessRandom</tt> to allow the host to write to the buffer.
             */
            vma::AllocationCreateInfo allocationCreateInfo = {
                vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped,
                vma::MemoryUsage::eAutoPreferHost,
            };

            const StagingInfo *stagingInfo = nullptr;
        };

        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        CombinedIndexBuffer(
            const fastgltf::Asset &asset LIFETIMEBOUND,
            vma::Allocator allocator,
            const Config<BufferDataAdapter> &config = {}
        ) : CombinedIndexBuffer { allocator, config, IData { asset, config } } { }

        /**
         * @brief Get (offset, size) pair of the index buffer for the given index type.
         * @param indexType Index type to get the offset and size for.
         * @return Pair of offset and size of the index buffer for the given index type.
         */
        [[nodiscard]] std::pair<vk::DeviceSize, vk::DeviceSize> getIndexOffsetAndSize(vk::IndexType indexType) const noexcept;

        /**
         * @brief Get (index type, first index) pair for the given primitive.
         * @param primitive Primitive to get the index information.
         * @return Pair of index type and first index in the index buffer.
         */
        [[nodiscard]] std::pair<vk::IndexType, std::uint32_t> getIndexTypeAndFirstIndex(const fastgltf::Primitive &primitive) const;

        /**
         * @brief Construct <tt>CombinedIndexBuffer</tt> from \p asset, only if the asset has index data.
         *
         * If there's no index data to process and <tt>Config::avoidZeroSizeBuffer</tt> is set to <tt>false</tt>,
         * <tt>vk::InitializationFailedError</tt> will be thrown as Vulkan does not allow zero-sized buffer creation.
         * This function calculates the buffer size before the buffer creation and returns <tt>std::nullopt</tt> if
         * the size is zero, otherwise returns a <tt>CombinedIndexBuffer</tt> instance.
         *
         * @tparam BufferDataAdapter A functor type that return the bytes span from a glTF buffer view.
         * @param asset glTF asset.
         * @param allocator VMA allocator to allocate the buffer.
         * @param config Configuration for the combined index buffer creation.
         * @return <tt>CombinedIndexBuffer</tt> instance if the asset has index data, otherwise <tt>std::nullopt</tt>.
         */
        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        [[nodiscard]] static std::optional<CombinedIndexBuffer> from(
            const fastgltf::Asset &asset LIFETIMEBOUND,
            vma::Allocator allocator,
            const Config<BufferDataAdapter> &config = {}
        ) {
            IData intermediateData { asset, config };
            if (intermediateData.bufferSize == 0) {
                return std::nullopt;
            }

            return std::optional<CombinedIndexBuffer> { std::in_place, allocator, config, std::move(intermediateData) };
        }

    private:
        struct IData {
            std::vector<std::unique_ptr<std::byte[]>> generatedBytes;
            std::unordered_map<const fastgltf::Primitive*, std::span<const std::byte>> unsignedByteIndexBytes;
            std::unordered_map<const fastgltf::Primitive*, std::span<const std::byte>> unsignedShortIndexBytes;
            std::unordered_map<const fastgltf::Primitive*, std::span<const std::byte>> unsignedIntIndexBytes;
            std::unordered_map<const fastgltf::Primitive*, std::pair<vk::IndexType, std::uint32_t>> indexTypeAndFirstIndexByPrimitive;
            vk::DeviceSize bufferSize;
            vk::DeviceSize unsignedShortIndexOffset;
            vk::DeviceSize unsignedByteIndexOffset;

            template <typename BufferDataAdapter>
            IData(const fastgltf::Asset &asset, const Config<BufferDataAdapter> &config) {
            #ifdef USE_DRACO
                draco::Decoder dracoDecoder;
            #endif

                for (const fastgltf::Mesh &mesh : asset.meshes) {
                    for (const fastgltf::Primitive &primitive : mesh.primitives) {
                        // glTF 2.0 specification:
                        //   When positions are not specified, client implementations SHOULD skip primitive’s rendering unless its
                        //   positions are provided by other means (e.g., by an extension). This applies to both indexed and
                        //   non-indexed geometry.
                        //   https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes-overview
                        const fastgltf::Attribute *positionAttribute = primitive.findAttribute("POSITION");
                        if (positionAttribute == primitive.attributes.end()) {
                            continue;
                        }

                        if (fastgltf::PrimitiveType type = std::invoke(config.topologyConvertFn, primitive.type); type != primitive.type) {
                        #ifdef USE_DRACO
                            if (primitive.dracoCompression) {
                                throw std::runtime_error { "Topology conversion of Draco-compressed primitive is not supported" }; // TODO
                            }
                        #endif

                            std::unique_ptr<std::byte[]> &newIndexBytes = generatedBytes.emplace_back();
                            std::uint32_t newIndexByteSize;

                            if (primitive.indicesAccessor) {
                                const fastgltf::Accessor &accessor = asset.accessors[*primitive.indicesAccessor];
                                switch (accessor.componentType) {
                                    case fastgltf::ComponentType::UnsignedByte: {
                                        std::span<const std::uint8_t> indices;
                                        std::unique_ptr<std::uint8_t[]> copiedAccessorIndices;
                                        if (!accessor.bufferViewIndex || accessor.sparse) {
                                            copiedAccessorIndices = std::make_unique_for_overwrite<std::uint8_t[]>(accessor.count);
                                            copyFromAccessor<std::uint8_t>(asset, accessor, copiedAccessorIndices.get(), config.adapter);
                                            indices = { copiedAccessorIndices.get(), accessor.count };
                                        }
                                        else {
                                            indices = { reinterpret_cast<const std::uint8_t*>(config.adapter(asset, *accessor.bufferViewIndex).data() + accessor.byteOffset), accessor.count };
                                        }

                                        if (config.promoteUnsignedByteToUnsignedShort) {
                                            std::tie(newIndexBytes, newIndexByteSize) = convertIndices<std::uint16_t>(primitive.type, type, indices);
                                            unsignedByteIndexBytes.emplace(&primitive, std::span { newIndexBytes.get(), newIndexByteSize });
                                        }
                                        else {
                                            std::tie(newIndexBytes, newIndexByteSize) = convertIndices<std::uint8_t>(primitive.type, type, indices);
                                            unsignedByteIndexBytes.emplace(&primitive, std::span { newIndexBytes.get(), newIndexByteSize });
                                        }
                                        break;
                                    }
                                    case fastgltf::ComponentType::UnsignedShort: {
                                        std::span<const std::uint16_t> indices;
                                        std::unique_ptr<std::uint16_t[]> copiedAccessorIndices;
                                        if (!accessor.bufferViewIndex || accessor.sparse) {
                                            copiedAccessorIndices = std::make_unique_for_overwrite<std::uint16_t[]>(accessor.count);
                                            copyFromAccessor<std::uint16_t>(asset, accessor, copiedAccessorIndices.get(), config.adapter);
                                            indices = { copiedAccessorIndices.get(), accessor.count };
                                        }
                                        else {
                                            indices = { reinterpret_cast<const std::uint16_t*>(config.adapter(asset, *accessor.bufferViewIndex).data() + accessor.byteOffset), accessor.count };
                                        }

                                        std::tie(newIndexBytes, newIndexByteSize) = convertIndices<std::uint16_t>(primitive.type, type, indices);
                                        unsignedShortIndexBytes.emplace(&primitive, std::span { newIndexBytes.get(), newIndexByteSize });
                                        break;
                                    }
                                    case fastgltf::ComponentType::UnsignedInt: {
                                        std::span<const std::uint32_t> indices;
                                        std::unique_ptr<std::uint32_t[]> copiedAccessorIndices;
                                        if (!accessor.bufferViewIndex || accessor.sparse) {
                                            copiedAccessorIndices = std::make_unique_for_overwrite<std::uint32_t[]>(accessor.count);
                                            copyFromAccessor<std::uint32_t>(asset, accessor, copiedAccessorIndices.get(), config.adapter);
                                            indices = { copiedAccessorIndices.get(), accessor.count };
                                        }
                                        else {
                                            indices = { reinterpret_cast<const std::uint32_t*>(config.adapter(asset, *accessor.bufferViewIndex).data() + accessor.byteOffset), accessor.count };
                                        }

                                        std::tie(newIndexBytes, newIndexByteSize) = convertIndices<std::uint32_t>(primitive.type, type, indices);
                                        unsignedIntIndexBytes.emplace(&primitive, std::span { newIndexBytes.get(), newIndexByteSize });
                                        break;
                                    }
                                    default:
                                        // glTF 2.0 mandates index accessor component type to be one of the above.
                                        std::unreachable();
                                }
                            }
                            else {
                                const std::size_t accessorCount = asset.accessors[positionAttribute->accessorIndex].count;
                                if (!config.promoteUnsignedByteToUnsignedShort && accessorCount < std::numeric_limits<std::uint8_t>::max()) {
                                    std::tie(newIndexBytes, newIndexByteSize) = convertIndices<std::uint8_t>(
                                        primitive.type, type,
                                        std::views::iota(std::uint8_t{}, static_cast<std::uint8_t>(accessorCount)));
                                    unsignedByteIndexBytes.emplace(&primitive, std::span { newIndexBytes.get(), newIndexByteSize });
                                }
                                else if (accessorCount < std::numeric_limits<std::uint16_t>::max()) {
                                    std::tie(newIndexBytes, newIndexByteSize) = convertIndices<std::uint16_t>(
                                        primitive.type, type,
                                        std::views::iota(std::uint16_t{}, static_cast<std::uint16_t>(accessorCount)));
                                    unsignedShortIndexBytes.emplace(&primitive, std::span { newIndexBytes.get(), newIndexByteSize });
                                }
                                else if (accessorCount < std::numeric_limits<std::uint32_t>::max()) {
                                    std::tie(newIndexBytes, newIndexByteSize) = convertIndices<std::uint32_t>(
                                        primitive.type, type,
                                        std::views::iota(std::uint32_t{}, static_cast<std::uint32_t>(accessorCount)));
                                    unsignedIntIndexBytes.emplace(&primitive, std::span { newIndexBytes.get(), newIndexByteSize });
                                }
                                else {
                                    // Error in here means POSITION attribute accessor count is 4,294,967,295 (=2^32 - 1).
                                    throw std::runtime_error { "Too large vertex draw count" };
                                }
                            }
                        }
                        else if (primitive.indicesAccessor) {
                            const fastgltf::Accessor &accessor = asset.accessors[*primitive.indicesAccessor];
                            const auto getBytes = [&]<typename T>() -> std::span<const std::byte> {
                            #ifdef USE_DRACO
                                if (const auto &draco = primitive.dracoCompression) {
                                    draco::DecoderBuffer dracoDecoderBuffer;

                                    const std::span<const std::byte> bufferViewBytes = config.adapter(asset, draco->bufferView);
                                    dracoDecoderBuffer.Init(reinterpret_cast<const char*>(bufferViewBytes.data()), bufferViewBytes.size());

                                    if (draco::Decoder::GetEncodedGeometryType(&dracoDecoderBuffer).value() != draco::EncodedGeometryType::TRIANGULAR_MESH) {
                                        throw std::runtime_error { "Only triangular mesh is supported" };
                                    }

                                    std::unique_ptr<const draco::Mesh> dracoMesh = dracoDecoder.DecodeMeshFromBuffer(&dracoDecoderBuffer).value();

                                    const std::size_t byteSize = sizeof(T) * accessor.count;
                                    auto bytes = std::make_unique_for_overwrite<std::byte[]>(byteSize);
                                    std::span result { generatedBytes.emplace_back(std::move(bytes)).get(), byteSize };

                                    auto it = result.begin();
                                    if (primitive.type == fastgltf::PrimitiveType::Triangles) {
                                        // For each face in the mesh, three indices are copied.
                                        for (draco::FaceIndex faceIndex { 0 }; faceIndex < dracoMesh->num_faces(); ++faceIndex) {
                                            INDEX_SEQ(Is, 3, {
                                                ((it = std::ranges::copy(
                                                    std::bit_cast<std::array<std::byte, sizeof(T)>>(static_cast<T>(get<Is>(dracoMesh->face(faceIndex)).value())),
                                                    it).out), ...);
                                            });
                                        }
                                    }
                                    else if (primitive.type == fastgltf::PrimitiveType::TriangleStrip) {
                                        // The first face's three indices are copied, then for each subsequent face,
                                        // only the third index is copied.
                                        INDEX_SEQ(Is, 3, {
                                            ((it = std::ranges::copy(
                                                std::bit_cast<std::array<std::byte, sizeof(T)>>(static_cast<T>(get<Is>(dracoMesh->face(draco::FaceIndex { 0 })).value())),
                                                it).out), ...);
                                        });
                                        for (draco::FaceIndex faceIndex { 1 }; faceIndex < dracoMesh->num_faces(); ++faceIndex) {
                                            it = std::ranges::copy(
                                                std::bit_cast<std::array<std::byte, sizeof(T)>>(static_cast<T>(get<2>(dracoMesh->face(faceIndex)).value())),
                                                it).out;
                                        }
                                    }
                                    assert(it == result.end() && "Size estimation failed");

                                    return result;
                                }
                            #endif

                                if (accessor.sparse || !accessor.bufferViewIndex || accessor.componentType != fastgltf::ElementTraits<T>::enum_component_type) {
                                    // Accessor data is not compatible with the Vulkan index buffer, so it has to be generated.
                                    const std::size_t byteSize = sizeof(T) * accessor.count;
                                    auto bytes = std::make_unique_for_overwrite<std::byte[]>(byteSize);
                                    copyFromAccessor<T>(asset, accessor, bytes.get(), config.adapter);
                                    return { generatedBytes.emplace_back(std::move(bytes)).get(), byteSize };
                                }

                                // Can use the already loaded buffer view data.
                                return config.adapter(asset, *accessor.bufferViewIndex).subspan(accessor.byteOffset, sizeof(T) * accessor.count);
                            };

                            switch (accessor.componentType) {
                                case fastgltf::ComponentType::UnsignedByte:
                                    if (!config.promoteUnsignedByteToUnsignedShort) {
                                        unsignedByteIndexBytes.emplace(&primitive, getBytes.template operator()<std::uint8_t>());
                                        break;
                                    }
                                    [[fallthrough]];
                                case fastgltf::ComponentType::UnsignedShort:
                                    unsignedShortIndexBytes.emplace(&primitive, getBytes.template operator()<std::uint16_t>());
                                    break;
                                case fastgltf::ComponentType::UnsignedInt:
                                    unsignedIntIndexBytes.emplace(&primitive, getBytes.template operator()<std::uint32_t>());
                                    break;
                                default:
                                    // glTF 2.0 mandates index accessor component type to be one of the above.
                                    std::unreachable();
                            }
                        }
                    }
                }

                bufferSize = 0;
                for (const auto &[primitive, indexBytes] : unsignedIntIndexBytes) {
                    indexTypeAndFirstIndexByPrimitive.try_emplace(
                        primitive, 
                        vk::IndexType::eUint32, static_cast<std::uint32_t>(bufferSize / sizeof(std::uint32_t)));
                    bufferSize += indexBytes.size_bytes();
                }

                unsignedShortIndexOffset = bufferSize;
                for (const auto &[primitive, indexBytes] : unsignedShortIndexBytes) {
                    indexTypeAndFirstIndexByPrimitive.try_emplace(
                        primitive, 
                        vk::IndexType::eUint16, static_cast<std::uint32_t>((bufferSize - unsignedShortIndexOffset) / sizeof(std::uint16_t)));
                    bufferSize += indexBytes.size_bytes();
                }

                unsignedByteIndexOffset = bufferSize;
                for (const auto &[primitive, indexBytes] : unsignedByteIndexBytes) {
                    indexTypeAndFirstIndexByPrimitive.try_emplace(
                        primitive, 
                        vk::IndexType::eUint8, static_cast<std::uint32_t>((bufferSize - unsignedByteIndexOffset) / sizeof(std::uint8_t)));
                    bufferSize += indexBytes.size_bytes();
                }
            }
        };

        template <typename BufferDataAdapter>
        CombinedIndexBuffer(vma::Allocator allocator, const Config<BufferDataAdapter> &config, IData &&intermediateData)
            : AllocatedBuffer {
                allocator,
                vk::BufferCreateInfo {
                    {},
                    std::max<vk::DeviceSize>(intermediateData.bufferSize, config.avoidZeroSizeBuffer ? 4 : 0),
                    config.usageFlags
                        | (config.stagingInfo ? vk::Flags { vk::BufferUsageFlagBits::eTransferSrc } : vk::BufferUsageFlags{}),
                    config.queueFamilies.size() < 2 ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent,
                    config.queueFamilies,
                },
                config.allocationCreateInfo,
            },
            indexTypeAndFirstIndexByPrimitive { std::move(intermediateData.indexTypeAndFirstIndexByPrimitive) },
            unsignedShortIndexOffset { intermediateData.unsignedShortIndexOffset },
            unsignedByteIndexOffset { intermediateData.unsignedByteIndexOffset },
            actualDataSize { intermediateData.bufferSize } {
            std::byte *dst = static_cast<std::byte*>(allocator.getAllocationInfo(allocation).pMappedData);
            for (std::span indexBytes : intermediateData.unsignedIntIndexBytes | std::views::values) {
                dst = std::ranges::copy(indexBytes, dst).out;
            }
            for (std::span indexBytes : intermediateData.unsignedShortIndexBytes | std::views::values) {
                dst = std::ranges::copy(indexBytes, dst).out;
            }
            for (std::span indexBytes : intermediateData.unsignedByteIndexBytes | std::views::values) {
                dst = std::ranges::copy(indexBytes, dst).out;
            }

            if (!vku::contains(allocator.getAllocationMemoryProperties(allocation), vk::MemoryPropertyFlagBits::eHostCoherent)) {
                allocator.flushAllocation(allocation, 0, size);
            }

            if (config.stagingInfo && actualDataSize > 0 /* no need for staging if no data */) {
                config.stagingInfo->stage(*this, config.usageFlags, config.queueFamilies);
            }
        }

        std::unordered_map<const fastgltf::Primitive*, std::pair<vk::IndexType, std::uint32_t>> indexTypeAndFirstIndexByPrimitive;
        vk::DeviceSize unsignedShortIndexOffset;
        vk::DeviceSize unsignedByteIndexOffset;
        vk::DeviceSize actualDataSize;
    };
}

export template <>
class vkgltf::CombinedIndexBuffer::Config<fastgltf::DefaultBufferDataAdapter> {
    static constexpr fastgltf::DefaultBufferDataAdapter adapter;

    // Make adapter accessible by CombinedIndexBuffer.
    friend class CombinedIndexBuffer;

public:
    bool avoidZeroSizeBuffer = true;
    bool promoteUnsignedByteToUnsignedShort = true;
    std::function<fastgltf::PrimitiveType(fastgltf::PrimitiveType)> topologyConvertFn = DefaultTopologyConvertFn{};
    vk::BufferUsageFlags usageFlags = vk::BufferUsageFlagBits::eIndexBuffer;
    vk::ArrayProxyNoTemporaries<const std::uint32_t> queueFamilies = {};
    vma::AllocationCreateInfo allocationCreateInfo = {
        vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped,
        vma::MemoryUsage::eAutoPreferHost,
    };
    const StagingInfo *stagingInfo = nullptr;
};

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

std::pair<vk::DeviceSize, vk::DeviceSize> vkgltf::CombinedIndexBuffer::getIndexOffsetAndSize(
    vk::IndexType indexType
) const noexcept {
    switch (indexType) {
        case vk::IndexType::eUint8:
            // When config.avoidZeroSizeBuffer == true and intermediateData.bufferSize == 0, the buffer will be created
            // with 4-byte size. If vk::Buffer::size is used instead of actualDataSize,
            //   getIndexOffsetAndSize(vk::IndexType::eUint8) = (unsignedByteIndexOffset, size - unsignedByteIndexOffset) = (0, 4),
            // which yield incorrect result.
            return { unsignedByteIndexOffset, actualDataSize - unsignedByteIndexOffset };
        case vk::IndexType::eUint16:
            return { unsignedShortIndexOffset, unsignedByteIndexOffset - unsignedShortIndexOffset };
        case vk::IndexType::eUint32:
            return { 0, unsignedShortIndexOffset };
        default:
            std::unreachable();
    }
}

std::pair<vk::IndexType, std::uint32_t> vkgltf::CombinedIndexBuffer::getIndexTypeAndFirstIndex(
    const fastgltf::Primitive &primitive
) const {
    return indexTypeAndFirstIndexByPrimitive.at(&primitive);
}