module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.rp.CubemapToneMapping;

#ifdef _MSC_VER
import std;
#endif
export import :vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::rp {
    export struct CubemapToneMapping final : vk::raii::RenderPass {
        explicit CubemapToneMapping(const Gpu &gpu LIFETIMEBOUND)
            : RenderPass { gpu.device, vk::StructureChain {
                vk::RenderPassCreateInfo {
                    {},
                    vku::unsafeProxy(vk::AttachmentDescription {
                        {},
                        vk::Format::eB8G8R8A8Srgb, vk::SampleCountFlagBits::e1,
                        vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore,
                        {}, {},
                        {}, gpu.workaround.generalOr(vk::ImageLayout::eShaderReadOnlyOptimal),
                    }),
                    vku::unsafeProxy(vk::SubpassDescription {
                        {},
                        vk::PipelineBindPoint::eGraphics,
                        {},
                        vku::unsafeProxy(vk::AttachmentReference { 0, gpu.workaround.generalOr(vk::ImageLayout::eColorAttachmentOptimal) }),
                    }),
                },
                vk::RenderPassMultiviewCreateInfo {
                    vku::unsafeProxy(0b111111U),
                    {},
                    vku::unsafeProxy(0b000000U),
                },
            }.get() } { }
    };
}