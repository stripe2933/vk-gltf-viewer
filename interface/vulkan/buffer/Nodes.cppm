module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.buffer.Nodes;

import std;
export import fastgltf;
import :helpers.fastgltf;
import :helpers.ranges;
export import :vulkan.buffer.InstancedNodeWorldTransforms;
export import :vulkan.buffer.MorphTargetWeights;
export import :vulkan.buffer.StagingBufferStorage;
import :vulkan.shader_type.Node;
import :vulkan.trait.PostTransferObject;

namespace vk_gltf_viewer::vulkan::buffer {
    export class Nodes : trait::PostTransferObject {
    public:
        Nodes(
            const fastgltf::Asset &asset,
            const InstancedNodeWorldTransforms &instancedNodeWorldTransformBuffer,
            const MorphTargetWeights &morphTargetWeights,
            vma::Allocator allocator,
            StagingBufferStorage &stagingBufferStorage
        ) : PostTransferObject { stagingBufferStorage },
            buffer { createBuffer(asset, instancedNodeWorldTransformBuffer, morphTargetWeights, allocator) },
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
            const MorphTargetWeights &morphTargetWeights,
            vma::Allocator allocator
        ) const {
            vku::AllocatedBuffer buffer = vku::MappedBuffer {
                allocator,
                std::from_range, ranges::views::upto(asset.nodes.size()) | std::views::transform([&](std::size_t nodeIndex) {
                    return shader_type::Node {
                        .instancedTransformStartIndex = instancedNodeWorldTransformBuffer.getStartIndex(nodeIndex),
                        .morphTargetWeightStartIndex = morphTargetWeights.getStartIndex(nodeIndex),
                    };
                }),
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
            }.unmap();
            if (StagingBufferStorage::needStaging(buffer)) {
                stagingBufferStorage.get().stage(buffer, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer);
            }

            return buffer;
        }
    };
}