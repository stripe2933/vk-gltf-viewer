export module vk_gltf_viewer:vulkan.attachment_groups;

export import vku;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::inline attachment_groups {
    struct DepthPrepassAttachmentGroup final : vku::AttachmentGroup {
        DepthPrepassAttachmentGroup(const Gpu &gpu [[clang::lifetimebound]], const vk::Extent2D &extent);
    };

    struct PrimaryAttachmentGroup final : vku::MsaaAttachmentGroup {
        PrimaryAttachmentGroup(const Gpu &gpu [[clang::lifetimebound]], const vk::Extent2D &extent);
    };

    struct SwapchainAttachmentGroup final : vku::AttachmentGroup {
        SwapchainAttachmentGroup(const vk::raii::Device &device [[clang::lifetimebound]], vk::Image swapchainImage, const vk::Extent2D &extent);
    };

    struct ImGuiSwapchainAttachmentGroup final : vku::AttachmentGroup {
        ImGuiSwapchainAttachmentGroup(const vk::raii::Device &device [[clang::lifetimebound]], vk::Image swapchainImage, const vk::Extent2D &extent);
    };
}