module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.buffer.Nodes;

import std;
export import fastgltf;
import :helpers.ranges;
export import :vulkan.buffer.InstancedNodeWorldTransforms;

namespace vk_gltf_viewer::vulkan::buffer {
    export class Nodes {
    public:
        Nodes(
            const fastgltf::Asset &asset,
            const InstancedNodeWorldTransforms &instancedNodeWorldTransformBuffer,
            const Gpu &gpu [[clang::lifetimebound]]
        ) : buffer { createBuffer(asset, instancedNodeWorldTransformBuffer, gpu) },
            descriptorInfo { buffer, 0, vk::WholeSize } { }

        [[nodiscard]] const vk::DescriptorBufferInfo &getDescriptorInfo() const noexcept {
            return descriptorInfo;
        }

    private:
        /**
         * @brief Buffer with the start index of the instanced node world transform buffer.
         */
        vku::AllocatedBuffer buffer;
        vk::DescriptorBufferInfo descriptorInfo;

        [[nodiscard]] vku::AllocatedBuffer createBuffer(
            const fastgltf::Asset &asset,
            const InstancedNodeWorldTransforms &instancedNodeWorldTransformBuffer,
            const Gpu &gpu
        ) const {
            vku::AllocatedBuffer buffer = vku::MappedBuffer {
                gpu.allocator,
                std::from_range, ranges::views::upto(asset.nodes.size()) | std::views::transform([&](std::size_t nodeIndex) {
                    return instancedNodeWorldTransformBuffer.getStartIndex(nodeIndex);
                }),
                gpu.isUmaDevice ? vk::BufferUsageFlagBits::eStorageBuffer : vk::BufferUsageFlagBits::eTransferSrc,
            }.unmap();

            if (gpu.isUmaDevice || vku::contains(gpu.allocator.getAllocationMemoryProperties(buffer.allocation), vk::MemoryPropertyFlagBits::eDeviceLocal)) {
                return buffer;
            }

            vku::AllocatedBuffer dstBuffer{ gpu.allocator, vk::BufferCreateInfo {
                {},
                buffer.size,
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
            } };

            const vk::raii::CommandPool transferCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.transfer } };
            const vk::raii::Fence fence { gpu.device, vk::FenceCreateInfo{} };
            vku::executeSingleCommand(*gpu.device, *transferCommandPool, gpu.queues.transfer, [&](vk::CommandBuffer cb) {
                cb.copyBuffer(buffer, dstBuffer, vk::BufferCopy { 0, 0, dstBuffer.size });
            }, *fence);

            std::ignore = gpu.device.waitForFences(*fence, true, ~0ULL); // TODO: failure handling

            return dstBuffer;
        }
    };
}