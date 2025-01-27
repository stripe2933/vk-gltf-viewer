module;

#include <cassert>
#include <mikktspace.h>

export module vk_gltf_viewer:gltf.AssetGpuBuffers;

import std;
export import BS.thread_pool;
export import glm;
import :gltf.algorithm.MikktSpaceInterface;
export import :gltf.AssetPrimitiveInfo;
export import :gltf.AssetProcessError;
import :helpers.fastgltf;
import :helpers.functional;
import :helpers.ranges;
import :helpers.type_map;
import :vulkan.buffer;
export import :vulkan.buffer.Materials;
export import :vulkan.Gpu;

/**
 * @brief Parse a number from given \p str.
 * @tparam T Type of the number.
 * @param str String to parse.
 * @return Expected of parsed number, or error code if failed.
 */
template <std::integral T>
[[nodiscard]] std::expected<T, std::errc> parse(std::string_view str) {
    T value;
    if (auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value); ec == std::errc{}) {
        return value;
    }
    else {
        return std::unexpected { ec };
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
        struct GpuPrimitive {
            vk::DeviceAddress pPositionBuffer;
            vk::DeviceAddress pNormalBuffer;
            vk::DeviceAddress pTangentBuffer;
            vk::DeviceAddress pTexcoordAttributeMappingInfoBuffer;
            vk::DeviceAddress pColorBuffer;
            std::uint8_t positionByteStride;
            std::uint8_t normalByteStride;
            std::uint8_t tangentByteStride;
            std::uint8_t colorByteStride;
            std::uint32_t materialIndex;
        };

        std::unordered_map<const fastgltf::Primitive*, AssetPrimitiveInfo> primitiveInfos = createPrimitiveInfos();

        /**
         * @brief All indices that are combined as a single buffer by their index types.
         *
         * Use <tt>AssetPrimitiveInfo::indexInfo</tt> to get the offset of data that is used by the primitive.
         *
         * @note If you passed <tt>vulkan::Gpu</tt> whose <tt>supportUint8Index</tt> is <tt>false</tt>, primitive with unsigned byte (<tt>uint8_t</tt>) indices will be converted to unsigned short (<tt>uint16_t</tt>) indices.
         */
        std::unordered_map<vk::IndexType, vku::AllocatedBuffer> indexBuffers;

    private:
        std::reference_wrapper<const vulkan::buffer::Materials> materialBuffer;
        /**
         * @brief Buffer that contains <tt>GpuPrimitive</tt>s.
         */
        std::variant<vku::AllocatedBuffer, vku::MappedBuffer> primitiveBuffer;

    public:
        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        AssetGpuBuffers(
            const fastgltf::Asset &asset,
            const vulkan::buffer::Materials &materialBuffer [[clang::lifetimebound]],
            const vulkan::Gpu &gpu,
            BS::thread_pool<> &threadPool,
            const BufferDataAdapter &adapter = {}
        ) : asset { asset },
            gpu { gpu },
            // Ensure the order of function execution:
            indexBuffers { createPrimitiveIndexBuffers(adapter) },
            materialBuffer { materialBuffer },
            // Remaining buffers MUST be created before the primitive buffer creation (because they fill the
            // AssetPrimitiveInfo and createPrimitiveBuffer() will stage it).
            primitiveBuffer { (createPrimitiveAttributeBuffers(adapter), createPrimitiveIndexedAttributeMappingBuffers(), createPrimitiveTangentBuffers(threadPool, adapter), createPrimitiveBuffer()) } {
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

        [[nodiscard]] vk::Buffer getPrimitiveBuffer() const noexcept { return visit_as<vk::Buffer>(primitiveBuffer); }

        /**
         * @brief Get the primitive by its order, which has the same manner of <tt>primitiveBuffer</tt>.
         * @param index The order of the primitive.
         * @return The primitive.
         */
        [[nodiscard]] const fastgltf::Primitive &getPrimitiveByOrder(std::uint16_t index) const { return *orderedPrimitives[index]; }

        /**
         * @brief Update \p primitive's material index inside the GPU buffer.
         * @param primitive Primitive to update.
         * @param materialIndex New material index.
         * @param transferCommandBuffer If buffer is not host-visible memory and so is unable to be updated from the host, this command buffer will be used for recording the buffer update command. Then, its execution MUST be synchronized to be available to the <tt>primitiveBuffer</tt>'s usage. Otherwise, this parameter is not used.
         * @return <tt>true</tt> if the buffer is not host-visible memory and the update command is recorded, <tt>false</tt> otherwise.
         */
        [[nodiscard]] bool updatePrimitiveMaterial(const fastgltf::Primitive &primitive, std::uint32_t materialIndex, vk::CommandBuffer transferCommandBuffer);

    private:
        [[nodiscard]] std::vector<const fastgltf::Primitive*> createOrderedPrimitives() const;
        [[nodiscard]] std::unordered_map<const fastgltf::Primitive*, AssetPrimitiveInfo> createPrimitiveInfos() const;

        template <typename BufferDataAdapter>
        [[nodiscard]] std::unordered_map<vk::IndexType, vku::AllocatedBuffer> createPrimitiveIndexBuffers(const BufferDataAdapter &adapter) {
            // Primitive that are contains an indices accessor.
            auto indexedPrimitives = asset.meshes
                | std::views::transform(&fastgltf::Mesh::primitives)
                | std::views::join
                | std::views::filter([](const fastgltf::Primitive &primitive) { return primitive.indicesAccessor.has_value(); });

            // Index data is either
            // - span of the buffer view region, or
            // - generated indices if accessor data layout couldn't be represented in Vulkan buffer.
            std::vector<std::vector<std::byte>> generatedIndexBytes;
            std::unordered_map<vk::IndexType, std::vector<std::pair<const fastgltf::Primitive*, std::span<const std::byte>>>> indexBufferBytesByType;

            // Get buffer view bytes from indexedPrimitives and group them by index type.
            for (const fastgltf::Primitive &primitive : indexedPrimitives) {
                const fastgltf::Accessor &accessor = asset.accessors[*primitive.indicesAccessor];

                bool shouldGenerateIndices = false;

                // Sparse accessor have to be handled.
                shouldGenerateIndices |= accessor.sparse.has_value();

                // Vulkan does not support interleaved index buffer.
                if (const auto& byteStride = asset.bufferViews[*accessor.bufferViewIndex].byteStride) {
                    const bool isAccessorStrided = *byteStride != getElementByteSize(accessor.type, accessor.componentType);
                    shouldGenerateIndices |= isAccessorStrided;
                }

                // Unsigned byte indices have to be converted to uint16 if the device does not support it.
                shouldGenerateIndices |= accessor.componentType == fastgltf::ComponentType::UnsignedByte && !gpu.supportUint8Index;

                if (shouldGenerateIndices) {
                    constexpr type_map indexGenerationTypeMap {
                        make_type_map_entry<std::pair<std::uint8_t, std::uint8_t>>(0),
                        make_type_map_entry<std::pair<std::uint16_t, std::uint16_t>>(1),
                        make_type_map_entry<std::pair<std::uint32_t, std::uint32_t>>(2),
                        make_type_map_entry<std::pair<std::uint8_t, std::uint16_t>>(3),
                    };

                    const int key = [&]() {
                        switch (accessor.componentType) {
                            case fastgltf::ComponentType::UnsignedByte:
                                return gpu.supportUint8Index ? 0 : 3;
                            case fastgltf::ComponentType::UnsignedShort:
                                return 1;
                            case fastgltf::ComponentType::UnsignedInt:
                                return 2;
                            default:
                                // glTF Specification:
                                // The indices accessor MUST have SCALAR type and an unsigned integer component type.
                                std::unreachable();
                        }
                    }();
                    visit([&]<typename SrcT, typename DstT>(std::type_identity<std::pair<SrcT, DstT>>) {
                        std::vector<std::byte> indexBytes(sizeof(DstT) * accessor.count);
                        if constexpr (std::same_as<SrcT, DstT>) {
                            copyFromAccessor<DstT>(asset, accessor, indexBytes.data(), adapter);
                        }
                        else {
                            iterateAccessorWithIndex<SrcT>(asset, accessor, [&](SrcT index, std::size_t i) {
                                *reinterpret_cast<DstT*>(indexBytes.data() + sizeof(DstT) * i) = index; // Index converted from uint8 to uint16.
                            }, adapter);
                        }

                        indexBufferBytesByType[vk::IndexTypeValue<DstT>::value].emplace_back(
                            &primitive,
                            generatedIndexBytes.emplace_back(std::move(indexBytes)));
                    }, indexGenerationTypeMap.get_variant(key));
                }
                else {
                    const vk::IndexType indexType = [&]() -> vk::IndexType {
                        switch (accessor.componentType) {
                            case fastgltf::ComponentType::UnsignedByte: return vk::IndexType::eUint8EXT;
                            case fastgltf::ComponentType::UnsignedShort: return vk::IndexType::eUint16;
                            case fastgltf::ComponentType::UnsignedInt: return vk::IndexType::eUint32;
                            default:
                                // glTF Specification:
                                // The indices accessor MUST have SCALAR type and an unsigned integer component type.
                                std::unreachable();
                        }
                    }();

                    indexBufferBytesByType[indexType].emplace_back(&primitive, getByteRegion(asset, accessor, adapter));
                }
            }

            return indexBufferBytesByType
                | ranges::views::decompose_transform([&](vk::IndexType indexType, const auto &primitiveAndIndexBytesPairs) {
                    auto [buffer, copyOffsets] = vulkan::buffer::createCombinedBuffer<true>(
                        gpu.allocator,
                        primitiveAndIndexBytesPairs | std::views::values,
                        gpu.isUmaDevice ? vk::BufferUsageFlagBits::eIndexBuffer : vk::BufferUsageFlagBits::eTransferSrc);
                    stageIfNeeded(buffer, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer);

                    for (auto [pPrimitive, offset] : std::views::zip(primitiveAndIndexBytesPairs | std::views::keys, copyOffsets)) {
                        AssetPrimitiveInfo &primitiveInfo = primitiveInfos[pPrimitive];
                        primitiveInfo.indexInfo.emplace(offset, indexType);
                    }

                    return std::pair { indexType, std::move(buffer) };
                })
                | std::ranges::to<std::unordered_map>();
        }

        [[nodiscard]] std::variant<vku::AllocatedBuffer, vku::MappedBuffer> createPrimitiveBuffer();

        template <typename DataBufferAdapter>
        void createPrimitiveAttributeBuffers(const DataBufferAdapter &adapter) {
            const auto primitives = asset.meshes | std::views::transform(&fastgltf::Mesh::primitives) | std::views::join;

            // Get buffer view indices that are used in primitive attributes.
            std::unordered_set<std::size_t> attributeBufferViewIndices;
            for (const fastgltf::Primitive &primitive : primitives) {
                for (const fastgltf::Attribute &attribute : primitive.attributes) {
                    // Process only used attributes.
                    using namespace std::string_view_literals;
                    const bool isAttributeUsed
                        = ranges::one_of(attribute.name, "POSITION"sv, "NORMAL"sv, "TANGENT"sv, "COLOR_0"sv)
                        || attribute.name.starts_with("TEXCOORD_"sv);
                    if (!isAttributeUsed) continue;

                    const fastgltf::Accessor &accessor = asset.accessors[attribute.accessorIndex];

                    // Check accessor validity.
                    if (accessor.sparse) throw AssetProcessError::SparseAttributeBufferAccessor;

                    attributeBufferViewIndices.emplace(accessor.bufferViewIndex.value());
                }
            }

            // Make an ordered sequence of (bufferViewIndex, bufferViewBytes) pairs.
            const std::vector attributeBufferViewBytes
                = attributeBufferViewIndices
                | std::views::transform([&](std::size_t bufferViewIndex) {
                    return std::pair { bufferViewIndex, adapter(asset, bufferViewIndex) };
                })
                | std::ranges::to<std::vector>();

            auto [buffer, copyOffsets] = vulkan::buffer::createCombinedBuffer<true>(
                gpu.allocator,
                attributeBufferViewBytes | std::views::values,
                gpu.isUmaDevice
                    ? vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress
                    : vk::BufferUsageFlagBits::eTransferSrc);
            stageIfNeeded(buffer, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);

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
                // Get number of TEXCOORD_<i> attributes.
                const auto attributeNames
                    = pPrimitive->attributes
                    | std::views::transform([](const auto &attribute) -> std::string_view { return attribute.name; });
                const std::size_t texcoordCount = std::ranges::count_if(
                    attributeNames,
                    [](std::string_view name) { return name.starts_with("TEXCOORD_"); });
                primitiveInfo.texcoordsInfo.attributeInfos.resize(texcoordCount);

                // Process the attributes by identifying their names.
                for (const auto &[attributeName, accessorIndex] : pPrimitive->attributes) {
                    const fastgltf::Accessor &accessor = asset.accessors[accessorIndex];
                    const auto getAttributeBufferInfo = [&]() -> AssetPrimitiveInfo::AttributeBufferInfo {
                        const std::size_t byteStride
                            = asset.bufferViews[*accessor.bufferViewIndex].byteStride
                            .value_or(getElementByteSize(accessor.type, accessor.componentType));
                        if (!std::in_range<std::uint8_t>(byteStride)) throw AssetProcessError::TooLargeAccessorByteStride;
                        return {
                            .address = bufferDeviceAddressMappings.at(*accessor.bufferViewIndex) + accessor.byteOffset,
                            .byteStride = static_cast<std::uint8_t>(byteStride),
                            .componentType = accessor.componentType,
                        };
                    };

                    using namespace std::string_view_literals;

                    if (attributeName == "POSITION"sv) {
                        primitiveInfo.positionInfo = getAttributeBufferInfo();
                    }
                    else if (attributeName == "NORMAL"sv) {
                        primitiveInfo.normalInfo.emplace(getAttributeBufferInfo());
                    }
                    else if (attributeName == "TANGENT"sv) {
                        primitiveInfo.tangentInfo.emplace(getAttributeBufferInfo());
                    }
                    else if (constexpr auto prefix = "TEXCOORD_"sv; attributeName.starts_with(prefix)) {
                        std::size_t index;
                        if (auto result = parse<std::size_t>(std::string_view { attributeName }.substr(prefix.size()))) {
                            index = *result;
                        }
                        else {
                            // Attribute name starting with "TEXCOORD_", but the following string is not a number.
                            // TODO: would it be filtered by the glTF validation?
                            throw fastgltf::Error::InvalidOrMissingAssetField;
                        }

                        primitiveInfo.texcoordsInfo.attributeInfos[index] = getAttributeBufferInfo();
                    }
                    else if (attributeName == "COLOR_0"sv) {
                        primitiveInfo.colorInfo.emplace(getAttributeBufferInfo(), getNumComponents(accessor.type));
                    }
                }
            }

            internalBuffers.emplace_back(std::move(buffer));
        }

        void createPrimitiveIndexedAttributeMappingBuffers();

        template <typename BufferDataAdapter>
        void createPrimitiveTangentBuffers(BS::thread_pool<> &threadPool, const BufferDataAdapter &adapter) {
            // Collect primitives that are missing tangent attributes (and require it).
            std::vector missingTangentPrimitives
                = primitiveInfos
                | std::views::filter(decomposer([&](const fastgltf::Primitive *pPrimitive, AssetPrimitiveInfo &primitiveInfo) {
                    // Skip if primitive already has a tangent attribute.
                    if (primitiveInfo.tangentInfo) return false;

                    const auto &materialIndex = pPrimitive->materialIndex;
                    // Skip if primitive doesn't have a material.
                    if (!materialIndex) return false;
                    // Skip if primitive material doesn't have a normal texture.
                    if (!asset.materials[*materialIndex].normalTexture) return false;

                    // Skip if primitive is non-indexed geometry (screen-space normal and tangent will be generated in the shader).
                    if (!pPrimitive->indicesAccessor) return false;

                    // Skip if primitive doesn't have normal attribute (screen-space normal and tangent will be generated in the shader).
                    if (!primitiveInfo.normalInfo.has_value()) return false;

                    return true;
                }))
                | std::views::transform(decomposer([&](const fastgltf::Primitive *pPrimitive, AssetPrimitiveInfo &primitiveInfo) {
                    const std::size_t texcoordIndex = getTexcoordIndex(*asset.materials[*pPrimitive->materialIndex].normalTexture);
                    return std::pair<AssetPrimitiveInfo*, algorithm::MikktSpaceMesh<BufferDataAdapter>> {
                        std::piecewise_construct,
                        std::tuple { &primitiveInfo },
                        std::tie(
                            asset,
                            asset.accessors[*pPrimitive->indicesAccessor],
                            asset.accessors[pPrimitive->findAttribute("POSITION")->accessorIndex],
                            asset.accessors[pPrimitive->findAttribute("NORMAL")->accessorIndex],
                            asset.accessors[pPrimitive->findAttribute(std::format("TEXCOORD_{}", texcoordIndex))->accessorIndex],
                            adapter),
                    };
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
                                // glTF Specification:
                                // The indices accessor MUST have SCALAR type and an unsigned integer component type.
                                std::unreachable();
                        }
                    }();
                if (const SMikkTSpaceContext context{ pInterface, &mesh }; !genTangSpaceDefault(&context)) {
                    throw std::runtime_error{ "Failed to generate the tangent attributes" };
                }
            }).get();

            auto [buffer, copyOffsets] = vulkan::buffer::createCombinedBuffer<true>(
                gpu.allocator,
                missingTangentPrimitives | std::views::transform([](const auto &pair) {
                    return as_bytes(std::span { pair.second.tangents });
                }),
                gpu.isUmaDevice
                    ? vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress
                    : vk::BufferUsageFlagBits::eTransferSrc);
            stageIfNeeded(buffer, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);

            for (vk::DeviceAddress baseAddress = gpu.device.getBufferAddress({ buffer });
                auto [pPrimitive, copyOffset] : std::views::zip(missingTangentPrimitives | std::views::keys, copyOffsets)) {
                pPrimitive->tangentInfo.emplace(baseAddress + copyOffset, 16);
            }

            internalBuffers.emplace_back(std::move(buffer));
        }

        [[nodiscard]] bool needStaging(const vku::AllocatedBuffer &buffer) const noexcept;
        void stage(vku::AllocatedBuffer &buffer, vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eTransferDst);
        void stageIfNeeded(vku::AllocatedBuffer &buffer, vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eTransferDst);
    };
}