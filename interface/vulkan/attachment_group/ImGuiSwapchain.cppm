export module vk_gltf_viewer:vulkan.ag.ImGuiSwapchain;

export import vku;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::ag {
    export struct ImGuiSwapchain final : vku::AttachmentGroup {
        ImGuiSwapchain(
            const vk::raii::Device &device [[clang::lifetimebound]],
            const vku::Image &swapchainImage
        ) : AttachmentGroup { vku::toExtent2D(swapchainImage.extent) } {
            addColorAttachment(device, swapchainImage, vk::Format::eB8G8R8A8Unorm);
        }
    };
}