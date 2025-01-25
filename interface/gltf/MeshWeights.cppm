export module vk_gltf_viewer:gltf.MeshWeights;

import std;
export import fastgltf;
import :helpers.ranges;
import :helpers.writeonly;
import :vulkan.buffer;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::gltf {
    class MeshWeights {
    public:
        struct Segment {
            vk::DeviceAddress startAddress;
            writeonly<std::uint32_t> count;
            std::span<writeonly<float>> weights;
        };

        std::vector<Segment> segments;

        MeshWeights(const fastgltf::Asset &asset, const vulkan::Gpu &gpu [[clang::lifetimebound]])
            : buffer { createBuffer(asset, gpu) } { }

    private:
        vku::MappedBuffer buffer;

        [[nodiscard]] vku::MappedBuffer createBuffer(const fastgltf::Asset &asset, const vulkan::Gpu &gpu) {
            auto [buffer, copyOffsets] = vulkan::buffer::createCombinedBuffer(
                gpu.allocator,
                asset.meshes | std::views::transform([](const fastgltf::Mesh &mesh) {
                    // [count, weight0, weight1, ..., weight(count-1)]
                    return ranges::views::concat(
                        std::bit_cast<std::array<std::byte, 4>>(static_cast<std::uint32_t>(mesh.weights.size())),
                        as_bytes(std::span { mesh.weights }));
                }),
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);

            segments.reserve(copyOffsets.size());
            const vk::DeviceAddress bufferAddress = gpu.device.getBufferAddress({ buffer });
            for (const auto &[mesh, copyOffset] : std::views::zip(asset.meshes, copyOffsets)) {
                const std::uintptr_t segmentStart = reinterpret_cast<std::uintptr_t>(buffer.data) + copyOffset;
                segments.emplace_back(
                    bufferAddress + copyOffset,
                    writeonly<std::uint32_t> { segmentStart },
                    std::span { reinterpret_cast<writeonly<float>*>(segmentStart + sizeof(std::uint32_t)), mesh.weights.size() });
            }

            return std::move(buffer);
        }
    };
}