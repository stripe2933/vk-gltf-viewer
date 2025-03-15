module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.buffer.MorphTargetWeights;

import std;
export import fastgltf;
export import :gltf.data_structure.TargetWeightCountExclusiveScan;
import :helpers.fastgltf;
import :vulkan.buffer;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::buffer {
    export class MorphTargetWeights {
    public:
        MorphTargetWeights(
            const fastgltf::Asset &asset,
            std::shared_ptr<const gltf::ds::TargetWeightCountExclusiveScan> targetWeightCountExclusiveScan,
            const Gpu &gpu LIFETIMEBOUND
        ) : buffer { [&]() {
                // This is workaround for Clang 19's bug that ranges::views::concat causes ambiguous spaceship operator error.
                // TODO: change it to use ranges::views::concat when available.
                std::vector<std::span<const float>> weights;
                weights.reserve(asset.nodes.size() + 1);

                weights.append_range(asset.nodes | std::views::transform([&](const fastgltf::Node &node) {
                    return getTargetWeights(node, asset);
                }));
                // A dummy NaN-valued weight for preventing the zero-sized buffer creation.
                // This will not affect to the actual weight indexing.
                constexpr float dummyWeight = std::numeric_limits<float>::quiet_NaN();
                weights.emplace_back(&dummyWeight, 1);

                return createCombinedBuffer(gpu.allocator, weights, vk::BufferUsageFlagBits::eStorageBuffer).first;
            }() },
            descriptorInfo { buffer, 0, vk::WholeSize },
            targetWeightCountExclusiveScan { std::move(targetWeightCountExclusiveScan) } { }

        [[nodiscard]] const vk::DescriptorBufferInfo &getDescriptorInfo() const noexcept {
            return descriptorInfo;
        }

        void updateWeight(std::size_t nodeIndex, std::size_t weightIndex, float weight) {
            buffer.asRange<float>()[(*targetWeightCountExclusiveScan)[nodeIndex] + weightIndex] = weight;
        }

    private:
        vku::MappedBuffer buffer;
        vk::DescriptorBufferInfo descriptorInfo;
        std::shared_ptr<const gltf::ds::TargetWeightCountExclusiveScan> targetWeightCountExclusiveScan;
    };
}