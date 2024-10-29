module;

#include <cassert>

export module vk_gltf_viewer:gltf.AssetGpuBuffers;

import std;
export import glm;
import thread_pool;
export import :gltf.AssetExternalBuffers;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::gltf {
    /**
     * @brief GPU buffers for <tt>fastgltf::Asset</tt>.
     *
     * These buffers could be used for all asset. If you're finding the scene specific buffers (like node transformation
     * matrices, ordered node primitives, etc.), see AssetSceneGpuBuffers for that purpose.
     */
    export class AssetGpuBuffers {
        const fastgltf::Asset &asset;
        const vulkan::Gpu &gpu;

        /**
         * Staging buffers for temporary data transfer. This have to be cleared after the transfer command execution
         * finished.
         */
        std::forward_list<vku::AllocatedBuffer> stagingBuffers;

    public:
        enum class IndexedAttribute { Texcoord, Color };

        struct PrimitiveInfo {
            struct IndexBufferInfo { vk::DeviceSize offset; vk::IndexType type; };
            struct AttributeBufferInfo { vk::DeviceAddress address; std::uint8_t byteStride; };
            struct IndexedAttributeMappingInfo { vk::DeviceAddress pMappingBuffer; };

            std::optional<std::size_t> materialIndex;
            std::uint32_t drawCount;
            std::optional<IndexBufferInfo> indexInfo{};
            AttributeBufferInfo positionInfo;
            std::optional<AttributeBufferInfo> normalInfo;
            std::optional<AttributeBufferInfo> tangentInfo;
            std::vector<AttributeBufferInfo> texcoordInfos;
            std::vector<AttributeBufferInfo> colorInfos;
            std::unordered_map<IndexedAttribute, IndexedAttributeMappingInfo> indexedAttributeMappingInfos;
            glm::dvec3 min;
            glm::dvec3 max;
        };

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
            glm::vec4 baseColorFactor = { 1.f, 0.f, 1.f, 1.f }; // Magenta.
            float metallicFactor = 1.f;
            float roughnessFactor = 1.f;
            float normalScale = 1.f;
            float occlusionStrength = 1.f;
            glm::vec3 emissiveFactor = { 0.f, 0.f, 0.f };
            float alphaCutOff;
        };

        std::unordered_map<const fastgltf::Primitive*, PrimitiveInfo> primitiveInfos = createPrimitiveInfos();

        /**
         * Buffer that contains <tt>GpuMaterial</tt>s, with fallback material at the index 0 (total <tt>asset.materials.size() + 1</tt>).
         */
        vku::AllocatedBuffer materialBuffer = createMaterialBuffer();

        /**
         * Mapping of used indices data by its type. Since all the used indices data are combined into a single buffer,
         * user can use <tt>PrimitiveInfo::indexInfo</tt> to get the indices.
         * @note If you passed <tt>vulkan::Gpu</tt> whose <tt>supportUint8Index</tt> is <tt>false</tt>, primitive with
         * unsigned byte (<tt>uint8_t</tt>) indices will be converted to unsigned short (<tt>uint16_t</tt>) indices.
         */
        std::unordered_map<vk::IndexType, vku::AllocatedBuffer> indexBuffers;

        AssetGpuBuffers(const fastgltf::Asset &asset, const AssetExternalBuffers &externalBuffers, const vulkan::Gpu &gpu, BS::thread_pool threadPool = {});

    private:
        std::vector<vku::AllocatedBuffer> attributeBuffers;
        std::unordered_map<IndexedAttribute, vku::AllocatedBuffer> indexedAttributeMappingBuffers;
        std::optional<vku::AllocatedBuffer> tangentBuffer;

        [[nodiscard]] auto createPrimitiveInfos() const -> std::unordered_map<const fastgltf::Primitive*, PrimitiveInfo>;
        [[nodiscard]] auto createMaterialBuffer() const -> vku::AllocatedBuffer;

        auto stageMaterials(vk::CommandBuffer copyCommandBuffer) -> void;
        auto stagePrimitiveAttributeBuffers(const AssetExternalBuffers &externalBuffers, vk::CommandBuffer copyCommandBuffer) -> void;
        auto stagePrimitiveIndexedAttributeMappingBuffers(IndexedAttribute attributeType, vk::CommandBuffer copyCommandBuffer) -> void;
        auto stagePrimitiveTangentBuffers(const AssetExternalBuffers &externalBuffers, vk::CommandBuffer copyCommandBuffer, BS::thread_pool &threadPool) -> void;
        auto stagePrimitiveIndexBuffers(const AssetExternalBuffers &externalBuffers, vk::CommandBuffer copyCommandBuffer) -> void;

        /**
         * From given segments (a range of byte data), create a combined staging buffer and return each segments' start offsets.
         *
         * Example: Two segments { 0xAA, 0xBB, 0xCC } and { 0xDD, 0xEE } will be combined to { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE },
         * and their start offsets are { 0, 3 }.
         * @param segments Data segments to be combined.
         * @return Pair of combined staging buffer and each segments' start offsets vector.
         */
        template <std::ranges::random_access_range R>
            requires std::ranges::contiguous_range<std::ranges::range_value_t<R>>
        [[nodiscard]] auto createCombinedStagingBuffer(
            R &&segments
        ) -> std::pair<const vku::AllocatedBuffer&, std::vector<vk::DeviceSize>> {
            if constexpr (std::convertible_to<std::ranges::range_value_t<R>, std::span<const std::byte>>) {
                assert(!segments.empty() && "Empty segments not allowed (Vulkan requires non-zero buffer size)");

                // Calculate each segments' size and their destination offsets.
                const auto segmentSizes = segments | std::views::transform([](const auto &bytes) { return bytes.size(); });
                std::vector<vk::DeviceSize> copyOffsets(segmentSizes.size());
                std::exclusive_scan(segmentSizes.begin(), segmentSizes.end(), copyOffsets.begin(), vk::DeviceSize { 0 });

                vku::MappedBuffer stagingBuffer { gpu.allocator, vk::BufferCreateInfo {
                    {},
                    copyOffsets.back() + segmentSizes.back(), // = sum(segmentSizes).
                    vk::BufferUsageFlagBits::eTransferSrc,
                } };
                for (auto [segment, copyOffset] : std::views::zip(segments, copyOffsets)){
                    std::ranges::copy(segment, static_cast<std::byte*>(stagingBuffer.data) + copyOffset);
                }

                return { stagingBuffers.emplace_front(std::move(stagingBuffer).unmap()), std::move(copyOffsets) };
            }
            else {
                // Retry with converting each segments into the std::span<const std::byte>.
                const auto byteSegments = segments | std::views::transform([](const auto &segment) { return as_bytes(std::span { segment }); });
                return createCombinedStagingBuffer(byteSegments);
            }
        }
    };
}