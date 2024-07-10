export module vku:images.Image;

import std;
export import vulkan_hpp;

namespace vku {
    export struct Image {
        vk::Image image;
        vk::Extent3D extent;
        vk::Format format;
        std::uint32_t mipLevels;
        std::uint32_t arrayLayers;

        [[nodiscard]] operator vk::Image() const noexcept {
            return image;
        }

        [[nodiscard]] auto maxMipLevels() const noexcept -> vk::Extent2D {
            return maxMipLevels({ extent.width, extent.height });
        }

        [[nodiscard]] auto mipExtent(
            std::uint32_t mipLevel
        ) const noexcept -> vk::Extent2D {
            return mipExtent({ extent.width, extent.height }, mipLevel);
        }

        [[nodiscard]] static constexpr auto maxMipLevels(
            std::uint32_t size
        ) noexcept -> std::uint32_t {
            return std::bit_width(size);
        }

        [[nodiscard]] static constexpr auto maxMipLevels(
            const vk::Extent2D &extent
        ) noexcept -> std::uint32_t {
            return maxMipLevels(std::min(extent.width, extent.height));
        }

        [[nodiscard]] static constexpr auto mipExtent(
            const vk::Extent2D &extent,
            std::uint32_t mipLevel
        ) noexcept -> vk::Extent2D {
            return { extent.width >> mipLevel, extent.height >> mipLevel };
        }
    };
}