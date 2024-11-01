export module vk_gltf_viewer:vulkan.ag.DepthPrepass;

export import vku;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::ag {
    export struct DepthPrepass final : vku::AttachmentGroup {
        DepthPrepass(
            const Gpu &gpu [[clang::lifetimebound]],
            const vk::Extent2D &extent
        ) : AttachmentGroup { extent } {
            addColorAttachment(
                gpu.device,
                storeImage(createColorImage(gpu.allocator, vk::Format::eR16Uint, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc)));
            setDepthStencilAttachment(
                gpu.device,
                storeImage(createDepthStencilImage(gpu.allocator, vk::Format::eD32Sfloat)));
        }
    };
}