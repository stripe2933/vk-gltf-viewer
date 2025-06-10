module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer.vulkan.buffer;

import std;
export import vku;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

namespace vk_gltf_viewer::vulkan::buffer {
    /**
     * @brief Create a combined buffer from given segments (a range of byte data) and return each segments' start offsets.
     *
     * Example: Two segments { 0xAA, 0xBB, 0xCC } and { 0xDD, 0xEE } will be combined to { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE }, and their start offsets are { 0, 3 }.
     *
     * @tparam Unmap If <tt>true</tt>, the buffer will be unmapped after data write.
     * @tparam R Range type of data segments.
     * @param allocator VMA allocator to allocate the buffer.
     * @param segments Range of data segments. Each segment will be converted to <tt>std::span<const std::byte></tt> if their elements are not <tt>std::byte</tt>.
     * @param usage Usage flags of the result buffer.
     * @return Pair of buffer and each segments' start offsets vector.
     * @throw vk::InitializationFailedError Result buffer size is zero.
     */
    export template <bool Unmap = false, std::ranges::forward_range R> requires (
        // Each segments must be a sized range.
        std::ranges::sized_range<std::ranges::range_value_t<R>>
        // Each segments must be either
        // - a range of std::byte, or
        && (std::same_as<std::ranges::range_value_t<std::ranges::range_value_t<R>>, std::byte>
            // - a contiguous range of trivially copyable types.
            || (std::ranges::contiguous_range<std::ranges::range_value_t<R>>
                && std::is_trivially_copyable_v<std::ranges::range_value_t<std::ranges::range_value_t<R>>>)))
    [[nodiscard]] auto createCombinedBuffer(
        vma::Allocator allocator,
        R &&segments,
        vk::BufferUsageFlags usage
    ) {
        if constexpr (std::same_as<std::ranges::range_value_t<std::ranges::range_value_t<R>>, std::byte>) {
            // Calculate each segments' copy destination offsets.
            std::vector<vk::DeviceSize> copyOffsets { std::from_range, segments | std::views::transform(LIFT(std::size)) };
            vk::DeviceSize sizeTotal = copyOffsets.back();
            std::exclusive_scan(copyOffsets.begin(), copyOffsets.end(), copyOffsets.begin(), vk::DeviceSize { 0 });
            sizeTotal += copyOffsets.back();

            // Create buffer.
            vku::MappedBuffer buffer { allocator, vk::BufferCreateInfo { {}, sizeTotal, usage } };

            // Copy segments to the buffer.
            std::byte *mapped = static_cast<std::byte*>(buffer.data);
            for (auto &&segment : FWD(segments)) {
                mapped = std::ranges::copy(FWD(segment), mapped).out;
            }

            if constexpr (Unmap) {
                return std::pair { std::move(buffer).unmap(), std::move(copyOffsets) };
            }
            else {
                return std::pair { std::move(buffer), std::move(copyOffsets) };
            }
        }
        else {
            // Retry with converting each segments into the std::span<const std::byte>.
            auto byteSegments = segments | std::views::transform([](const auto &segment) { return as_bytes(std::span { segment }); });
            return createCombinedBuffer<Unmap>(allocator, byteSegments, usage);
        }
    }
}