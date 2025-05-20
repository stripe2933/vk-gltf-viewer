module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.ag.SceneOpaque;

import std;
export import vku;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::ag {
    export struct SceneOpaque final : vku::MultisampleAttachmentGroup {
        vku::AllocatedImage stencilResolveImage;
        vk::raii::ImageView stencilResolveImageView;

        SceneOpaque(const Gpu &gpu LIFETIMEBOUND, const vk::Extent2D &extent)
            : MultisampleAttachmentGroup { extent, vk::SampleCountFlagBits::e4 }
            , stencilResolveImage {
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
#ifndef __APPLE__
                vku::allocation::deviceLocalTransient,
#endif
            }
            , stencilResolveImageView { gpu.device, stencilResolveImage.getViewCreateInfo() }{
            addColorAttachment(
                gpu.device,
                storeImage(createColorImage(gpu.allocator, vk::Format::eB8G8R8A8Srgb)),
                storeImage(createResolveImage(
                    gpu.allocator,
                    vk::Format::eB8G8R8A8Srgb,
                    vk::ImageUsageFlagBits::eColorAttachment
                        | vk::ImageUsageFlagBits::eInputAttachment // input in InverseToneMappingRenderer, feedback loop in BloomApplyRenderer
                        | vk::ImageUsageFlagBits::eTransferSrc /* copied to the swapchain image */)));
            setDepthStencilAttachment(
                gpu.device,
                storeImage(createDepthStencilImage(gpu.allocator, vk::Format::eD32SfloatS8Uint
#if __APPLE__
                    // MoltenVK bug. Described in https://github.com/stripe2933/vk-deferred/blob/75bf7536f4c9c6af76fe9875853f9e785ca1dfb2/interface/vulkan/attachment_group/GBuffer.cppm#L28.
                    , vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
                    vku::allocation::deviceLocal
#endif
                )));
        }
    };
}