module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.ag.JumpFloodSeed;

#ifdef _MSC_VER
import std;
#endif
export import vku;

export import vk_gltf_viewer.vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::ag {
    export struct JumpFloodSeed {
        vk::raii::ImageView colorImageView;
        vku::AllocatedImage depthImage;
        vk::raii::ImageView depthImageView;

        JumpFloodSeed(const Gpu &gpu LIFETIMEBOUND, const vku::Image &seedImage LIFETIMEBOUND);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::ag::JumpFloodSeed::JumpFloodSeed(const Gpu& gpu, const vku::Image& seedImage)
	: colorImageView { gpu.device, seedImage.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 4 /* TODO */ }, vk::ImageViewType::e2DArray)}
    , depthImage{
        gpu.allocator,
        vk::ImageCreateInfo {
            {},
            vk::ImageType::e2D,
            vk::Format::eD32Sfloat,
            seedImage.extent,
            1, 4,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
        },
        vma::AllocationCreateInfo {
            {},
            vma::MemoryUsage::eAutoPreferDevice,
            {},
            vk::MemoryPropertyFlagBits::eLazilyAllocated,
        },
    }
    , depthImageView{ gpu.device, depthImage.getViewCreateInfo(vk::ImageViewType::e2DArray) } { }