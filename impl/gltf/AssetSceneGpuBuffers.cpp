module;

#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :gltf.AssetSceneGpuBuffers;

import std;
import :helpers.fastgltf;
import :helpers.ranges;

vku::AllocatedBuffer vk_gltf_viewer::gltf::AssetSceneGpuBuffers::createNodeBuffer(
    const fastgltf::Asset &asset,
    const vulkan::buffer::MeshNodeWorldTransforms &meshNodeWorldTransforms,
    const vulkan::buffer::MeshWeights &meshWeights,
    const vulkan::Gpu &gpu
) const {
    vku::AllocatedBuffer stagingBuffer = vku::MappedBuffer {
        gpu.allocator,
        std::from_range, ranges::views::upto(asset.nodes.size()) | std::views::transform([&](std::size_t nodeIndex) {
            return std::array {
                to_optional(asset.nodes[nodeIndex].meshIndex)
                    .transform([&](std::size_t meshIndex) {
                        return meshWeights.segments[meshIndex].startAddress;
                    })
                    .value_or(vk::DeviceAddress { 0 }),
                meshNodeWorldTransforms.getTransformStartAddress(nodeIndex),
            };
        }),
        gpu.isUmaDevice ? vk::BufferUsageFlagBits::eStorageBuffer : vk::BufferUsageFlagBits::eTransferSrc,
    }.unmap();

    if (gpu.isUmaDevice || vku::contains(gpu.allocator.getAllocationMemoryProperties(stagingBuffer.allocation), vk::MemoryPropertyFlagBits::eDeviceLocal)) {
        return stagingBuffer;
    }

    vku::AllocatedBuffer dstBuffer{ gpu.allocator, vk::BufferCreateInfo {
        {},
        stagingBuffer.size,
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    } };

    const vk::raii::CommandPool transferCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.transfer } };
    const vk::raii::Fence fence { gpu.device, vk::FenceCreateInfo{} };
    vku::executeSingleCommand(*gpu.device, *transferCommandPool, gpu.queues.transfer, [&](vk::CommandBuffer cb) {
        cb.copyBuffer(stagingBuffer, dstBuffer, vk::BufferCopy { 0, 0, dstBuffer.size });
    }, *fence);

    std::ignore = gpu.device.waitForFences(*fence, true, ~0ULL); // TODO: failure handling

    return dstBuffer;
}