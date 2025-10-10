module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.render_pass.BloomApply;

#ifdef _MSC_VER
import std;
#endif
import vku;
export import vulkan_hpp;

export import vk_gltf_viewer.vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::rp {
    export struct BloomApply final : vk::raii::RenderPass {
        explicit BloomApply(const Gpu &gpu LIFETIMEBOUND);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::rp::BloomApply::BloomApply(const Gpu &gpu)
    : RenderPass { gpu.device, vk::RenderPassCreateInfo {
        {},
        vku::lvalue({
            // Result image.
            vk::AttachmentDescription {
                {},
                vk::Format::eB8G8R8A8Srgb, vk::SampleCountFlagBits::e1,
                vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore,
                {}, {},
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eColorAttachmentOptimal,
            },
        }),
        vku::lvalue(vk::SubpassDescription {
            {},
            vk::PipelineBindPoint::eGraphics,
            vku::lvalue(vk::AttachmentReference { 0, gpu.supportAttachmentFeedbackLoopLayout ? vk::ImageLayout::eAttachmentFeedbackLoopOptimalEXT : vk::ImageLayout::eGeneral }),
            vku::lvalue(vk::AttachmentReference { 0, vk::ImageLayout::eGeneral }),
        }),
    } } { }