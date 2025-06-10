module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.buffer.MorphTargetWeights;

import std;
export import fastgltf;

export import vk_gltf_viewer.gltf.data_structure.TargetWeightCountExclusiveScanWithCount;
import vk_gltf_viewer.helpers.fastgltf;
import vk_gltf_viewer.vulkan.buffer;
export import vk_gltf_viewer.vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::buffer {
    export class MorphTargetWeights {
    public:
        MorphTargetWeights(
            const fastgltf::Asset &asset,
            const gltf::ds::TargetWeightCountExclusiveScanWithCount &targetWeightCountExclusiveScanWithCount LIFETIMEBOUND,
            const Gpu &gpu LIFETIMEBOUND
        );

        [[nodiscard]] const vk::DescriptorBufferInfo &getDescriptorInfo() const noexcept;

        void updateWeight(std::size_t nodeIndex, std::size_t weightIndex, float weight);

    private:
        vku::MappedBuffer buffer;
        vk::DescriptorBufferInfo descriptorInfo;
        std::reference_wrapper<const gltf::ds::TargetWeightCountExclusiveScanWithCount> targetWeightCountExclusiveScanWithCount;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::buffer::MorphTargetWeights::MorphTargetWeights(
    const fastgltf::Asset &asset,
    const gltf::ds::TargetWeightCountExclusiveScanWithCount &targetWeightCountExclusiveScanWithCount,
    const Gpu &gpu
) : buffer { createCombinedBuffer(gpu.allocator, asset.nodes | std::views::transform([&](const fastgltf::Node &node) {
        return getTargetWeights(node, asset);
    }), vk::BufferUsageFlagBits::eStorageBuffer).first },
    descriptorInfo { buffer, 0, vk::WholeSize },
    targetWeightCountExclusiveScanWithCount { targetWeightCountExclusiveScanWithCount } { }

const vk::DescriptorBufferInfo &vk_gltf_viewer::vulkan::buffer::MorphTargetWeights::getDescriptorInfo() const noexcept {
    return descriptorInfo;
}

void vk_gltf_viewer::vulkan::buffer::MorphTargetWeights::updateWeight(std::size_t nodeIndex, std::size_t weightIndex, float weight) {
    buffer.asRange<float>()[targetWeightCountExclusiveScanWithCount.get()[nodeIndex] + weightIndex] = weight;
}