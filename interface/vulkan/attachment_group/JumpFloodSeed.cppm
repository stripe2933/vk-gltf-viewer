module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.ag.JumpFloodSeed;

export import vku;

export import vk_gltf_viewer.vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::ag {
    export struct JumpFloodSeed final : vku::AttachmentGroup {
        JumpFloodSeed(const Gpu &gpu LIFETIMEBOUND, const vku::Image &seedImage LIFETIMEBOUND);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::ag::JumpFloodSeed::JumpFloodSeed(const Gpu &gpu, const vku::Image &seedImage)
    : AttachmentGroup { vku::toExtent2D(seedImage.extent) } {
    addColorAttachment(
        gpu.device,
        seedImage,
        seedImage.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } /* ping image subresource */));
    setDepthStencilAttachment(
        gpu.device,
        storeImage(createDepthStencilImage(gpu.allocator, vk::Format::eD32Sfloat)));
}