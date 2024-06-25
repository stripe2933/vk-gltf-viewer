module;

#include <compare>
#include <concepts>
#include <initializer_list>
#include <ranges>

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
    [[nodiscard]] auto unsafeAddress(const T &value [[clang::lifetimebound]]) -> const T* {
        return &value;
    }

    template <typename T>
    [[nodiscard]] auto unsafeProxy(const T &elem [[clang::lifetimebound]]) -> vk::ArrayProxyNoTemporaries<const T> {
        return elem;
    }

    template <typename T>
    [[nodiscard]] auto unsafeProxy(const std::initializer_list<T> &arr [[clang::lifetimebound]]) -> vk::ArrayProxyNoTemporaries<const T> {
        return arr;
    }

    template <std::ranges::contiguous_range R>
    [[nodiscard]] auto unsafeProxy(const R &arr [[clang::lifetimebound]]) -> vk::ArrayProxyNoTemporaries<const std::ranges::range_value_t<R>> {
        return arr;
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