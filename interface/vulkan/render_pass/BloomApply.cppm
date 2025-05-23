module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.rp.BloomApply;

#ifdef _MSC_VER
import std;
#endif
export import :vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::rp {
    export struct BloomApply : vk::raii::RenderPass {
        explicit BloomApply(const Gpu &gpu LIFETIMEBOUND)
            : RenderPass { gpu.device, vk::RenderPassCreateInfo {
                {},
                vku::unsafeProxy({
                    // Result image.
                    vk::AttachmentDescription {
                        {},
                        vk::Format::eB8G8R8A8Srgb, vk::SampleCountFlagBits::e1,
                        vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore,
                        {}, {},
                        gpu.workaround.generalOr(vk::ImageLayout::eColorAttachmentOptimal), gpu.workaround.generalOr(vk::ImageLayout::eColorAttachmentOptimal),
                    },
                    // Bloom image.
                    vk::AttachmentDescription {
                        {},
                        vk::Format::eR16G16B16A16Sfloat, vk::SampleCountFlagBits::e1,
                        vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eDontCare,
                        {}, {},
                        gpu.workaround.generalOr(vk::ImageLayout::eShaderReadOnlyOptimal), gpu.workaround.generalOr(vk::ImageLayout::eShaderReadOnlyOptimal),
                    },
                }),
                vku::unsafeProxy(vk::SubpassDescription {
                    {},
                    vk::PipelineBindPoint::eGraphics,
                    vku::unsafeProxy({
                        vk::AttachmentReference { 0, gpu.supportAttachmentFeedbackLoopLayout ? vk::ImageLayout::eAttachmentFeedbackLoopOptimalEXT : vk::ImageLayout::eGeneral },
                        vk::AttachmentReference { 1, gpu.workaround.generalOr(vk::ImageLayout::eShaderReadOnlyOptimal) },
                    }),
                    vku::unsafeProxy(vk::AttachmentReference { 0, vk::ImageLayout::eGeneral }),
                }),
            } } { }
    };
}