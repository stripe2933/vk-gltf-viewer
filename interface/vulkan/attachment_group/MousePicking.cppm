module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.ag.MousePicking;

export import vku;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::ag {
    export struct MousePicking final : vku::AttachmentGroup {
        MousePicking(
            const Gpu &gpu LIFETIMEBOUND,
            const vk::Extent2D &extent
        ) : AttachmentGroup { extent } {
            addColorAttachment(
                gpu.device,
                storeImage(createColorImage(gpu.allocator, vk::Format::eR16Uint,
                    vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment| vk::ImageUsageFlagBits::eTransientAttachment,
                    vku::allocation::deviceLocalTransient)));
            setDepthStencilAttachment(
                gpu.device,
                storeImage(createDepthStencilImage(gpu.allocator, vk::Format::eD32Sfloat)));
        }
    };
}