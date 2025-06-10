module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.rp.BloomApply;

#ifdef _MSC_VER
import std;
#endif

export import vk_gltf_viewer.vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::rp {
    export struct BloomApply : vk::raii::RenderPass {
        explicit BloomApply(const Gpu &gpu LIFETIMEBOUND);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::rp::BloomApply::BloomApply(const Gpu &gpu)
    : RenderPass { gpu.device, vk::RenderPassCreateInfo {
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
                vk::AttachmentReference { 0, gpu.supportAttachmentFeedbackLoopLayout ? vk::ImageLayout::eAttachmentFeedbackLoopOptimalEXT : vk::ImageLayout::eGeneral },
                vk::AttachmentReference { 1, vk::ImageLayout::eShaderReadOnlyOptimal },
            }),
            vku::unsafeProxy(vk::AttachmentReference { 0, vk::ImageLayout::eGeneral }),
        }),
    } } { }