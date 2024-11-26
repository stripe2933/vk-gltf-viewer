module;

#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :gltf.AssetSceneGpuBuffers;

import std;
import :gltf.algorithm.traversal;
import :helpers.fastgltf;
import :helpers.ranges;

#define FWD(...) static_cast<decltype(__VA_ARGS__) &&>(__VA_ARGS__)
#define LIFT(...) [&](auto &&...xs) { return (__VA_ARGS__)(FWD(xs)...); }

const fastgltf::math::fmat4x4 &vk_gltf_viewer::gltf::AssetSceneGpuBuffers::getMeshNodeWorldTransform(std::uint16_t nodeIndex, std::uint32_t instanceIndex) const noexcept {
    return meshNodeWorldTransformBuffer.asRange<const fastgltf::math::fmat4x4>()[instanceOffsets[nodeIndex] + instanceIndex];
}

std::vector<std::uint32_t> vk_gltf_viewer::gltf::AssetSceneGpuBuffers::createInstanceCounts(const fastgltf::Scene &scene) const {
    std::vector<std::uint32_t> result(pAsset->nodes.size(), 0U);
    algorithm::traverseScene(*pAsset, scene, [&](std::size_t nodeIndex) {
        result[nodeIndex] = [&]() -> std::uint32_t {
            const fastgltf::Node &node = pAsset->nodes[nodeIndex];
            if (!node.meshIndex) {
                return 0;
            }
            if (node.instancingAttributes.empty()) {
                return 1;
            }
            else {
                // According to the EXT_mesh_gpu_instancing specification, all attribute accessors in a given node
                // must have the same count. Therefore, we can use the count of the first attribute accessor.
                return pAsset->accessors[node.instancingAttributes[0].accessorIndex].count;
            }
        }();
    });
    return result;
}

std::vector<std::uint32_t> vk_gltf_viewer::gltf::AssetSceneGpuBuffers::createInstanceOffsets() const {
    std::vector<std::uint32_t> result(instanceCounts.size());
    std::exclusive_scan(instanceCounts.cbegin(), instanceCounts.cend(), result.begin(), 0U);
    return result;
}

vku::AllocatedBuffer vk_gltf_viewer::gltf::AssetSceneGpuBuffers::createNodeBuffer(const vulkan::Gpu &gpu) const {
    const vk::DeviceAddress nodeTransformBufferStartAddress = gpu.device.getBufferAddress({ meshNodeWorldTransformBuffer });

    vku::AllocatedBuffer stagingBuffer = vku::MappedBuffer {
        gpu.allocator,
        std::from_range, instanceOffsets | std::views::transform([=](std::uint32_t offset) {
            return nodeTransformBufferStartAddress + sizeof(fastgltf::math::fmat4x4) * offset;
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

    if (gpu.device.waitForFences(*fence, true, ~0ULL) != vk::Result::eSuccess) {
        throw std::runtime_error { "Failed to transfer the node buffer" };
    }

    return dstBuffer;
}