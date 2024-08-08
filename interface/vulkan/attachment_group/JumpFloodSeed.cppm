export module vk_gltf_viewer:vulkan.ag.JumpFloodSeed;

export import vku;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::ag {
    export struct JumpFloodSeed final : vku::AttachmentGroup {
        JumpFloodSeed(
            const Gpu &gpu [[clang::lifetimebound]],
            const vku::Image &seedImage [[clang::lifetimebound]]
        ) : AttachmentGroup { vku::toExtent2D(seedImage.extent) } {
            addColorAttachment(
                gpu.device,
                seedImage,
                seedImage.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } /* ping image subresource */));
            setDepthStencilAttachment(
                gpu.device,
                storeImage(createDepthStencilImage(gpu.allocator, vk::Format::eD32Sfloat)));
        }
    };
}