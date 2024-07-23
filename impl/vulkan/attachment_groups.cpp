module vk_gltf_viewer;
import :vulkan.attachment_groups;

vk_gltf_viewer::vulkan::DepthPrepassAttachmentGroup::DepthPrepassAttachmentGroup(
    const Gpu &gpu,
    const vku::Image &hoveringNodeJumpFloodImage,
    const vku::Image &selectedNodeJumpFloodImage,
    const vk::Extent2D &extent
) : AttachmentGroup { extent } {
    addColorAttachment(
        gpu.device,
        storeImage(createColorImage(gpu.allocator, vk::Format::eR32Uint, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc)));
    addColorAttachment(
        gpu.device,
        hoveringNodeJumpFloodImage,
        hoveringNodeJumpFloodImage.getViewCreateInfo(
            { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } /* ping image subresource */));
    addColorAttachment(
        gpu.device,
        selectedNodeJumpFloodImage,
        selectedNodeJumpFloodImage.getViewCreateInfo(
            { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } /* ping image subresource */));
    setDepthStencilAttachment(
        gpu.device,
        storeImage(createDepthStencilImage(gpu.allocator, vk::Format::eD32Sfloat)));
}

vk_gltf_viewer::vulkan::PrimaryAttachmentGroup::PrimaryAttachmentGroup(
    const Gpu &gpu,
    const vk::Extent2D &extent
) : MsaaAttachmentGroup { extent, vk::SampleCountFlagBits::e4 } {
    addColorAttachment(
        gpu.device,
        storeImage(createColorImage(gpu.allocator, vk::Format::eR16G16B16A16Sfloat)),
        storeImage(createResolveImage(gpu.allocator, vk::Format::eR16G16B16A16Sfloat, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eStorage)));
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
