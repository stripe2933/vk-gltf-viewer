export module vk_gltf_viewer:vulkan.ag.SceneOpaque;

import std;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::ag {
    export struct SceneOpaque final : vku::MsaaAttachmentGroup {
        SceneOpaque(
            const Gpu &gpu [[clang::lifetimebound]],
            const vk::Extent2D &extent,
            std::span<const vk::Image> swapchainImages
        ) : MsaaAttachmentGroup { extent, vk::SampleCountFlagBits::e4 } {
            addSwapchainAttachment(
                gpu.device,
                storeImage(createColorImage(gpu.allocator, vk::Format::eB8G8R8A8Srgb)),
                swapchainImages,
                vk::Format::eB8G8R8A8Srgb);
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