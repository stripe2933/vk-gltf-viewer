module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.rp.BloomApply;

#ifdef _MSC_VER
import std;
#endif
export import vulkan_hpp;
import vku;

namespace vk_gltf_viewer::vulkan::rp {
    export struct BloomApply : vk::raii::RenderPass {
        explicit BloomApply(const vk::raii::Device &device LIFETIMEBOUND)
            : RenderPass { device, vk::RenderPassCreateInfo {
                {},
                vku::unsafeProxy({
                    // Result image.
                    vk::AttachmentDescription {
                        {},
                        vk::Format::eB8G8R8A8Srgb, vk::SampleCountFlagBits::e1,
                        vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore,
                        {}, {},
                        vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eColorAttachmentOptimal,
                    },
                    // Bloom image.
                    vk::AttachmentDescription {
                        {},
                        vk::Format::eR16G16B16A16Sfloat, vk::SampleCountFlagBits::e1,
                        vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eDontCare,
                        {}, {},
                        vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                    },
                }),
                vku::unsafeProxy(vk::SubpassDescription {
                    {},
                    vk::PipelineBindPoint::eGraphics,
                    vku::unsafeProxy({
                        // The result image will be both used for input attachment and color attachment, therefore its layout
                        // must be GENERAL.
                        // TODO: utilize VK_EXT_attachment_feedback_loop_layout and set the layout as ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT.
                        vk::AttachmentReference { 0, vk::ImageLayout::eGeneral },
                        vk::AttachmentReference { 1, vk::ImageLayout::eShaderReadOnlyOptimal },
                    }),
                    vku::unsafeProxy(vk::AttachmentReference { 0, vk::ImageLayout::eGeneral }),
                }),
            } } { }
    };
}