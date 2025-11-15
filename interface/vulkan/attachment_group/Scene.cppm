module;

#include <boost/container/static_vector.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.ag.Scene;

import std;
export import vku;

import vk_gltf_viewer.helpers.optional;
export import vk_gltf_viewer.vulkan.render_pass.BloomApply;
export import vk_gltf_viewer.vulkan.render_pass.Scene;
export import vk_gltf_viewer.vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::ag {
    export struct Scene {
        struct MultisampleResources {
            vku::raii::AllocatedImage colorImage;
            vk::raii::ImageView colorImageView;
            vku::raii::AllocatedImage stencilResolveImage;
            vk::raii::ImageView stencilResolveImageView;
            vku::raii::AllocatedImage accumulationImage;
            vk::raii::ImageView accumulationImageView;
            vku::raii::AllocatedImage revealageImage;
            vk::raii::ImageView revealageImageView;

            MultisampleResources(const Gpu &gpu LIFETIMEBOUND, const vk::Extent2D &extent, vk::SampleCountFlagBits sampleCount);
        };

        vku::raii::AllocatedImage colorImage;
        vk::raii::ImageView colorImageView;
        vku::raii::AllocatedImage depthStencilImage;
        vk::raii::ImageView depthStencilImageView;
        vku::raii::AllocatedImage accumulationImage;
        vk::raii::ImageView accumulationImageView;
        vku::raii::AllocatedImage revealageImage;
        vk::raii::ImageView revealageImageView;

        /// Will have value only if the passed <tt>sceneRenderPass</tt>'s sampleCount is not <tt>vk::SampleCountFlagBits::e1</tt>.
        std::optional<MultisampleResources> multisample;

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

vk_gltf_viewer::vulkan::ag::Scene::MultisampleResources::MultisampleResources(
    const Gpu &gpu,
    const vk::Extent2D &extent,
    vk::SampleCountFlagBits sampleCount
) : colorImage {
        gpu.allocator,
        vk::ImageCreateInfo {
            {},
            vk::ImageType::e2D,
            vk::Format::eB8G8R8A8Srgb,
            vk::Extent3D { extent, 1 },
            1, 1,
            sampleCount,
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
    colorImageView { gpu.device, colorImage.getViewCreateInfo(vk::ImageViewType::e2D) },
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
    stencilResolveImageView { gpu.device, stencilResolveImage.getViewCreateInfo(vk::ImageViewType::e2D) },
    accumulationImage {
        gpu.allocator,
        vk::ImageCreateInfo {
            {},
            vk::ImageType::e2D,
            vk::Format::eR16G16B16A16Sfloat,
            vk::Extent3D { extent, 1 },
            1, 1,
            sampleCount,
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
    accumulationImageView { gpu.device, accumulationImage.getViewCreateInfo(vk::ImageViewType::e2D) },
    revealageImage {
        gpu.allocator,
        vk::ImageCreateInfo {
            {},
            vk::ImageType::e2D,
            vk::Format::eR16Unorm,
            vk::Extent3D { extent, 1 },
            1, 1,
            sampleCount,
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
    revealageImageView { gpu.device, revealageImage.getViewCreateInfo(vk::ImageViewType::e2D) } { }

vk_gltf_viewer::vulkan::ag::Scene::Scene(
    const Gpu &gpu, 
    const vk::Extent2D &extent, 
    const rp::Scene &sceneRenderPass, 
    const rp::BloomApply& bloomApplyRenderPass
) : colorImage {
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
    colorImageView { gpu.device, colorImage.getViewCreateInfo(vk::ImageViewType::e2D) },
    depthStencilImage {
        gpu.allocator,
        vk::ImageCreateInfo {
            {},
            vk::ImageType::e2D,
            vk::Format::eD32SfloatS8Uint,
            vk::Extent3D { extent, 1 },
            1, 1,
            sceneRenderPass.sampleCount,
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
    depthStencilImageView { gpu.device, depthStencilImage.getViewCreateInfo(vk::ImageViewType::e2D) },
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
    accumulationImageView { gpu.device, accumulationImage.getViewCreateInfo(vk::ImageViewType::e2D) },
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
    revealageImageView { gpu.device, revealageImage.getViewCreateInfo(vk::ImageViewType::e2D) },
    multisample { value_if(sceneRenderPass.sampleCount != vk::SampleCountFlagBits::e1, [&] {
        return MultisampleResources { gpu, extent, sceneRenderPass.sampleCount };
    }) },
    sceneFramebuffer { gpu.device, vk::FramebufferCreateInfo {
        {},
        *sceneRenderPass,
        vku::lvalue([&] -> boost::container::static_vector<vk::ImageView, 8> {
            if (multisample) {
                return {
                    *multisample->colorImageView,
                    *colorImageView,
                    *depthStencilImageView,
                    *multisample->stencilResolveImageView,
                    *multisample->accumulationImageView,
                    *accumulationImageView,
                    *multisample->revealageImageView,
                    *revealageImageView,
                };
            }
            else {
                return {
                    *colorImageView,
                    *depthStencilImageView,
                    *accumulationImageView,
                    *revealageImageView,
                };
            }
        }()),
        extent.width, extent.height, 1,
    } },
    bloomApplyFramebuffer { gpu.device, vk::FramebufferCreateInfo {
        {},
        *bloomApplyRenderPass,
        *colorImageView,
        extent.width, extent.height, 1,
    } } { }