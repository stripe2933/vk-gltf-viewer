export module vk_gltf_viewer:vulkan.ag.Scene;

export import vku;

namespace vk_gltf_viewer::vulkan::ag {
    export struct Scene final : vku::MsaaAttachmentGroup {
        Scene(
            const vk::raii::Device &device [[clang::lifetimebound]],
            const vku::Image &msaaImage [[clang::lifetimebound]],
            const vku::Image &swapchainImage,
            const vku::Image &depthImage [[clang::lifetimebound]]
        ) : MsaaAttachmentGroup { vku::toExtent2D(msaaImage.extent), vk::SampleCountFlagBits::e4 } {
            addColorAttachment(device, msaaImage, swapchainImage);
            setDepthStencilAttachment(device, depthImage);
        }
    };
}