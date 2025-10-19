module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.ag.MousePicking;

#ifdef _MSC_VER
import std;
#endif
export import vku;

export import vk_gltf_viewer.vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::ag {
    export struct MousePicking {
        vku::raii::AllocatedImage depthImage;
        vk::raii::ImageView depthImageView;

        MousePicking(const Gpu &gpu LIFETIMEBOUND, const vk::Extent2D &extent);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::ag::MousePicking::MousePicking(const Gpu &gpu, const vk::Extent2D &extent)
    : depthImage {
        gpu.allocator,
        vk::ImageCreateInfo {
            {},
            vk::ImageType::e2D,
            vk::Format::eD32Sfloat,
            vk::Extent3D { extent, 1 },
            1, 1,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
        },
        vma::AllocationCreateInfo {
            {},
            vma::MemoryUsage::eAutoPreferDevice,
        },
    },
    depthImageView { gpu.device, depthImage.getViewCreateInfo(vk::ImageViewType::e2D) } { }