module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.render_pass.CubemapToneMapping;

import std;
import vku;
export import vulkan_hpp;

namespace vk_gltf_viewer::vulkan::rp {
    export struct CubemapToneMapping final : vk::raii::RenderPass {
        explicit CubemapToneMapping(const vk::raii::Device &device LIFETIMEBOUND);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::rp::CubemapToneMapping::CubemapToneMapping(const vk::raii::Device &device)
    : RenderPass { device, vk::StructureChain {
        vk::RenderPassCreateInfo {
            {},
            vku::lvalue(vk::AttachmentDescription {
                {},
                vk::Format::eB8G8R8A8Srgb, vk::SampleCountFlagBits::e1,
                vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore,
                {}, {},
                {}, vk::ImageLayout::eShaderReadOnlyOptimal,
            }),
            vku::lvalue(vk::SubpassDescription {
                {},
                vk::PipelineBindPoint::eGraphics,
                {},
                vku::lvalue(vk::AttachmentReference { 0, vk::ImageLayout::eColorAttachmentOptimal }),
            }),
        },
        vk::RenderPassMultiviewCreateInfo {
            vk::ArrayProxyNoTemporaries<const std::uint32_t> { vku::lvalue(0b111111U) },
            {},
            vk::ArrayProxyNoTemporaries<const std::uint32_t> { vku::lvalue(0b000000U) },
        },
    }.get() } { }