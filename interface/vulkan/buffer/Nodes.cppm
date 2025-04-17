module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.buffer.Nodes;

import std;
export import fastgltf;
import :helpers.fastgltf;
import :helpers.ranges;
export import :gltf.data_structure.NodeInstanceCountExclusiveScanWithCount;
export import :gltf.data_structure.TargetWeightCountExclusiveScanWithCount;
import :gltf.data_structure.SkinJointCountExclusiveScan;
export import :vulkan.buffer.StagingBufferStorage;
import :vulkan.shader_type.Node;
import :vulkan.trait.PostTransferObject;

namespace vk_gltf_viewer::vulkan::buffer {
    export class Nodes : trait::PostTransferObject {
    public:
        Nodes(
            const fastgltf::Asset &asset,
            const gltf::ds::NodeInstanceCountExclusiveScanWithCount &nodeInstanceCountExclusiveScan,
            const gltf::ds::TargetWeightCountExclusiveScanWithCount &targetWeightCountExclusiveScan,
            vma::Allocator allocator,
            StagingBufferStorage &stagingBufferStorage
        ) : PostTransferObject { stagingBufferStorage },
            buffer { [&]() {
                const gltf::ds::SkinJointCountExclusiveScan skinJointCountExclusiveScan { asset };
                vku::AllocatedBuffer result = vku::MappedBuffer {
                    allocator,
                    std::from_range, ranges::views::upto(asset.nodes.size()) | std::views::transform([&](std::size_t nodeIndex) {
                        const fastgltf::Node &node = asset.nodes[nodeIndex];
                        return shader_type::Node {
                            .instancedTransformStartIndex = nodeInstanceCountExclusiveScan[nodeIndex],
                            .morphTargetWeightStartIndex = targetWeightCountExclusiveScan[nodeIndex],
                            .skinJointIndexStartIndex = to_optional(node.skinIndex).transform([&](std::size_t skinIndex) {
                                return skinJointCountExclusiveScan[skinIndex];
                            }).value_or(std::numeric_limits<std::uint32_t>::max()),
                        };
                    }),
                    vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
                }.unmap();
                if (StagingBufferStorage::needStaging(result)) {
                    stagingBufferStorage.stage(result, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer);
                }

                return result;
            }() },
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
    };
}