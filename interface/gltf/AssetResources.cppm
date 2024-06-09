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
export import :vulkan.Gpu;
export import :gltf.io.StbDecoder;

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

        mutable std::list<vku::MappedBuffer> stagingBuffers;

    public:
        struct PrimitiveData {
            struct IndexBufferInfo { vk::DeviceSize offset; vk::IndexType type; std::uint32_t drawCount; };
            struct AttributeBufferInfo { vk::DeviceAddress address; vk::DeviceSize byteStride; };

            IndexBufferInfo indexInfo;
            AttributeBufferInfo positionInfo;
            std::optional<AttributeBufferInfo> normalInfo, tangentInfo;
            std::unordered_map<std::size_t, AttributeBufferInfo> texcoordInfos, colorInfos;
        };

        struct GpuMaterial {
            vk::DeviceAddress pTangentBuffer                   = 0,
                              pBaseColorTexcoordBuffer         = 0,
                              pMetallicRoughnessTexcoordBuffer = 0,
                              pNormalTexcoordBuffer            = 0,
                              pOcclusionTexcoordBuffer         = 0;
            std::uint8_t      tangentByteStride                   = 4,
                              baseColorTexcoordByteStride         = 2,
                              metallicRoughnessTexcoordByteStride = 2,
                              normalTexcoordByteStride            = 2,
                              occlusionTexcoordByteStride         = 2;
            char              padding0[3];
            std::int16_t      baseColorTextureIndex         = -1,
                              metallicRoughnessTextureIndex = -1,
                              normalTextureIndex            = -1,
                              occlusionTextureIndex         = -1;
            char              padding1[8];
            glm::vec4         baseColorFactor = { 1.f, 0.f, 1.f, 1.f }; // Magenta.
            float             metallicFactor    = 1.f,
                              roughnessFactor   = 1.f,
                              normalScale       = 1.f,
                              occlusionStrength = 1.f;
            char              padding2[32];
        };

        vk::raii::Sampler defaultSampler;

        std::vector<vku::AllocatedImage> images;
        std::vector<vk::raii::ImageView> imageViews;
        std::vector<vk::raii::Sampler> samplers;
        std::vector<vk::DescriptorImageInfo> textures;
        vku::AllocatedBuffer materialBuffer;

        std::unordered_map<const fastgltf::Primitive*, PrimitiveData> primitiveData;
        std::unordered_map<vk::IndexType, vku::AllocatedBuffer> indexBuffers;
        std::vector<vku::AllocatedBuffer> attributeBuffers;

        AssetResources(const fastgltf::Asset &asset, const std::filesystem::path &assetDir, const vulkan::Gpu &gpu);

    private:
        AssetResources(const fastgltf::Asset &asset, const ResourceBytes &resourceBytes, const vulkan::Gpu &gpu);

        [[nodiscard]] auto createDefaultSampler(const vk::raii::Device &device) const -> decltype(defaultSampler);
        [[nodiscard]] auto createImages(const ResourceBytes &resourceBytes, vma::Allocator allocator) const -> decltype(images);
        [[nodiscard]] auto createImageViews(const vk::raii::Device &device) const -> decltype(imageViews);
        [[nodiscard]] auto createSamplers(const fastgltf::Asset &asset, const vk::raii::Device &device) const -> decltype(samplers);
        [[nodiscard]] auto createTextures(const fastgltf::Asset &asset) const -> decltype(textures);
        [[nodiscard]] auto createMaterialBuffer(const fastgltf::Asset &asset, vma::Allocator allocator) const -> decltype(materialBuffer);

        auto stageImages(const ResourceBytes &resourceBytes, vma::Allocator allocator, vk::CommandBuffer copyCommandBuffer) -> void;
        auto setPrimitiveIndexData(const fastgltf::Asset &asset, const ResourceBytes &resourceBytes, vma::Allocator allocator, vk::CommandBuffer copyCommandBuffer) -> void;
        auto setPrimitiveAttributeData(const fastgltf::Asset &asset, const ResourceBytes &resourceBytes, const vulkan::Gpu &gpu, vk::CommandBuffer copyCommandBuffer) -> void;
        auto stageMaterials(const fastgltf::Asset &asset, vma::Allocator allocator, vk::CommandBuffer copyCommandBuffer) -> void;

        auto releaseResourceQueueFamilyOwnership(const vulkan::Gpu::QueueFamilies &queueFamilies, vk::CommandBuffer commandBuffer) const -> void;

        [[nodiscard]] auto createCombinedStagingBuffer(vma::Allocator allocator, std::ranges::random_access_range auto &&segments) -> std::pair<const vku::MappedBuffer&, std::vector<vk::DeviceSize>>;
    };
}

// module :private;

static_assert(sizeof(vk_gltf_viewer::gltf::AssetResources::GpuMaterial) % 64 == 0 && "minStorageBufferOffsetAlignment = 64");

/**
 * From given segments (a range of byte datas), create a combined staging buffer and return each segments' start offsets.
 * @param allocator Allocator that used for buffer allocation.
 * @param segments Data segments to be combined.
 * @return pair, first=(combined staging buffer), second=(each segments' start offsets).
 * @example
 * Two segments { 0xAA, 0xBB, 0xCC } and { 0xDD, 0xEE } will combined to { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE }, and their
 * start offsets are { 0, 3 }.
 */
[[nodiscard]] auto vk_gltf_viewer::gltf::AssetResources::createCombinedStagingBuffer(
    vma::Allocator allocator,
    std::ranges::random_access_range auto &&segments
) -> std::pair<const vku::MappedBuffer&, std::vector<vk::DeviceSize>> {
    const auto segmentSizes = segments | std::views::transform([](const auto &segment) { return segment.size(); }) | std::views::common;
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
    std::for_each(std::execution::par_unseq, segmentAndCopyOffsets.begin(), segmentAndCopyOffsets.end(), [&](const auto &segmentAndCopyOffsets) {
        const auto &[segment, copyOffset] = segmentAndCopyOffsets;
		std::ranges::copy(segment, static_cast<std::uint8_t*>(stagingBuffer.data) + copyOffset);
	});
#else
    #pragma omp parallel for
    for (const auto &[segment, copyOffset] : std::views::zip(segments, copyOffsets)){
        std::ranges::copy(segment, static_cast<std::uint8_t*>(stagingBuffer.data) + copyOffset);
    }
#endif

    return { std::move(stagingBuffer), std::move(copyOffsets) };
}