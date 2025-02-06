export module vk_gltf_viewer:vulkan.buffer.MorphTargetWeights;

import std;
export import fastgltf;
import :vulkan.buffer;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::buffer {
    export class MorphTargetWeights {
    public:
        MorphTargetWeights(const fastgltf::Asset &asset, const Gpu &gpu [[clang::lifetimebound]])
            : buffer { createBuffer(asset, gpu) } { }

        [[nodiscard]] vk::DeviceAddress getStartAddress(std::size_t nodeIndex) const noexcept {
            return startAddresses[nodeIndex];
        }

    private:
        std::vector<vk::DeviceAddress> startAddresses;
        vku::MappedBuffer buffer;

        [[nodiscard]] vku::MappedBuffer createBuffer(const fastgltf::Asset &asset, const Gpu &gpu) {
            auto [buffer, copyOffsets] = createCombinedBuffer(
                gpu.allocator,
                asset.nodes | std::views::transform([&](const fastgltf::Node &node) {
                    std::span<const float> weights = node.weights;
                    if (node.meshIndex) {
                        const fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
                        weights = mesh.weights;
                    }
                    return weights;
                }),
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);

            const vk::DeviceAddress bufferAddress = gpu.device.getBufferAddress({ buffer });
            startAddresses = { std::from_range, copyOffsets | std::views::transform([&](vk::DeviceSize copyOffset) {
                return bufferAddress + copyOffset;
            }) };

            return std::move(buffer);
        }
    };
}