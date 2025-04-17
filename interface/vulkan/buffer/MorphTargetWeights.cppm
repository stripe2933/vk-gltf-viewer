module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.buffer.MorphTargetWeights;

import std;
export import fastgltf;
export import :gltf.data_structure.TargetWeightCountExclusiveScanWithCount;
import :helpers.fastgltf;
import :vulkan.buffer;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::buffer {
    export class MorphTargetWeights {
    public:
        MorphTargetWeights(
            const fastgltf::Asset &asset,
            const gltf::ds::TargetWeightCountExclusiveScanWithCount &targetWeightCountExclusiveScanWithCount LIFETIMEBOUND,
            const Gpu &gpu LIFETIMEBOUND
        ) : buffer { createCombinedBuffer(gpu.allocator, asset.nodes | std::views::transform([&](const fastgltf::Node &node) {
                return getTargetWeights(node, asset);
            }), vk::BufferUsageFlagBits::eStorageBuffer).first },
            descriptorInfo { buffer, 0, vk::WholeSize },
            targetWeightCountExclusiveScanWithCount { targetWeightCountExclusiveScanWithCount } { }

        [[nodiscard]] const vk::DescriptorBufferInfo &getDescriptorInfo() const noexcept {
            return descriptorInfo;
        }

        void updateWeight(std::size_t nodeIndex, std::size_t weightIndex, float weight) {
            buffer.asRange<float>()[targetWeightCountExclusiveScanWithCount.get()[nodeIndex] + weightIndex] = weight;
        }

    private:
        vku::MappedBuffer buffer;
        vk::DescriptorBufferInfo descriptorInfo;
        std::reference_wrapper<const gltf::ds::TargetWeightCountExclusiveScanWithCount> targetWeightCountExclusiveScanWithCount;
    };
}