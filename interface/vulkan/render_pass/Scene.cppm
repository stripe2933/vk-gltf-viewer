module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.rp.Scene;

#ifdef _MSC_VER
import std;
#endif
import vku;
export import vulkan_hpp;

namespace vk_gltf_viewer::vulkan::rp {
    export struct Scene final : vk::raii::RenderPass {
        explicit Scene(const vk::raii::Device &device LIFETIMEBOUND)
            : RenderPass { device, vk::RenderPassCreateInfo {
                {},
                vku::unsafeProxy({
                    // Opaque MSAA color attachment.
                    vk::AttachmentDescription {
                        {},
                        vk::Format::eB8G8R8A8Srgb, vk::SampleCountFlagBits::e4,
                        vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
                        {}, {},
                        {}, vk::ImageLayout::eColorAttachmentOptimal,
                    },
                    // Opaque MSAA resolve attachment (=result image)
                    vk::AttachmentDescription {
                        {},
                        vk::Format::eB8G8R8A8Srgb, vk::SampleCountFlagBits::e1,
                        vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore,
                        {}, {},
                        {}, vk::ImageLayout::eColorAttachmentOptimal,
                    },
                    // Depth image.
                    vk::AttachmentDescription {
                        {},
                        vk::Format::eD32Sfloat, vk::SampleCountFlagBits::e4,
                        vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
                        {}, {},
                        {}, vk::ImageLayout::eDepthStencilAttachmentOptimal,
                    },
                    // Accumulation color image.
                    vk::AttachmentDescription {
                        {},
                        vk::Format::eR16G16B16A16Sfloat, vk::SampleCountFlagBits::e4,
                        vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
                        {}, {},
                        {}, vk::ImageLayout::eColorAttachmentOptimal,
                    },
                    // Accumulation resolve image.
                    vk::AttachmentDescription {
                        {},
                        vk::Format::eR16G16B16A16Sfloat, vk::SampleCountFlagBits::e1,
                        vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eNone,
                        {}, {},
                        {}, vk::ImageLayout::eShaderReadOnlyOptimal,
                    },
                    // Revealage color image.
                    vk::AttachmentDescription {
                        {},
                        vk::Format::eR16Unorm, vk::SampleCountFlagBits::e4,
                        vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
                        {}, {},
                        {}, vk::ImageLayout::eColorAttachmentOptimal,
                    },
                    // Revealage resolve image.
                    vk::AttachmentDescription {
                        {},
                        vk::Format::eR16Unorm, vk::SampleCountFlagBits::e1,
                        vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eNone,
                        {}, {},
                        {}, vk::ImageLayout::eShaderReadOnlyOptimal,
                    },
                }),
                vku::unsafeProxy({
                    // Opaque pass.
                    vk::SubpassDescription {
                        {},
                        vk::PipelineBindPoint::eGraphics,
                        {},
                        vku::unsafeProxy(vk::AttachmentReference { 0, vk::ImageLayout::eColorAttachmentOptimal }),
                        vku::unsafeProxy(vk::AttachmentReference { 1, vk::ImageLayout::eColorAttachmentOptimal }),
                        vku::unsafeAddress(vk::AttachmentReference { 2, vk::ImageLayout::eDepthStencilAttachmentOptimal }),
                    },
                    // Weighted blended pass.
                    vk::SubpassDescription {
                        {},
                        vk::PipelineBindPoint::eGraphics,
                        {},
                        vku::unsafeProxy({
                            vk::AttachmentReference { 3, vk::ImageLayout::eColorAttachmentOptimal },
                            vk::AttachmentReference { 5, vk::ImageLayout::eColorAttachmentOptimal },
                        }),
                        vku::unsafeProxy({
                            vk::AttachmentReference { 4, vk::ImageLayout::eColorAttachmentOptimal },
                            vk::AttachmentReference { 6, vk::ImageLayout::eColorAttachmentOptimal },
                        }),
                        vku::unsafeAddress(vk::AttachmentReference { 2, vk::ImageLayout::eDepthStencilAttachmentOptimal }),
                    },
                    // Composition pass.
                    vk::SubpassDescription {
                        {},
                        vk::PipelineBindPoint::eGraphics,
                        vku::unsafeProxy({
                            vk::AttachmentReference { 4, vk::ImageLayout::eShaderReadOnlyOptimal },
                            vk::AttachmentReference { 6, vk::ImageLayout::eShaderReadOnlyOptimal },
                        }),
                        vku::unsafeProxy(vk::AttachmentReference { 1, vk::ImageLayout::eColorAttachmentOptimal }),
                    },
                }),
                vku::unsafeProxy({
                    // Dependency between opaque pass and weighted blended pass:
                    // Since weighted blended uses the result of depth attachment from opaque pass, it must be finished before weighted blended pass.
                    vk::SubpassDependency {
                        0, 1,
                        vk::PipelineStageFlagBits::eLateFragmentTests, vk::PipelineStageFlagBits::eEarlyFragmentTests,
                        vk::AccessFlagBits::eDepthStencilAttachmentWrite, vk::AccessFlagBits::eDepthStencilAttachmentRead,
                    },
                    // Dependency between opaque pass and WBOIT composition pass:
                    // Color attachments must be written before full-quad pass writes them.
                    vk::SubpassDependency {
                        0, 2,
                        vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eColorAttachmentRead,
                    },
                    // Dependency between blend pass and WBOIT composition pass:
                    // Color attachments must be written before they are read as input attachments
                    vk::SubpassDependency {
                        1, 2,
                        vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eFragmentShader,
                        vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eInputAttachmentRead,
                    },
                    vk::SubpassDependency {
                        2, 2,
                        vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,
                        vk::DependencyFlagBits::eByRegion,
                    },
                }),
            } } { }
    };
}