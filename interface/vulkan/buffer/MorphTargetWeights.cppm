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
    export class MorphTargetWeights final : public vku::MappedBuffer {
    public:
        std::reference_wrapper<const gltf::ds::TargetWeightCountExclusiveScanWithCount> targetWeightCountExclusiveScanWithCount;

        MorphTargetWeights(
            const fastgltf::Asset &asset,
            const gltf::ds::TargetWeightCountExclusiveScanWithCount &targetWeightCountExclusiveScanWithCount LIFETIMEBOUND,
            const Gpu &gpu LIFETIMEBOUND
        );

        [[nodiscard]] std::span<float> weights(std::size_t nodeIndex);
        [[nodiscard]] std::span<const float> weights(std::size_t nodeIndex) const;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::buffer::MorphTargetWeights::MorphTargetWeights(
    const fastgltf::Asset &asset,
    const gltf::ds::TargetWeightCountExclusiveScanWithCount &targetWeightCountExclusiveScanWithCount,
    const Gpu &gpu
) : MappedBuffer { createCombinedBuffer(gpu.allocator, asset.nodes | std::views::transform([&](const fastgltf::Node &node) {
        return getTargetWeights(node, asset);
    }), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, vma::MemoryUsage::eAutoPreferDevice).first },
    targetWeightCountExclusiveScanWithCount { targetWeightCountExclusiveScanWithCount } { }

std::span<float> vk_gltf_viewer::vulkan::buffer::MorphTargetWeights::weights(std::size_t nodeIndex) {
    return asRange<float>().subspan(
        targetWeightCountExclusiveScanWithCount.get()[nodeIndex],
        targetWeightCountExclusiveScanWithCount.get()[nodeIndex + 1] - targetWeightCountExclusiveScanWithCount.get()[nodeIndex]);
}

std::span<const float> vk_gltf_viewer::vulkan::buffer::MorphTargetWeights::weights(std::size_t nodeIndex) const {
    return asRange<const float>().subspan(
        targetWeightCountExclusiveScanWithCount.get()[nodeIndex],
        targetWeightCountExclusiveScanWithCount.get()[nodeIndex + 1] - targetWeightCountExclusiveScanWithCount.get()[nodeIndex]);
}