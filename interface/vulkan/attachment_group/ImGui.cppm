module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.ag.ImGui;

import std;

export import vk_gltf_viewer.vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::ag {
    export struct ImGui final : vku::AttachmentGroup {
        ImGui(
            const Gpu &gpu LIFETIMEBOUND,
            const vk::Extent2D &extent,
            std::span<const vk::Image> swapchainImages
        ) : AttachmentGroup { extent } {
            addSwapchainAttachment(gpu.device, swapchainImages, gpu.supportSwapchainMutableFormat ? vk::Format::eB8G8R8A8Unorm : vk::Format::eB8G8R8A8Srgb);
        }
    };
}