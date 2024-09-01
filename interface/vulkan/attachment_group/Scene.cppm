export module vk_gltf_viewer:vulkan.ag.Scene;

import std;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::ag {
    export struct Scene final : vku::MsaaAttachmentGroup {
        Scene(
            const Gpu &gpu [[clang::lifetimebound]],
            const vk::Extent2D &extent,
            std::span<const vk::Image> swapchainImages
        ) : MsaaAttachmentGroup { extent, vk::SampleCountFlagBits::e4 } {
            addSwapchainAttachment(gpu.device,
                storeImage(createColorImage(gpu.allocator, vk::Format::eB8G8R8A8Srgb)),
                swapchainImages,
                vk::Format::eB8G8R8A8Srgb);
            setDepthStencilAttachment(gpu.device, storeImage(createDepthStencilImage(gpu.allocator, vk::Format::eD32Sfloat)));
        }
    };
}