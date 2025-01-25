export module vk_gltf_viewer:gltf.MeshWeights;

import std;
export import fastgltf;
import :helpers.ranges;
export import :vulkan.Gpu;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [&](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

template <typename T>
class writeonly {
    T volatile *addr;

public:
    explicit writeonly(std::uintptr_t addr)
        : addr { reinterpret_cast<T*>(addr) } { }

    void operator=(const T &t) volatile { *addr = t; }

    [[nodiscard]] std::uintptr_t address() const noexcept { return addr; }
};

/**
 * @brief Create a combined buffer from given segments (a range of byte data) and return each segments' start offsets.
 *
 * Example: Two segments { 0xAA, 0xBB, 0xCC } and { 0xDD, 0xEE } will be combined to { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE }, and their start offsets are { 0, 3 }.
 *
 * @tparam R Range type of data segments.
 * @param allocator VMA allocator to allocate the buffer.
 * @param segments Range of data segments. Each segment will be converted to <tt>std::span<const std::byte></tt>, therefore segment's elements must be trivially copyable.
 * @param usage Usage flags of the result buffer.
 * @return Pair of buffer and each segments' start offsets vector.
 */
template <std::ranges::forward_range R>
    requires (
        std::ranges::sized_range<std::ranges::range_value_t<R>>
        && std::same_as<std::ranges::range_value_t<std::ranges::range_value_t<R>>, std::byte>)
[[nodiscard]] std::pair<vku::MappedBuffer, std::vector<vk::DeviceSize>> createCombinedBuffer(
    vma::Allocator allocator,
    R &&segments,
    vk::BufferUsageFlags usage
) {
    // Calculate each segments' copy destination offsets.
    std::vector<vk::DeviceSize> copyOffsets { std::from_range, segments | std::views::transform(LIFT(std::size)) };
    vk::DeviceSize sizeTotal = copyOffsets.back();
    std::exclusive_scan(copyOffsets.begin(), copyOffsets.end(), copyOffsets.begin(), vk::DeviceSize { 0 });
    sizeTotal += copyOffsets.back();

    if (sizeTotal == 0) {
        throw std::invalid_argument { "No data to write" };
    }

    // Create buffer.
    vku::MappedBuffer buffer { allocator, vk::BufferCreateInfo { {}, sizeTotal, usage } };

    // Copy segments to the buffer.
    std::byte *mapped = static_cast<std::byte*>(buffer.data);
    for (auto &&segment : FWD(segments)) {
        mapped = std::ranges::copy(FWD(segment), mapped).out;
    }

    return { std::move(buffer), std::move(copyOffsets) };
}

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
            auto [buffer, copyOffsets] = createCombinedBuffer(
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
                    std::span { reinterpret_cast<writeonly<float>*>(segmentStart + sizeof(float)), mesh.weights.size() });
            }

            return std::move(buffer);
        }
    };
}