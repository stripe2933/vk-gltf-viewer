module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.render_pass.BloomApply;

import std;

export import vk_gltf_viewer.vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::rp {
    export struct BloomApply : vk::raii::RenderPass {
        BloomApply(const Gpu &gpu LIFETIMEBOUND, std::uint32_t viewMask);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::rp::BloomApply::BloomApply(const Gpu &gpu, std::uint32_t viewMask)
    : RenderPass { gpu.device, vk::StructureChain {
        vk::RenderPassCreateInfo {
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
        },
        vk::RenderPassMultiviewCreateInfo {
            viewMask,
            {},
            vku::unsafeProxy(0U),
        },
    }.get() } { }