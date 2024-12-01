module;

#include <cassert>
#include <mikktspace.h>

export module vk_gltf_viewer:gltf.AssetGpuBuffers;

import std;
export import glm;
export import thread_pool;
import :gltf.algorithm.MikktSpaceInterface;
export import :gltf.AssetPrimitiveInfo;
import :helpers.functional;
import :helpers.ranges;
export import :vulkan.Gpu;

/**
 * @brief Parse a number from given \p str.
 * @tparam T Type of the number.
 * @param str String to parse.
 * @return Parsed number.
 * @throw std::runtime_error If the parsing failed.
 */
template <std::integral T>
[[nodiscard]] T parse(std::string_view str) {
    T value;
    if (auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value); ec == std::errc{}) {
        return value;
    }

    throw std::runtime_error { std::format("Failed to parse the number from \"{}\"", str) };
}

/**
 * @brief Create a combined staging buffer from given segments (a range of byte data) and return each segments' start offsets.
 *
 * Example: Two segments { 0xAA, 0xBB, 0xCC } and { 0xDD, 0xEE } will be combined to { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE }, and their start offsets are { 0, 3 }.
 *
 * @tparam R Range type of data segments.
 * @param allocator VMA allocator to allocate the staging buffer.
 * @param segments Range of data segments. Each segment will be converted to <tt>std::span<const std::byte></tt>, therefore segment's elements must be trivially copyable.
 * @param usage Usage flags of the result buffer.
 * @return Pair of staging buffer and each segments' start offsets vector.
 */
template <std::ranges::random_access_range R>
    requires std::ranges::contiguous_range<std::ranges::range_value_t<R>>
[[nodiscard]] std::pair<vku::AllocatedBuffer, std::vector<vk::DeviceSize>> createCombinedStagingBuffer(
    vma::Allocator allocator,
    R &&segments,
    vk::BufferUsageFlags usage
) {
    if constexpr (std::convertible_to<std::ranges::range_value_t<R>, std::span<const std::byte>>) {
        assert(!segments.empty() && "Empty segments not allowed (Vulkan requires non-zero buffer size)");

        // Calculate each segments' copy destination offsets.
        std::vector copyOffsets
            = segments
            | std::views::transform([](std::span<const std::byte> segment) -> vk::DeviceSize {
                return segment.size_bytes();
            })
            | std::ranges::to<std::vector>();
        vk::DeviceSize sizeTotal = copyOffsets.back();
        std::exclusive_scan(copyOffsets.begin(), copyOffsets.end(), copyOffsets.begin(), vk::DeviceSize { 0 });
        sizeTotal += copyOffsets.back();

        // Create staging buffer and copy segments into it.
        vku::MappedBuffer stagingBuffer { allocator, vk::BufferCreateInfo { {}, sizeTotal, usage } };
        for (std::byte *mapped = static_cast<std::byte*>(stagingBuffer.data);
            const auto &[segment, copyOffset] : std::views::zip(segments, copyOffsets)){
            std::ranges::copy(segment, mapped + copyOffset);
        }

        return { std::move(stagingBuffer).unmap(), std::move(copyOffsets) };
    }
    else {
        // Retry with converting each segments into the std::span<const std::byte>.
        const auto byteSegments = segments | std::views::transform([](const auto &segment) { return as_bytes(std::span { segment }); });
        return createCombinedStagingBuffer(allocator, byteSegments, usage);
    }
}

namespace vk_gltf_viewer::gltf {
    /**
     * @brief GPU buffers for <tt>fastgltf::Asset</tt>.
     *
     * These buffers could be used for all asset. If you're finding the scene specific buffers (like node transformation matrices, ordered node primitives, etc.), see AssetSceneGpuBuffers for that purpose.
     */
    export class AssetGpuBuffers {
        const fastgltf::Asset &asset;
        const vulkan::Gpu &gpu;

