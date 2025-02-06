export module vk_gltf_viewer:vulkan.buffer.MorphTargetWeights;

import std;
export import fastgltf;
import :vulkan.buffer;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::buffer {
    export class MorphTargetWeights {
    public:
        MorphTargetWeights(const fastgltf::Asset &asset, const Gpu &gpu [[clang::lifetimebound]])
            : buffer { createBuffer(asset, gpu.allocator) }
            , bufferAddress { gpu.device.getBufferAddress({ buffer }) }{ }

        [[nodiscard]] vk::DeviceAddress getStartAddress(std::size_t nodeIndex) const noexcept {
            return bufferAddress + startOffsets[nodeIndex];
        }

        void updateWeight(std::size_t nodeIndex, std::size_t weightIndex, float weight) {
            buffer.asRange<float>(startOffsets[nodeIndex])[weightIndex] = weight;
        }

    private:
        std::vector<vk::DeviceSize> startOffsets;
        vku::MappedBuffer buffer;
        vk::DeviceAddress bufferAddress;

        [[nodiscard]] vku::MappedBuffer createBuffer(const fastgltf::Asset &asset, vma::Allocator allocator) {
            auto [buffer, copyOffsets] = createCombinedBuffer(
                allocator,
                asset.nodes | std::views::transform([&](const fastgltf::Node &node) {
                    std::span<const float> weights = node.weights;
                    if (node.meshIndex) {
                        const fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
                        weights = mesh.weights;
                    }
                    return weights;
                }),
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);
            startOffsets = std::move(copyOffsets);

            return std::move(buffer);
        }
    };
}