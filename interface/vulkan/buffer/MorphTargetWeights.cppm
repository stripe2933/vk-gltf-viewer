export module vk_gltf_viewer:vulkan.buffer.MorphTargetWeights;

import std;
export import fastgltf;
import :helpers.ranges;
import :vulkan.buffer;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::buffer {
    export class MorphTargetWeights {
    public:
        MorphTargetWeights(const fastgltf::Asset &asset, const Gpu &gpu [[clang::lifetimebound]])
            : buffer { createBuffer(asset, gpu.allocator) }
            , descriptorInfo { buffer, 0, vk::WholeSize } { }

        [[nodiscard]] std::uint32_t getStartIndex(std::size_t nodeIndex) const noexcept {
            return startOffsets[nodeIndex];
        }

        [[nodiscard]] const vk::DescriptorBufferInfo &getDescriptorInfo() const noexcept {
            return descriptorInfo;
        }

        void updateWeight(std::size_t nodeIndex, std::size_t weightIndex, float weight) {
            buffer.asRange<float>(startOffsets[nodeIndex])[weightIndex] = weight;
        }

    private:
        std::vector<std::uint32_t> startOffsets;
        vku::MappedBuffer buffer;
        vk::DescriptorBufferInfo descriptorInfo;

        [[nodiscard]] vku::MappedBuffer createBuffer(const fastgltf::Asset &asset, vma::Allocator allocator) {
            auto [buffer, copyOffsets] = createCombinedBuffer(
                allocator,
                ranges::views::concat(
                    asset.nodes | std::views::transform([&](const fastgltf::Node &node) {
                        std::span<const float> weights = node.weights;
                        if (node.meshIndex) {
                            const fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
                            weights = mesh.weights;
                        }
                        return weights;
                    }),
                    // A dummy NaN-valued weight for preventing the zero-sized buffer creation.
                    // This will not affect to the actual weight indexing.
                    std::views::single(std::array { std::numeric_limits<float>::quiet_NaN() })),
                vk::BufferUsageFlagBits::eStorageBuffer);
            startOffsets = { std::from_range, copyOffsets };

            return std::move(buffer);
        }
    };
}