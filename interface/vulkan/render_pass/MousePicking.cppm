module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.rp.MousePicking;

#ifdef _MSC_VER
import std;
#endif
export import vulkan_hpp;
import vku;

namespace vk_gltf_viewer::vulkan::rp {
    export struct MousePicking : vk::raii::RenderPass {
        explicit MousePicking(const vk::raii::Device &device LIFETIMEBOUND)
            : RenderPass { device, vk::RenderPassCreateInfo {
                {},
                vku::unsafeProxy({
                    vk::AttachmentDescription {
                        {},
                        vk::Format::eR16Uint, vk::SampleCountFlagBits::e1,
                        vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
                        {}, {},
                        {}, vk::ImageLayout::eColorAttachmentOptimal,
                    },
                    vk::AttachmentDescription {
                        {},
                        vk::Format::eD32Sfloat, vk::SampleCountFlagBits::e1,
                        vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
                        {}, {},
                        {}, vk::ImageLayout::eDepthStencilAttachmentOptimal,
                    },
                }),
                vku::unsafeProxy({
                    vk::SubpassDescription {
                        {},
                        vk::PipelineBindPoint::eGraphics,
                        {},
                        vku::unsafeProxy(vk::AttachmentReference { 0, vk::ImageLayout::eColorAttachmentOptimal }),
                        {},
                        vku::unsafeAddress(vk::AttachmentReference { 1, vk::ImageLayout::eDepthStencilAttachmentOptimal }),
                    },
                    vk::SubpassDescription {
                        {},
                        vk::PipelineBindPoint::eGraphics,
                        vku::unsafeProxy(vk::AttachmentReference { 0, vk::ImageLayout::eShaderReadOnlyOptimal }),
                    },
                }),
                vku::unsafeProxy(vk::SubpassDependency {
                    0, 1,
                    vk::PipelineStageFlagBits::eColorAttachmentOutput,
                    vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eColorAttachmentOutput,
                    vk::AccessFlagBits::eColorAttachmentWrite,
                    vk::AccessFlagBits::eInputAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,
                }),
            } } { }
    };
}