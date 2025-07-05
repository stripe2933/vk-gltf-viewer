module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vkgltf.CombinedIndexBuffer;

import std;
export import fastgltf;
export import vku;

export import vkgltf.StagingBufferStorage;

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
             * @brief If <tt>true</tt>, <tt>LINE_LOOP</tt> primitive will be processed as <tt>LINE_STRIP</tt> with an
             * additional first vertex at the end of the index buffer.
             *
             * As Vulkan does not support <tt>LINE_LOOP</tt> primitive topology natively (unlike OpenGL), it has to be
             * emulated as <tt>LINE_STRIP</tt> with an additional first vertex at the end of the index buffer.
             */
            bool processLineLoopAsLineStrip = true;

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
            vk::ArrayProxy<std::uint32_t> queueFamilies = {};

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

                        if (config.processLineLoopAsLineStrip && primitive.type == fastgltf::PrimitiveType::LineLoop) {
                            // LINE_LOOP primitive is not supported in Vulkan, therefore has to be emulated as LINE_STRIP, with
                            // additional first vertex at the end using indexed draw.
                            if (primitive.indicesAccessor) {
                                const fastgltf::Accessor &accessor = asset.accessors[*primitive.indicesAccessor];
                                fastgltf::ComponentType componentType = accessor.componentType;
                                const std::size_t drawCount = accessor.count + 1;

                                const auto getBytes = [&]<typename T>() -> std::span<const std::byte> {
                                    const std::size_t byteSize = sizeof(T) * drawCount;
                                    auto bytes = std::make_unique_for_overwrite<std::byte[]>(byteSize);

                                    // [i0, i1, ..., i_{n-1}, i0] (n: indices accessor count)
                                    copyFromAccessor<T>(asset, accessor, bytes.get(), config.adapter);
                                    std::ranges::copy(
                                        std::bit_cast<std::array<std::byte, sizeof(T)>>(getAccessorElement<T>(asset, accessor, 0, config.adapter)),
                                        &bytes[sizeof(T) * accessor.count]);

                                    return { generatedBytes.emplace_back(std::move(bytes)).get(), byteSize };
                                };

                                // glTF 2.0 specification:
                                //   indices accessor MUST NOT contain the maximum possible value for the component type used
                                //   (i.e., 255 for unsigned bytes, 65535 for unsigned shorts, 4294967295 for unsigned ints).
                                //   https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes-overview
                                //
                                // As indices count is incremented by 1, it has to be re-checked if the count is still less than
                                // the maximum value of the component type.

                                switch (componentType) {
                                    case fastgltf::ComponentType::UnsignedByte:
                                        if (!config.promoteUnsignedByteToUnsignedShort) {
                                            unsignedByteIndexBytes.emplace(&primitive, getBytes.template operator()<std::uint8_t>());
                                            break;
                                        }
                                        // If condition not satisfied, type is promoted to the larger type and will be retried (at
                                        // the below).
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
                            else {
                                const std::size_t accessorCount = asset.accessors[positionAttribute->accessorIndex].count;
                                const std::size_t drawCount = accessorCount + 1;

                                const auto getBytes = [&]<typename T>() -> std::span<const std::byte> {
                                    const std::size_t byteSize = sizeof(T) * drawCount;
                                    auto bytes = std::make_unique_for_overwrite<std::byte[]>(byteSize);

                                    // [0, 1, ..., n-1, 0] (n: POSITION accessor count)
                                    auto it = &bytes[0];
                                    for (T n = 0; n < accessorCount; ++n) {
                                        it = std::ranges::copy(std::bit_cast<std::array<std::byte, sizeof(T)>>(n), it).out;
                                    }
                                    std::ranges::copy(std::bit_cast<std::array<std::byte, sizeof(T)>>(T{}), it);

                                    return { generatedBytes.emplace_back(std::move(bytes)).get(), byteSize };
                                };

                                if (!config.promoteUnsignedByteToUnsignedShort && drawCount < std::numeric_limits<std::uint8_t>::max()) {
                                    unsignedByteIndexBytes.emplace(&primitive, getBytes.template operator()<std::uint8_t>());
                                }
                                else if (drawCount < std::numeric_limits<std::uint16_t>::max()) {
                                    unsignedShortIndexBytes.emplace(&primitive, getBytes.template operator()<std::uint16_t>());
                                }
                                else if (drawCount < std::numeric_limits<std::uint32_t>::max()) {
                                    unsignedIntIndexBytes.emplace(&primitive, getBytes.template operator()<std::uint32_t>());
                                }
                                else {
                                    // Error in here means POSITION attribute accessor count is 4,294,967,294 (=2^32 - 2).
                                    throw std::runtime_error { "Too large vertex draw count" };
                                }
                            }
                        }
                        else if (primitive.indicesAccessor) {
                            const fastgltf::Accessor &accessor = asset.accessors[*primitive.indicesAccessor];
                            const auto getBytes = [&]<typename T>() -> std::span<const std::byte> {
                                if (accessor.sparse || !accessor.bufferViewIndex || accessor.componentType != fastgltf::ElementTraits<T>::enum_component_type) {
                                    // Accessor data is not compatible with the Vulkan index buffer, so it has to be generated.
                                    const std::size_t byteSize = sizeof(T) * accessor.count;
                                    auto bytes = std::make_unique_for_overwrite<std::byte[]>(byteSize);
                                    copyFromAccessor<T>(asset, accessor, bytes.get(), config.adapter);
                                    return { generatedBytes.emplace_back(std::move(bytes)).get(), byteSize };
                                }
                                else {
                                    // Can use the already loaded buffer view data.
                                    return config.adapter(asset, *accessor.bufferViewIndex).subspan(accessor.byteOffset, sizeof(T) * accessor.count);
                                }
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
                    indexTypeAndFirstIndexByPrimitive.try_emplace(primitive, vk::IndexType::eUint32, bufferSize / sizeof(std::uint32_t));
                    bufferSize += indexBytes.size_bytes();
                }

                unsignedShortIndexOffset = bufferSize;
                for (const auto &[primitive, indexBytes] : unsignedShortIndexBytes) {
                    indexTypeAndFirstIndexByPrimitive.try_emplace(primitive, vk::IndexType::eUint16, (bufferSize - unsignedShortIndexOffset) / sizeof(std::uint16_t));
                    bufferSize += indexBytes.size_bytes();
                }

                unsignedByteIndexOffset = bufferSize;
                for (const auto &[primitive, indexBytes] : unsignedByteIndexBytes) {
                    indexTypeAndFirstIndexByPrimitive.try_emplace(primitive, vk::IndexType::eUint8, (bufferSize - unsignedByteIndexOffset) / sizeof(std::uint8_t));
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
    bool processLineLoopAsLineStrip = true;
    vk::BufferUsageFlags usageFlags = vk::BufferUsageFlagBits::eIndexBuffer;
    vk::ArrayProxy<std::uint32_t> queueFamilies = {};
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