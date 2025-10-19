module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.ag.JumpFloodSeed;

import std;
export import vku;

export import vk_gltf_viewer.vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::ag {
    export struct JumpFloodSeed {
        vk::raii::ImageView seedImageView;
        vku::raii::AllocatedImage depthImage;
        vk::raii::ImageView depthImageView;

        JumpFloodSeed(const Gpu &gpu LIFETIMEBOUND, const vku::Image &seedImage LIFETIMEBOUND, std::uint32_t viewCount);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::ag::JumpFloodSeed::JumpFloodSeed(const Gpu &gpu, const vku::Image &seedImage, std::uint32_t viewCount)
    : seedImageView { gpu.device, seedImage.getViewCreateInfo(vk::ImageViewType::e2DArray, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, viewCount } /* ping image subresource */) }
    , depthImage {
        gpu.allocator,
        vk::ImageCreateInfo {
            {},
            vk::ImageType::e2D,
            vk::Format::eD32Sfloat,
            seedImage.extent,
            1, viewCount,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment,
        },
        vma::AllocationCreateInfo {
            {},
            vma::MemoryUsage::eAutoPreferDevice,
        },
    }
    , depthImageView { gpu.device, depthImage.getViewCreateInfo(vk::ImageViewType::e2DArray) } {
}