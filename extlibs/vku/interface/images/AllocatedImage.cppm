export module vku:images.AllocatedImage;

import std;
export import vk_mem_alloc_hpp;
export import vulkan_hpp;
export import :images.Image;

namespace vku {
    export struct AllocatedImage : Image {
        vma::Allocator allocator;
        vma::Allocation allocation;

        AllocatedImage(
            vma::Allocator allocator,
            const vk::ImageCreateInfo &createInfo,
            const vma::AllocationCreateInfo &allocationCreateInfo = { {}, vma::MemoryUsage::eAutoPreferDevice }
        );
        AllocatedImage(const AllocatedImage&) = delete;
        AllocatedImage(AllocatedImage &&src) noexcept;
        auto operator=(const AllocatedImage&) -> AllocatedImage& = delete;
        auto operator=(AllocatedImage &&src) noexcept -> AllocatedImage&;
        ~AllocatedImage();
    };
}

// module:private;

vku::AllocatedImage::AllocatedImage(
    vma::Allocator _allocator,
    const vk::ImageCreateInfo &createInfo,
    const vma::AllocationCreateInfo &allocationCreateInfo
) : Image { nullptr, createInfo.extent, createInfo.format, createInfo.mipLevels, createInfo.arrayLayers },
   allocator { _allocator } {
    std::tie(image, allocation) = allocator.createImage(createInfo, allocationCreateInfo);
}

vku::AllocatedImage::AllocatedImage(
    AllocatedImage &&src
) noexcept : Image { static_cast<Image>(src) },
             allocator { src.allocator },
             allocation { std::exchange(src.allocation, nullptr) } { }

auto vku::AllocatedImage::operator=(
    AllocatedImage &&src
) noexcept -> AllocatedImage & {
    if (allocation) {
        allocator.destroyImage(image, allocation);
    }

    static_cast<Image &>(*this) = static_cast<Image>(src);
    allocator = src.allocator;
    allocation = std::exchange(src.allocation, nullptr);
    return *this;
}

vku::AllocatedImage::~AllocatedImage() {
    if (allocation) {
        allocator.destroyImage(image, allocation);
    }
}