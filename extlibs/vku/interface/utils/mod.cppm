module;

#include <concepts>

export module vku:utils;

import vulkan_hpp;
export import :utils.RefHolder;

namespace vku {
    export template <std::unsigned_integral T>
    [[nodiscard]] constexpr auto divCeil(T num, T denom) noexcept -> T {
        return (num / denom) + (num % denom != 0);
    }

    export
    [[nodiscard]] constexpr auto fullSubresourceRange(
        vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor
    ) noexcept -> vk::ImageSubresourceRange {
        return { aspectFlags, 0, vk::RemainingMipLevels, 0, vk::RemainingArrayLayers };
    }
}

namespace vk {
    export
    [[nodiscard]] constexpr auto toFlags(auto flagBit) noexcept -> Flags<decltype(flagBit)> {
        return flagBit;
    }

    export template <typename T>
    [[nodiscard]] constexpr auto contains(Flags<T> flags, T flag) noexcept -> bool {
        return (flags & flag) == flag;
    }

    export template <typename T>
    [[nodiscard]] constexpr auto contains(Flags<T> flags, Flags<T> flag) noexcept -> bool {
        return (flags & flag) == flag;
    }

    export
    [[nodiscard]] constexpr auto toExtent2D(const Extent3D &extent) noexcept -> Extent2D {
        return { extent.width, extent.height };
    }

    export
    [[nodiscard]] constexpr auto aspect(const Extent2D &extent) noexcept -> float {
        return static_cast<float>(extent.width) / extent.height;
    }
}