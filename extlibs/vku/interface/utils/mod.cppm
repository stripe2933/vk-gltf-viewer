module;

#include <compare>
#include <concepts>

export module vku:utils;

import vulkan_hpp;
export import :utils.RefHolder;

export namespace vku {
    template <std::unsigned_integral T>
    [[nodiscard]] constexpr auto divCeil(T num, T denom) noexcept -> T {
        return (num / denom) + (num % denom != 0);
    }

    [[nodiscard]] constexpr auto fullSubresourceRange(
        vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor
    ) noexcept -> vk::ImageSubresourceRange {
        return { aspectFlags, 0, vk::RemainingMipLevels, 0, vk::RemainingArrayLayers };
    }

    template <typename T>
    [[nodiscard]] constexpr auto contains(vk::Flags<T> flags, T flag) noexcept -> bool {
        return (flags & flag) == flag;
    }

    template <typename T>
    [[nodiscard]] constexpr auto contains(vk::Flags<T> flags, vk::Flags<T> flag) noexcept -> bool {
        return (flags & flag) == flag;
    }

    [[nodiscard]] constexpr auto toExtent2D(const vk::Extent3D &extent) noexcept -> vk::Extent2D {
        return { extent.width, extent.height };
    }

    [[nodiscard]] constexpr auto aspect(const vk::Extent2D &extent) noexcept -> float {
        return static_cast<float>(extent.width) / extent.height;
    }
}