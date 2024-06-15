module;

#include <cstdint>
#include <compare>
#ifdef _MSC_VER
#include <execution>
#endif
#include <filesystem>
#include <list>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <unordered_map>
#include <vector>

#include <fastgltf/core.hpp>

export module vk_gltf_viewer:gltf.AssetResources;

export import vku;
import :io.StbDecoder;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::gltf {
    export class AssetResources {
        class ResourceBytes {
            // Load external buffer/image bytes.
            std::list<std::vector<std::uint8_t>> externalBufferBytes;

        public:
            std::vector<std::span<const std::uint8_t>> bufferBytes; // bufferBytes[i] -> asset.buffers[i].data
            std::vector<io::StbDecoder<std::uint8_t>::DecodeResult> images; // images[i] -> decoded image data.

            explicit ResourceBytes(const fastgltf::Asset &asset, const std::filesystem::path &assetDir);

            [[nodiscard]] auto getBufferViewBytes(const fastgltf::BufferView &bufferView) const noexcept -> std::span<const std::uint8_t>;

        private:
            auto createBufferBytes(const fastgltf::Asset &asset, const std::filesystem::path &assetDir) -> decltype(bufferBytes);
            auto createImages(const fastgltf::Asset &asset, const std::filesystem::path &assetDir) const -> decltype(images);
        };

        std::list<vku::MappedBuffer> stagingBuffers;

    public:
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
            std::uint8_t baseColorTexcoordIndex,
                         metallicRoughnessTexcoordIndex,
                         normalTexcoordIndex,
                         occlusionTexcoordIndex,
                         emissiveTexcoordIndex;
            char         padding0[1];
            std::int16_t baseColorTextureIndex         = -1,
                         metallicRoughnessTextureIndex = -1,
                         normalTextureIndex            = -1,
                         occlusionTextureIndex         = -1,
                         emissiveTextureIndex          = -1;
            glm::vec4    baseColorFactor = { 1.f, 0.f, 1.f, 1.f }; // Magenta.
            float        metallicFactor    = 1.f,
                         roughnessFactor   = 1.f,
                         normalScale       = 1.f,
                         occlusionStrength = 1.f;
            glm::vec3    emissiveFactor = { 0.f, 0.f, 0.f };
            char         padding1[4];
        };

        const fastgltf::Asset &asset;

        vk::raii::Sampler defaultSampler;

        std::vector<vku::AllocatedImage> images;
        std::vector<vk::raii::ImageView> imageViews;
        std::vector<vk::raii::Sampler> samplers;
        std::vector<vk::DescriptorImageInfo> textures = createTextures();
        std::optional<vku::AllocatedBuffer> materialBuffer;

        std::unordered_map<const fastgltf::Primitive*, PrimitiveInfo> primitiveInfos;
        std::vector<vku::AllocatedBuffer> attributeBuffers;
        std::unordered_map<IndexedAttribute, std::pair<vku::AllocatedBuffer /* bufferPtrs */, vku::AllocatedBuffer /* byteStrides */>> indexedAttributeMappingBuffers;
        std::optional<vku::AllocatedBuffer> tangentBuffer;
        std::unordered_map<vk::IndexType, vku::AllocatedBuffer> indexBuffers;

        AssetResources(const fastgltf::Asset &asset, const std::filesystem::path &assetDir, const vulkan::Gpu &gpu);

    private:
        AssetResources(const fastgltf::Asset &asset, const ResourceBytes &resourceBytes, const vulkan::Gpu &gpu);

        [[nodiscard]] auto createPrimitiveInfos(const fastgltf::Asset &asset) const -> decltype(primitiveInfos);

        [[nodiscard]] auto createDefaultSampler(const vk::raii::Device &device) const -> decltype(defaultSampler);
        [[nodiscard]] auto createImages(const ResourceBytes &resourceBytes, vma::Allocator allocator) const -> decltype(images);
        [[nodiscard]] auto createImageViews(const vk::raii::Device &device) const -> decltype(imageViews);
        [[nodiscard]] auto createSamplers(const vk::raii::Device &device) const -> decltype(samplers);
        [[nodiscard]] auto createTextures() const -> decltype(textures);
        [[nodiscard]] auto createMaterialBuffer(vma::Allocator allocator) const -> decltype(materialBuffer);

        auto stageImages(const ResourceBytes &resourceBytes, vma::Allocator allocator, vk::CommandBuffer copyCommandBuffer) -> void;
        auto stageMaterials(vma::Allocator allocator, vk::CommandBuffer copyCommandBuffer) -> void;
        auto stagePrimitiveAttributeBuffers(const ResourceBytes &resourceBytes, const vulkan::Gpu &gpu, vk::CommandBuffer copyCommandBuffer) -> void;
        auto stagePrimitiveIndexedAttributeMappingBuffers(IndexedAttribute attributeType, const vulkan::Gpu &gpu, vk::CommandBuffer copyCommandBuffer) -> void;
        auto stagePrimitiveTangentBuffers(const ResourceBytes &resourceBytes, const vulkan::Gpu &gpu, vk::CommandBuffer copyCommandBuffer) -> void;
        auto stagePrimitiveIndexBuffers(const ResourceBytes &resourceBytes, vma::Allocator allocator, vk::CommandBuffer copyCommandBuffer) -> void;

        auto releaseResourceQueueFamilyOwnership(const vulkan::Gpu::QueueFamilies &queueFamilies, vk::CommandBuffer commandBuffer) const -> void;

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
        [[nodiscard]] auto createCombinedStagingBuffer(vma::Allocator allocator, R &&segments) -> std::pair<const vku::MappedBuffer&, std::vector<vk::DeviceSize>>;
    };
}

