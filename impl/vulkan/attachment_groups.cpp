module vk_gltf_viewer;
import :vulkan.attachment_groups;

vk_gltf_viewer::vulkan::DepthPrepassAttachmentGroup::DepthPrepassAttachmentGroup(
    const Gpu &gpu,
    const vk::Extent2D &extent
) : AttachmentGroup { extent } {
    addColorAttachment(
        gpu.device,
        storeImage(createColorImage(gpu.allocator, vk::Format::eR32Uint, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc)));
    setDepthStencilAttachment(
        gpu.device,
        storeImage(createDepthStencilImage(gpu.allocator, vk::Format::eD32Sfloat)));
}

vk_gltf_viewer::vulkan::SwapchainAttachmentGroup::SwapchainAttachmentGroup(
    const vk::raii::Device &device,
    vk::Image swapchainImage,
    const vk::Extent2D &extent
) : AttachmentGroup { extent } {
    addColorAttachment(
        device,
        { swapchainImage, vk::Extent3D { extent, 1 }, vk::Format::eB8G8R8A8Srgb, 1, 1 },
        vk::Format::eB8G8R8A8Srgb);
}

vk_gltf_viewer::vulkan::ImGuiSwapchainAttachmentGroup::ImGuiSwapchainAttachmentGroup(
    const vk::raii::Device &device,
    vk::Image swapchainImage,
    const vk::Extent2D &extent
) : AttachmentGroup { extent } {
    addColorAttachment(
        device,
        { swapchainImage, vk::Extent3D { extent, 1 }, vk::Format::eB8G8R8A8Unorm, 1, 1 },
        vk::Format::eB8G8R8A8Unorm);
}
