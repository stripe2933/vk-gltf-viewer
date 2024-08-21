module;

#include <fastgltf/types.hpp>

export module vk_gltf_viewer:gltf.AssetResources;

import std;
export import glm;
import thread_pool;
export import vku;
import :gltf.AssetExternalBuffers;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::gltf {
    export class AssetResources {
        std::forward_list<vku::AllocatedBuffer> stagingBuffers;

    public:
        struct Config {
            bool supportUint8Index;
        };

        enum class IndexedAttribute { Texcoord, Color };

        struct PrimitiveInfo {
            struct IndexBufferInfo { vk::DeviceSize offset; vk::IndexType type; };
            struct AttributeBufferInfo { vk::DeviceAddress address; std::uint8_t byteStride; };
            struct IndexedAttributeMappingInfo { vk::DeviceAddress pBufferPtrBuffer; vk::DeviceAddress pByteStridesBuffer; };

            std::optional<std::uint32_t> materialIndex;
            std::uint32_t drawCount;
            std::optional<IndexBufferInfo> indexInfo{};
            AttributeBufferInfo positionInfo;
            std::optional<AttributeBufferInfo> normalInfo, tangentInfo;
            std::unordered_map<std::size_t, AttributeBufferInfo> texcoordInfos, colorInfos;
            std::unordered_map<IndexedAttribute, IndexedAttributeMappingInfo> indexedAttributeMappingInfos;
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

        const fastgltf::Asset &asset;

        std::unordered_map<std::size_t, vku::AllocatedImage> images;
        std::unordered_map<std::size_t, vk::raii::Sampler> samplers;

        vku::AllocatedBuffer materialBuffer;

        std::unordered_map<const fastgltf::Primitive*, PrimitiveInfo> primitiveInfos;
        std::vector<vku::AllocatedBuffer> attributeBuffers;
        std::unordered_map<IndexedAttribute, std::pair<vku::AllocatedBuffer /* bufferPtrs */, vku::AllocatedBuffer /* byteStrides */>> indexedAttributeMappingBuffers;
        std::optional<vku::AllocatedBuffer> tangentBuffer;
        std::unordered_map<vk::IndexType, vku::AllocatedBuffer> indexBuffers;

        AssetResources(const fastgltf::Asset &asset [[clang::lifetimebound]], const std::filesystem::path &assetDir, const vulkan::Gpu &gpu [[clang::lifetimebound]], const Config &config = {});

    private:
        AssetResources(const fastgltf::Asset &asset, const std::filesystem::path &assetDir, const AssetExternalBuffers &externalBuffers, const vulkan::Gpu &gpu, const Config &config, BS::thread_pool threadPool = {});

        [[nodiscard]] auto createPrimitiveInfos(const fastgltf::Asset &asset) const -> decltype(primitiveInfos);
        [[nodiscard]] auto createImages(const std::filesystem::path &assetDir, const AssetExternalBuffers &externalBuffers, vma::Allocator allocator, BS::thread_pool &threadPool) const -> std::unordered_map<std::size_t, vku::AllocatedImage>;
        [[nodiscard]] auto createSamplers(const vk::raii::Device &device) const -> std::unordered_map<std::size_t, vk::raii::Sampler>;
        [[nodiscard]] auto createMaterialBuffer(vma::Allocator allocator) const -> vku::AllocatedBuffer;

        auto stageImages(const std::filesystem::path &assetDir, const AssetExternalBuffers &externalBuffers, vma::Allocator allocator, vk::CommandBuffer copyCommandBuffer, BS::thread_pool &threadPool) -> void;
        auto stageMaterials(vma::Allocator allocator, vk::CommandBuffer copyCommandBuffer) -> void;
        auto stagePrimitiveAttributeBuffers(const AssetExternalBuffers &externalBuffers, const vulkan::Gpu &gpu, vk::CommandBuffer copyCommandBuffer) -> void;
        auto stagePrimitiveIndexedAttributeMappingBuffers(IndexedAttribute attributeType, const vulkan::Gpu &gpu, vk::CommandBuffer copyCommandBuffer) -> void;
        auto stagePrimitiveTangentBuffers(const AssetExternalBuffers &externalBuffers, const vulkan::Gpu &gpu, vk::CommandBuffer copyCommandBuffer, BS::thread_pool &threadPool) -> void;
        auto stagePrimitiveIndexBuffers(const AssetExternalBuffers &externalBuffers, const vulkan::Gpu &gpu, vk::CommandBuffer copyCommandBuffer, bool supportUint8Index) -> void;

        auto releaseResourceQueueFamilyOwnership(const vulkan::QueueFamilies &queueFamilies, vk::CommandBuffer commandBuffer) const -> void;

        /**
         * From given segments (a range of byte datas), create a combined staging buffer and return each segments' start offsets.
         * @param allocator Allocator that used for buffer allocation.
         * @param segments Data segments to be combined.
         * @return pair, first=(combined staging buffer), second=(each segments' start offsets).
         * @example
         * Two segments { 0xAA, 0xBB, 0xCC } and { 0xDD, 0xEE } will combined to { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE }, and their
         * start offsets are { 0, 3 }.
         */
        template <std::ranges::random_access_range R>
            requires std::ranges::contiguous_range<std::ranges::range_value_t<R>>
        [[nodiscard]] auto createCombinedStagingBuffer(
            vma::Allocator allocator,
            R &&segments
        ) -> std::pair<const vku::AllocatedBuffer&, std::vector<vk::DeviceSize>>;
    };
}

// module :private;

template <std::ranges::random_access_range R>
    requires std::ranges::contiguous_range<std::ranges::range_value_t<R>>
[[nodiscard]] auto vk_gltf_viewer::gltf::AssetResources::createCombinedStagingBuffer(
    vma::Allocator allocator,
    R &&segments
) -> std::pair<const vku::AllocatedBuffer&, std::vector<vk::DeviceSize>> {
    using value_type = std::ranges::range_value_t<std::ranges::range_value_t<R>>;
    static_assert(std::is_standard_layout_v<value_type>, "Copying non-standard layout does not guarantee the intended result.");
    assert(!segments.empty() && "Empty segments not allowed (Vulkan requires non-zero buffer size)");

    // Calculate each segments' size and their destination offsets.
    const auto segmentsBytes = segments | std::views::transform([](const auto &segment) { return as_bytes(std::span { segment }); });
    const auto segmentsByteSizes = segmentsBytes | std::views::transform(&std::span<const std::byte>::size);
    std::vector<vk::DeviceSize> copyOffsets(segmentsByteSizes.size());
    std::exclusive_scan(segmentsByteSizes.begin(), segmentsByteSizes.end(), copyOffsets.begin(), vk::DeviceSize { 0 });

    vku::MappedBuffer stagingBuffer { allocator, vk::BufferCreateInfo {
        {},
        copyOffsets.back() + segmentsByteSizes.back(), // = sum(segmentSizes).
        vk::BufferUsageFlagBits::eTransferSrc,
    } };
    for (auto [segmentBytes, copyOffset] : std::views::zip(segmentsBytes, copyOffsets)){
        std::ranges::copy(segmentBytes, static_cast<std::byte*>(stagingBuffer.data) + copyOffset);
    }

    return { stagingBuffers.emplace_front(std::move(stagingBuffer).unmap()), std::move(copyOffsets) };
}