        /**
         * @brief Ordered asset primitives.
         *
         * glTF asset primitives are appeared inside the mesh, and they have no explicit ordering. This vector is constructed by traversing the asset meshes and collect the primitives with their appearing order.
         */
        std::vector<const fastgltf::Primitive*> orderedPrimitives = createOrderedPrimitives();

        /**
         * @brief GPU buffers that would only be accessed by buffer device address.
         *
         * Asset buffer view data that are used by attributes, missing tangents, indexed attribute (e.g. <tt>TEXCOORD_<i></tt>) mapping information are staged into GPU buffer, but these are "unnamed". They are specific to this class' implementation, and cannot be accessed from outside this class. Instead, their device addresses are stored in AssetPrimitiveInfo and could be accessed in the shader.
         */
        std::vector<vku::AllocatedBuffer> internalBuffers;

        /**
         * @brief Staging buffers and their copy infos.
         *
         * Consisted of <tt>AllocatedBuffer</tt> that contains the data, <tt>Buffer</tt> that will be copied into, and <tt>BufferCopy</tt> that describes the copy region.
         * This should be cleared after the staging operation ends.
         */
        std::vector<std::tuple<vku::AllocatedBuffer, vk::Buffer, vk::BufferCopy>> stagingInfos;

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
        };

        struct GpuPrimitive {
            vk::DeviceAddress pPositionBuffer;
            vk::DeviceAddress pNormalBuffer;
            vk::DeviceAddress pTangentBuffer;
            vk::DeviceAddress pTexcoordAttributeMappingInfoBuffer;
            vk::DeviceAddress pColorAttributeMappingInfoBuffer;
            std::uint8_t positionByteStride;
            std::uint8_t normalByteStride;
            std::uint8_t tangentByteStride;
            char padding[1];
            std::uint32_t materialIndex;
        };

        std::unordered_map<const fastgltf::Primitive*, AssetPrimitiveInfo> primitiveInfos = createPrimitiveInfos();

        /**
         * @brief Buffer that contains <tt>GpuMaterial</tt>s, with fallback material at the index 0 (total <tt>asset.materials.size() + 1</tt>).
         */
        vku::AllocatedBuffer materialBuffer = createMaterialBuffer();

        /**
         * @brief All indices that are combined as a single buffer by their index types.
         *
         * Use <tt>AssetPrimitiveInfo::indexInfo</tt> to get the offset of data that is used by the primitive.
         *
         * @note If you passed <tt>vulkan::Gpu</tt> whose <tt>supportUint8Index</tt> is <tt>false</tt>, primitive with unsigned byte (<tt>uint8_t</tt>) indices will be converted to unsigned short (<tt>uint16_t</tt>) indices.
         */
        std::unordered_map<vk::IndexType, vku::AllocatedBuffer> indexBuffers;

