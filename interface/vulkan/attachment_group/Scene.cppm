module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.ag.Scene;

import std;
export import vku;

export import vk_gltf_viewer.vulkan.render_pass.BloomApply;
export import vk_gltf_viewer.vulkan.render_pass.Scene;
export import vk_gltf_viewer.vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::ag {
    export struct Scene {
        vk::Extent2D extent;

        vku::AllocatedImage multisampleColorImage;
        vk::raii::ImageView multisampleColorImageView;
        vku::AllocatedImage colorImage;
        vk::raii::ImageView colorImageView;
        vku::AllocatedImage depthStencilImage;
        vk::raii::ImageView depthStencilImageView;
        vku::AllocatedImage stencilResolveImage;
        vk::raii::ImageView stencilResolveImageView;
        vku::AllocatedImage multisampleAccumulationImage;
        vk::raii::ImageView multisampleAccumulationImageView;
        vku::AllocatedImage accumulationImage;
        vk::raii::ImageView accumulationImageView;
        vku::AllocatedImage multisampleRevealageImage;
        vk::raii::ImageView multisampleRevealageImageView;
        vku::AllocatedImage revealageImage;
        vk::raii::ImageView revealageImageView;

        vk::raii::Framebuffer sceneFramebuffer;
        vk::raii::Framebuffer bloomApplyFramebuffer;

        Scene(
            const Gpu &gpu LIFETIMEBOUND, 
            const vk::Extent2D &extent, 
            const rp::Scene &sceneRenderPass LIFETIMEBOUND,
            const rp::BloomApply &bloomApplyRenderPass LIFETIMEBOUND);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::ag::Scene::Scene(
    const Gpu &gpu, 
    const vk::Extent2D &extent, 
    const rp::Scene &sceneRenderPass, 
    const rp::BloomApply& bloomApplyRenderPass
) : extent { extent },
    multisampleColorImage {
        gpu.allocator,
        vk::ImageCreateInfo {
            {},
            vk::ImageType::e2D,
            vk::Format::eB8G8R8A8Srgb,
            vk::Extent3D { extent, 1 },
            1, 1,
            vk::SampleCountFlagBits::e4,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
        },
        vma::AllocationCreateInfo {
            {},
            vma::MemoryUsage::eAutoPreferDevice,
            {},
            vk::MemoryPropertyFlagBits::eLazilyAllocated,
        },
    },
    multisampleColorImageView { gpu.device, multisampleColorImage.getViewCreateInfo() },
    colorImage {
        gpu.allocator,
        vk::ImageCreateInfo {
            {},
            vk::ImageType::e2D,
            vk::Format::eB8G8R8A8Srgb,
            vk::Extent3D { extent, 1 },
            1, 1,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eColorAttachment
                | vk::ImageUsageFlagBits::eInputAttachment // input in InverseToneMappingRenderer
                | (gpu.supportAttachmentFeedbackLoopLayout ? vk::ImageUsageFlagBits::eAttachmentFeedbackLoopEXT : vk::ImageUsageFlagBits{}) // BloomApplyRenderer
                | vk::ImageUsageFlagBits::eTransferSrc /* copied to the swapchain image */,
        },
        vma::AllocationCreateInfo {
            {},
            vma::MemoryUsage::eAutoPreferDevice,
        },
    },
    colorImageView { gpu.device, colorImage.getViewCreateInfo() },
    depthStencilImage {
        gpu.allocator,
        vk::ImageCreateInfo {
            {},
            vk::ImageType::e2D,
            vk::Format::eD32SfloatS8Uint,
            vk::Extent3D { extent, 1 },
            1, 1,
            vk::SampleCountFlagBits::e4,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
        },
        vma::AllocationCreateInfo {
            {},
            vma::MemoryUsage::eAutoPreferDevice,
        // As MoltenVK does not support sceneFramebuffer fetch, it breaks the render pass by multiple MTLRenderCommandEncoders
        // for each subpass. Therefore, MTLStoreAction=Store must be used and therefore cannot be memoryless.
        #if !__APPLE__
            {},
            vk::MemoryPropertyFlagBits::eLazilyAllocated,
        #endif
        },
    },
    depthStencilImageView { gpu.device, depthStencilImage.getViewCreateInfo() },
    stencilResolveImage {
        gpu.allocator,
        vk::ImageCreateInfo {
            {},
            vk::ImageType::e2D,
            // If GPU supports VK_FORMAT_S8_UINT and no implementation-specific errors for it, it can be
            // used for reduce the resource usage (5 -> 1 bit per texel) as only stencil component resolve
            // is needed.
            gpu.supportS8UintDepthStencilAttachment && !gpu.workaround.depthStencilResolveDifferentFormat
                ? vk::Format::eS8Uint : vk::Format::eD32SfloatS8Uint,
            vk::Extent3D { extent, 1 },
            1, 1,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
        },
        vma::AllocationCreateInfo {
            {},
            vma::MemoryUsage::eAutoPreferDevice,
        // As MoltenVK does not support sceneFramebuffer fetch, it breaks the render pass by multiple MTLRenderCommandEncoders
        // for each subpass. Therefore, MTLStoreAction=Store must be used and therefore cannot be memoryless.
        #if !__APPLE__
            {},
            vk::MemoryPropertyFlagBits::eLazilyAllocated,
        #endif
        },
    },
    stencilResolveImageView { gpu.device, stencilResolveImage.getViewCreateInfo() },
    multisampleAccumulationImage {
        gpu.allocator,
        vk::ImageCreateInfo {
            {},
            vk::ImageType::e2D,
            vk::Format::eR16G16B16A16Sfloat,
            vk::Extent3D { extent, 1 },
            1, 1,
            vk::SampleCountFlagBits::e4,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
        },
        vma::AllocationCreateInfo {
            {},
            vma::MemoryUsage::eAutoPreferDevice,
            {},
            vk::MemoryPropertyFlagBits::eLazilyAllocated,
        },
    },
    multisampleAccumulationImageView { gpu.device, multisampleAccumulationImage.getViewCreateInfo() },
    accumulationImage {
        gpu.allocator,
        vk::ImageCreateInfo {
            {},
            vk::ImageType::e2D,
            vk::Format::eR16G16B16A16Sfloat,
            vk::Extent3D { extent, 1 },
            1, 1,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
        },
        vma::AllocationCreateInfo {
            {},
            vma::MemoryUsage::eAutoPreferDevice,
        // As MoltenVK does not support sceneFramebuffer fetch, it breaks the render pass by multiple MTLRenderCommandEncoders
        // for each subpass. Therefore, MTLStoreAction=Store must be used and therefore cannot be memoryless.
        #if !__APPLE__
            {},
            vk::MemoryPropertyFlagBits::eLazilyAllocated,
        #endif
        },
    },
    accumulationImageView { gpu.device, accumulationImage.getViewCreateInfo() },
    multisampleRevealageImage {
        gpu.allocator,
        vk::ImageCreateInfo {
            {},
            vk::ImageType::e2D,
            vk::Format::eR16Unorm,
            vk::Extent3D { extent, 1 },
            1, 1,
            vk::SampleCountFlagBits::e4,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
        },
        vma::AllocationCreateInfo {
            {},
            vma::MemoryUsage::eAutoPreferDevice,
            {},
            vk::MemoryPropertyFlagBits::eLazilyAllocated,
        },
    },
    multisampleRevealageImageView { gpu.device, multisampleRevealageImage.getViewCreateInfo() },
    revealageImage {
        gpu.allocator,
        vk::ImageCreateInfo {
            {},
            vk::ImageType::e2D,
            vk::Format::eR16Unorm,
            vk::Extent3D { extent, 1 },
            1, 1,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
        },
        vma::AllocationCreateInfo {
            {},
            vma::MemoryUsage::eAutoPreferDevice,
        // As MoltenVK does not support sceneFramebuffer fetch, it breaks the render pass by multiple MTLRenderCommandEncoders
        // for each subpass. Therefore, MTLStoreAction=Store must be used and therefore cannot be memoryless.
        #if !__APPLE__
            {},
            vk::MemoryPropertyFlagBits::eLazilyAllocated,
        #endif
        },
    },
    revealageImageView { gpu.device, revealageImage.getViewCreateInfo() },
    sceneFramebuffer { gpu.device, vk::FramebufferCreateInfo {
        {},
        *sceneRenderPass,
        vku::unsafeProxy({
            *multisampleColorImageView,
            *colorImageView,
            *depthStencilImageView,
            *stencilResolveImageView,
            *multisampleAccumulationImageView,
            *accumulationImageView,
            *multisampleRevealageImageView,
            *revealageImageView,
        }),
        extent.width, extent.height, 1,
    } },
    bloomApplyFramebuffer { gpu.device, vk::FramebufferCreateInfo {
        {},
        *bloomApplyRenderPass,
        *colorImageView,
        extent.width, extent.height, 1,
    } } { }