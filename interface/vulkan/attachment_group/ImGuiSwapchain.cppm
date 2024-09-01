export module vk_gltf_viewer:vulkan.ag.ImGuiSwapchain;

import std;
export import vku;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::ag {
    export struct ImGuiSwapchain final : vku::AttachmentGroup {
        ImGuiSwapchain(
            const vk::raii::Device &device [[clang::lifetimebound]],
            const vk::Extent2D &extent,
            std::span<const vk::Image> swapchainImages
        ) : AttachmentGroup { extent } {
            addSwapchainAttachment(device, swapchainImages, vk::Format::eB8G8R8A8Unorm);
        }
    };
}