        /**
         * @brief Buffer that contains <tt>GpuPrimitive</tt>s.
         */
        vku::AllocatedBuffer primitiveBuffer = createPrimitiveBuffer();

        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        AssetGpuBuffers(
            const fastgltf::Asset &asset,
            const vulkan::Gpu &gpu,
            BS::thread_pool &threadPool,
            const BufferDataAdapter &adapter = {}
        ) : asset { asset },
            gpu { gpu },
            // Ensure the order of function execution:
            // Primitive attribute buffers MUST be created before index buffer creation (because fill the AssetPrimitiveInfo
            // and determine the drawCount if primitive is non-indexed, and createIndexBuffers() will use it).
            indexBuffers { (createPrimitiveAttributeBuffers(adapter), createPrimitiveIndexBuffers(adapter)) },
            // Remaining buffers MUST be created before the primitive buffer creation (because they fill the
            // AssetPrimitiveInfo and createPrimitiveBuffer() will stage it).
            primitiveBuffer { (createPrimitiveIndexedAttributeMappingBuffers(), createPrimitiveTangentBuffers(threadPool, adapter), createPrimitiveBuffer()) } {
            if (!stagingInfos.empty()) {
                // Transfer the asset resources into the GPU using transfer queue.
                const vk::raii::CommandPool transferCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.transfer } };
                const vk::raii::Fence transferFence { gpu.device, vk::FenceCreateInfo{} };
                vku::executeSingleCommand(*gpu.device, *transferCommandPool, gpu.queues.transfer, [&](vk::CommandBuffer cb) {
                    for (const auto &[srcBuffer, dstBuffer, copyRegion] : stagingInfos) {
                        cb.copyBuffer(srcBuffer, dstBuffer, copyRegion);
                    }
                }, *transferFence);
                std::ignore = gpu.device.waitForFences(*transferFence, true, ~0ULL); // TODO: failure handling
                stagingInfos.clear();
            }
        }

        /**
         * @brief Get the primitive by its order, which has the same manner of <tt>primitiveBuffer</tt>.
         * @param index The order of the primitive.
         * @return The primitive.
         */
        [[nodiscard]] const fastgltf::Primitive &getPrimitiveByOrder(std::uint16_t index) const { return *orderedPrimitives[index]; }

    private:
        [[nodiscard]] std::vector<const fastgltf::Primitive*> createOrderedPrimitives() const;
        [[nodiscard]] std::unordered_map<const fastgltf::Primitive*, AssetPrimitiveInfo> createPrimitiveInfos() const;
        [[nodiscard]] vku::AllocatedBuffer createMaterialBuffer();

        template <typename BufferDataAdapter>
        [[nodiscard]] std::unordered_map<vk::IndexType, vku::AllocatedBuffer> createPrimitiveIndexBuffers(const BufferDataAdapter &adapter) {
            // Primitive that are contains an indices accessor.
            auto indexedPrimitives = asset.meshes
                | std::views::transform(&fastgltf::Mesh::primitives)
                | std::views::join
                | std::views::filter([](const fastgltf::Primitive &primitive) { return primitive.indicesAccessor.has_value(); });

            // Index data is either
            // - span of the buffer view region, or
            // - vector of uint16 indices if accessor have unsigned byte component and native uint8 index is not supported.
            std::vector<std::vector<std::uint16_t>> generated16BitIndices;
            std::unordered_map<vk::IndexType, std::vector<std::pair<const fastgltf::Primitive*, std::span<const std::byte>>>> indexBufferBytesByType;

            // Get buffer view bytes from indexedPrimitives and group them by index type.
            for (const fastgltf::Primitive &primitive : indexedPrimitives) {
                const fastgltf::Accessor &accessor = asset.accessors[*primitive.indicesAccessor];

                // Check accessor validity.
                if (accessor.sparse) throw std::runtime_error { "Sparse indices accessor not supported" };
                if (accessor.normalized) throw std::runtime_error { "Normalized indices accessor not supported" };

                // Vulkan does not support interleaved index buffer.
                if (const auto& byteStride = asset.bufferViews[*accessor.bufferViewIndex].byteStride) {
                    const std::size_t componentByteSize = getElementByteSize(accessor.type, accessor.componentType);
                    if (*byteStride != componentByteSize) {
                        throw std::runtime_error { "Interleaved index buffer not supported" };
                    }
                }

                if (accessor.componentType == fastgltf::ComponentType::UnsignedByte && !gpu.supportUint8Index) {
                    // Make vector of uint16 indices.
                    std::vector<std::uint16_t> indices(accessor.count);
                    iterateAccessorWithIndex<std::uint8_t>(asset, accessor, [&](std::uint8_t index, std::size_t i) {
                        indices[i] = index; // Index converted from uint8 to uint16.
                    }, adapter);

                    indexBufferBytesByType[vk::IndexType::eUint16].emplace_back(
                        &primitive,
                        as_bytes(std::span { generated16BitIndices.emplace_back(std::move(indices)) }));
                }
                else {
                    const vk::IndexType indexType = [&]() -> vk::IndexType {
                        switch (accessor.componentType) {
                            case fastgltf::ComponentType::UnsignedByte: return vk::IndexType::eUint8EXT;
                            case fastgltf::ComponentType::UnsignedShort: return vk::IndexType::eUint16;
                            case fastgltf::ComponentType::UnsignedInt: return vk::IndexType::eUint32;
                            default: throw std::runtime_error { "Unsupported index type: only Uint8EXT, Uint16 and Uint32 are supported." };
                        }
                    }();

                    indexBufferBytesByType[indexType].emplace_back(&primitive, getByteRegion(asset, accessor, adapter));
                }
            }

            return indexBufferBytesByType
                | ranges::views::decompose_transform([&](vk::IndexType indexType, const auto &primitiveAndIndexBytesPairs) {
                    auto [buffer, copyOffsets] = createCombinedStagingBuffer(
                        gpu.allocator,
                        primitiveAndIndexBytesPairs | std::views::values,
                        gpu.isUmaDevice ? vk::BufferUsageFlagBits::eIndexBuffer : vk::BufferUsageFlagBits::eTransferSrc);

                    if (!gpu.isUmaDevice) {
                        vku::AllocatedBuffer dstBuffer { gpu.allocator, vk::BufferCreateInfo {
                            {},
                            buffer.size,
                            vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
                        } };
                        stagingInfos.emplace_back(
                            std::move(buffer),
                            dstBuffer,
                            vk::BufferCopy { 0, 0, dstBuffer.size });
                        buffer = std::move(dstBuffer);
                    }

                    for (auto [pPrimitive, offset] : std::views::zip(primitiveAndIndexBytesPairs | std::views::keys, copyOffsets)) {
                        AssetPrimitiveInfo &primitiveInfo = primitiveInfos[pPrimitive];
                        primitiveInfo.indexInfo.emplace(offset, indexType);
                        primitiveInfo.drawCount = asset.accessors[*pPrimitive->indicesAccessor].count;
                    }

                    return std::pair { indexType, std::move(buffer) };
                })
                | std::ranges::to<std::unordered_map>();
        }

        [[nodiscard]] vku::AllocatedBuffer createPrimitiveBuffer();

        template <typename DataBufferAdapter>
        void createPrimitiveAttributeBuffers(const DataBufferAdapter &adapter) {
            const auto primitives = asset.meshes | std::views::transform(&fastgltf::Mesh::primitives) | std::views::join;

            // Get buffer view indices that are used in primitive attributes.
            const std::unordered_set attributeBufferViewIndices
                = primitives
                | std::views::transform([](const fastgltf::Primitive &primitive) {
                    return primitive.attributes | std::views::transform(&fastgltf::Attribute::accessorIndex);
                })
                | std::views::join
                | std::views::transform([&](std::size_t accessorIndex) {
                    const fastgltf::Accessor &accessor = asset.accessors[accessorIndex];

                    // Check accessor validity.
                    if (accessor.sparse) throw std::runtime_error { "Sparse accessor is not supported." };
                    if (accessor.normalized) throw std::runtime_error { "Normalized accessor is not supported." };

                    return *accessor.bufferViewIndex;
                })
                | std::ranges::to<std::unordered_set>();

            // Make an ordered sequence of (bufferViewIndex, bufferViewBytes) pairs.
            const std::vector attributeBufferViewBytes
                = attributeBufferViewIndices
                | std::views::transform([&](std::size_t bufferViewIndex) {
                    return std::pair { bufferViewIndex, adapter(asset, bufferViewIndex) };
                })
                | std::ranges::to<std::vector>();

            auto [buffer, copyOffsets] = createCombinedStagingBuffer(
                gpu.allocator,
                attributeBufferViewBytes | std::views::values,
                gpu.isUmaDevice
                    ? vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress
                    : vk::BufferUsageFlagBits::eTransferSrc);

            if (!gpu.isUmaDevice && !vku::contains(gpu.allocator.getAllocationMemoryProperties(buffer.allocation), vk::MemoryPropertyFlagBits::eDeviceLocal)) {
                vku::AllocatedBuffer dstBuffer { gpu.allocator, vk::BufferCreateInfo {
                    {},
                    buffer.size,
                    vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
                } };
                stagingInfos.emplace_back(
                    std::move(buffer),
                    dstBuffer,
                    vk::BufferCopy { 0, 0, dstBuffer.size });
                buffer = std::move(dstBuffer);
            }

            // Hashmap that can get buffer device address by corresponding buffer view index.
            const std::unordered_map bufferDeviceAddressMappings
                = std::views::zip(
                    attributeBufferViewBytes | std::views::keys,
                    copyOffsets | std::views::transform([baseAddress = gpu.device.getBufferAddress({ buffer })](vk::DeviceSize offset) {
                        return baseAddress + offset;
                    }))
                | std::ranges::to<std::unordered_map>();

            // Iterate over the primitives and set their attribute infos.
            for (auto &[pPrimitive, primitiveInfo] : primitiveInfos) {
                for (const auto &[attributeName, accessorIndex] : pPrimitive->attributes) {
                    const fastgltf::Accessor &accessor = asset.accessors[accessorIndex];
                    const auto getAttributeBufferInfo = [&]() -> AssetPrimitiveInfo::AttributeBufferInfo {
                        const std::size_t byteStride
                            = asset.bufferViews[*accessor.bufferViewIndex].byteStride
                            .value_or(getElementByteSize(accessor.type, accessor.componentType));
                        if (!std::in_range<std::uint8_t>(byteStride)) throw std::runtime_error { "Too large byteStride" };
                        return {
                            .address = bufferDeviceAddressMappings.at(*accessor.bufferViewIndex) + accessor.byteOffset,
                            .byteStride = static_cast<std::uint8_t>(byteStride),
                        };
                    };

                    using namespace std::string_view_literals;

                    if (attributeName == "POSITION"sv) {
                        primitiveInfo.positionInfo = getAttributeBufferInfo();
                        primitiveInfo.drawCount = accessor.count;
                        primitiveInfo.min = glm::make_vec3(get_if<std::pmr::vector<double>>(&accessor.min)->data());
                        primitiveInfo.max = glm::make_vec3(get_if<std::pmr::vector<double>>(&accessor.max)->data());
                    }
                    else if (attributeName == "NORMAL"sv) {
                        primitiveInfo.normalInfo.emplace(getAttributeBufferInfo());
                    }
                    else if (attributeName == "TANGENT"sv) {
                        primitiveInfo.tangentInfo.emplace(getAttributeBufferInfo());
                    }
                    else if (constexpr auto prefix = "TEXCOORD_"sv; attributeName.starts_with(prefix)) {
                        const std::size_t index = parse<std::size_t>(std::string_view { attributeName }.substr(prefix.size()));

                        if (primitiveInfo.texcoordsInfo.attributeInfos.size() <= index) {
                            primitiveInfo.texcoordsInfo.attributeInfos.resize(index + 1);
                        }
                        primitiveInfo.texcoordsInfo.attributeInfos[index] = getAttributeBufferInfo();
                    }
                    // TODO: COLOR_<i> attribute processing.
                }
            }

            internalBuffers.emplace_back(std::move(buffer));
        }

        void createPrimitiveIndexedAttributeMappingBuffers();

        template <typename BufferDataAdapter>
        void createPrimitiveTangentBuffers(BS::thread_pool &threadPool, const BufferDataAdapter &adapter) {
            // Collect primitives that are missing tangent attributes (and require it).
            std::vector missingTangentPrimitives
                = primitiveInfos
                | std::views::filter(decomposer([&](const fastgltf::Primitive *pPrimitive, AssetPrimitiveInfo &primitiveInfo) {
                    // Skip if primitive already has a tangent attribute.
                    if (primitiveInfo.tangentInfo) return false;
                    // Skip if primitive doesn't have a material.
                    if (const auto &materialIndex = pPrimitive->materialIndex; !materialIndex) return false;
                    // Skip if primitive doesn't have a normal texture.
                    else if (!asset.materials[*materialIndex].normalTexture) return false;
                    // Skip if primitive is non-indexed geometry (screen-space normal and tangent will be generated in the shader).
                    return pPrimitive->indicesAccessor.has_value();
                }))
                | std::views::transform(decomposer([&](const fastgltf::Primitive *pPrimitive, AssetPrimitiveInfo &primitiveInfo) {
                    // Validate the constraints for MikktSpaceInterface.
                    if (auto normalIt = pPrimitive->findAttribute("NORMAL"); normalIt == pPrimitive->attributes.end()) {
                        throw std::runtime_error { "Missing NORMAL attribute" };
                    }
                    else if (auto texcoordIt = pPrimitive->findAttribute(std::format("TEXCOORD_{}", asset.materials[*pPrimitive->materialIndex].normalTexture->texCoordIndex));
                        texcoordIt == pPrimitive->attributes.end()) {
                        throw std::runtime_error { "Missing TEXCOORD attribute" };
                    }
                    else {
                        return std::pair<AssetPrimitiveInfo*, algorithm::MikktSpaceMesh<BufferDataAdapter>> {
                            std::piecewise_construct,
                            std::tuple { &primitiveInfo },
                            std::tie(
                                asset,
                                asset.accessors[*pPrimitive->indicesAccessor],
                                asset.accessors[pPrimitive->findAttribute("POSITION")->accessorIndex],
                                asset.accessors[normalIt->accessorIndex],
                                asset.accessors[texcoordIt->accessorIndex],
                                adapter),
                        };
                    }
                }))
                | std::ranges::to<std::vector>();

            if (missingTangentPrimitives.empty()) {
                return;
            }

            threadPool.submit_loop(std::size_t{ 0 }, missingTangentPrimitives.size(), [&](std::size_t i) {
                auto& mesh = missingTangentPrimitives[i].second;

                SMikkTSpaceInterface* const pInterface
                    = [indexType = mesh.indicesAccessor.componentType]() -> SMikkTSpaceInterface* {
                        switch (indexType) {
                            case fastgltf::ComponentType::UnsignedByte:
                                return &algorithm::mikktSpaceInterface<std::uint8_t, BufferDataAdapter>;
                            case fastgltf::ComponentType::UnsignedShort:
                                return &algorithm::mikktSpaceInterface<std::uint16_t, BufferDataAdapter>;
                            case fastgltf::ComponentType::UnsignedInt:
                                return &algorithm::mikktSpaceInterface<std::uint32_t, BufferDataAdapter>;
                            default:
                                throw std::runtime_error{ "Unsupported index type: only unsigned byte/short/int are supported." };
                        }
                    }();
                if (const SMikkTSpaceContext context{ pInterface, &mesh }; !genTangSpaceDefault(&context)) {
                    throw std::runtime_error{ "Failed to generate the tangent attributes" };
                }
            }).get();

            auto [buffer, copyOffsets] = createCombinedStagingBuffer(
                gpu.allocator,
                missingTangentPrimitives | std::views::transform([](const auto &pair) {
                    return as_bytes(std::span { pair.second.tangents });
                }),
                gpu.isUmaDevice
                    ? vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress
                    : vk::BufferUsageFlagBits::eTransferSrc);

            if (!gpu.isUmaDevice && !vku::contains(gpu.allocator.getAllocationMemoryProperties(buffer.allocation), vk::MemoryPropertyFlagBits::eDeviceLocal)) {
                vku::AllocatedBuffer dstBuffer { gpu.allocator, vk::BufferCreateInfo {
                    {},
                    buffer.size,
                    vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
                } };
                stagingInfos.emplace_back(
                    std::move(buffer),
                    dstBuffer,
                    vk::BufferCopy { 0, 0, dstBuffer.size });
                buffer = std::move(dstBuffer);
            }

            for (vk::DeviceAddress baseAddress = gpu.device.getBufferAddress({ buffer });
                auto [pPrimitive, copyOffset] : std::views::zip(missingTangentPrimitives | std::views::keys, copyOffsets)) {
                pPrimitive->tangentInfo.emplace(baseAddress + copyOffset, 16);
            }

            internalBuffers.emplace_back(std::move(buffer));
        }
    };
}