// module :private;

template <std::ranges::random_access_range R>
[[nodiscard]] auto vk_gltf_viewer::gltf::AssetResources::createCombinedStagingBuffer(
    vma::Allocator allocator,
    R &&segments
) -> std::pair<const vku::MappedBuffer&, std::vector<vk::DeviceSize>> {
    using value_type = std::ranges::range_value_t<std::ranges::range_value_t<R>>;
    static_assert(std::is_standard_layout_v<value_type>, "Copying non-standard layout does not guarantee the intended result.");
    assert(!segments.empty() && "Empty segments not allowed (Vulkan requires non-zero buffer size)");

    const auto segmentSizes = segments | std::views::transform([](const auto &segment) { return sizeof(value_type) * segment.size(); }) | std::views::common;
    std::vector<vk::DeviceSize> copyOffsets(segmentSizes.size());
    std::exclusive_scan(segmentSizes.begin(), segmentSizes.end(), copyOffsets.begin(), vk::DeviceSize { 0 });

    const auto &stagingBuffer = stagingBuffers.emplace_back(
        allocator,
        vk::BufferCreateInfo {
            {},
            copyOffsets.back() + segmentSizes.back(), // = sum(segmentSizes).
            vk::BufferUsageFlagBits::eTransferSrc,
        },
        vma::AllocationCreateInfo {
            vma::AllocationCreateFlagBits::eHostAccessRandom | vma::AllocationCreateFlagBits::eMapped,
            vma::MemoryUsage::eAuto,
        });

#ifdef _MSC_VER
    const auto segmentAndCopyOffsets = std::views::zip(segments, copyOffsets) | std::views::common;
    std::for_each(std::execution::par_unseq, segmentAndCopyOffsets.begin(), segmentAndCopyOffsets.end(), [&](const auto &segmentAndCopyOffset) {
        const auto &[segment, copyOffset] = segmentAndCopyOffset;
		std::ranges::copy(segment, reinterpret_cast<value_type*>(static_cast<char*>(stagingBuffer.data) + copyOffset));
	});
#else
    #pragma omp parallel for
    for (const auto &[segment, copyOffset] : std::views::zip(segments, copyOffsets)){
        std::ranges::copy(segment, reinterpret_cast<value_type*>(static_cast<char*>(stagingBuffer.data) + copyOffset));
    }
#endif

    return { std::move(stagingBuffer), std::move(copyOffsets) };
}