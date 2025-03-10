module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.buffer.MorphTargetWeights;

import std;
export import fastgltf;
import :vulkan.buffer;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::buffer {
    export class MorphTargetWeights {
    public:
        MorphTargetWeights(const fastgltf::Asset &asset, const Gpu &gpu LIFETIMEBOUND)
            : buffer { createBuffer(asset, gpu.allocator) }
            , descriptorInfo { buffer, 0, vk::WholeSize } { }

        [[nodiscard]] std::uint32_t getStartIndex(std::size_t nodeIndex) const noexcept {
            return startOffsets[nodeIndex];
        }

        [[nodiscard]] const vk::DescriptorBufferInfo &getDescriptorInfo() const noexcept {
            return descriptorInfo;
        }

        void updateWeight(std::size_t nodeIndex, std::size_t weightIndex, float weight) {
            buffer.asRange<float>()[startOffsets[nodeIndex] + weightIndex] = weight;
        }

    private:
        std::vector<std::uint32_t> startOffsets;
        vku::MappedBuffer buffer;
        vk::DescriptorBufferInfo descriptorInfo;

        [[nodiscard]] vku::MappedBuffer createBuffer(const fastgltf::Asset &asset, vma::Allocator allocator) {
            // This is workaround for Clang 19's bug that ranges::views::concat causes ambiguous spaceship operator error.
            // TODO: change it to use ranges::views::concat when available.
            std::vector<std::span<const float>> weights;
            weights.reserve(asset.nodes.size() + 1);

            weights.append_range(asset.nodes | std::views::transform([&](const fastgltf::Node &node) {
                std::span<const float> weights = node.weights;
                if (node.meshIndex) {
                    const fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
                    weights = mesh.weights;
                }
                return weights;
            }));
            // A dummy NaN-valued weight for preventing the zero-sized buffer creation.
            // This will not affect to the actual weight indexing.
            constexpr float dummyWeight = std::numeric_limits<float>::quiet_NaN();
            weights.emplace_back(&dummyWeight, 1);

            auto [buffer, copyOffsets] = createCombinedBuffer(allocator, weights, vk::BufferUsageFlagBits::eStorageBuffer);
            startOffsets = { std::from_range, copyOffsets };

            return std::move(buffer);
        }
    };
}