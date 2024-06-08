module;

#include <compare>
#include <filesystem>
#include <list>
#include <numeric>
#include <ranges>
#include <span>
#include <unordered_map>
#include <vector>

#include <fastgltf/core.hpp>

export module vk_gltf_viewer:gltf.AssetResources;

export import vku;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::gltf {
    export class AssetResources {
        class ResourceBytes {
            // Load external buffer/image bytes.
            std::list<std::vector<std::uint8_t>> externalBufferBytes;

        public:
            std::vector<std::span<const std::uint8_t>> bufferBytes; // bufferBytes[i] -> asset.buffers[i].data
            std::vector<std::span<const std::uint8_t>> imageBytes; // imageBytes[i] -> asset.images[i].data

            explicit ResourceBytes(const fastgltf::Asset &asset, const std::filesystem::path &assetDir);

            [[nodiscard]] auto getBufferViewBytes(const fastgltf::BufferView &bufferView) const noexcept -> std::span<const std::uint8_t>;
        };

        mutable std::list<vku::MappedBuffer> stagingBuffers;

    public:
        struct PrimitiveData {
            struct IndexBufferInfo { vk::DeviceSize offset; vk::IndexType type; std::uint32_t drawCount; };
            struct AttributeBufferInfo { vk::DeviceAddress address; vk::DeviceSize byteStride; };

            IndexBufferInfo indexInfo;
            AttributeBufferInfo positionInfo, normalInfo;
        };

        std::unordered_map<vk::IndexType, vku::AllocatedBuffer> indexBuffers;
        std::vector<vku::AllocatedImage> images;
        std::unordered_map<const fastgltf::Primitive*, PrimitiveData> primitiveData;

        AssetResources(const fastgltf::Asset &asset, const std::filesystem::path &assetDir, const vulkan::Gpu &gpu);

    private:
        std::vector<vku::AllocatedBuffer> attributeBuffers;

        [[nodiscard]] auto createCombinedStagingBuffer(vma::Allocator allocator, std::ranges::random_access_range auto &&segments) -> std::pair<const vku::MappedBuffer&, std::vector<vk::DeviceSize>>;

        auto setPrimitiveIndexData(const fastgltf::Asset &asset, const vulkan::Gpu &gpu, const ResourceBytes &resourceBytes, vk::CommandBuffer copyCommandBuffer) -> void;
        auto setPrimitiveAttributeData(const fastgltf::Asset &asset, const vulkan::Gpu &gpu, const ResourceBytes &resourceBytes, vk::CommandBuffer copyCommandBuffer) -> void;
    };
}

// module :private;

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

    for (const auto &[segment, copyOffset] : std::views::zip(segments, copyOffsets)){
        std::ranges::copy(segment, static_cast<std::uint8_t*>(stagingBuffer.data) + copyOffset);
    }

    return { std::move(stagingBuffer), std::move(copyOffsets) };
}