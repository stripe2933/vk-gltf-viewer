module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.ag.SceneOpaque;

import std;
export import vku;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::ag {
    export struct SceneOpaque final : vku::MultisampleAttachmentGroup {
        SceneOpaque(const Gpu &gpu LIFETIMEBOUND, const vk::Extent2D &extent)
            : MultisampleAttachmentGroup { extent, vk::SampleCountFlagBits::e4 } {
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
                storeImage(createDepthStencilImage(gpu.allocator, vk::Format::eD32Sfloat
#if __APPLE__
                    // MoltenVK bug. Described in https://github.com/stripe2933/vk-deferred/blob/75bf7536f4c9c6af76fe9875853f9e785ca1dfb2/interface/vulkan/attachment_group/GBuffer.cppm#L28.
                    , vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
                    vku::allocation::deviceLocal
#endif
                )));
        }
    };
}