module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.render_pass.Scene;

#ifdef _MSC_VER
import std;
#endif
import vku;
export import vulkan_hpp;

export import vk_gltf_viewer.vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::rp {
    export struct Scene final : vk::raii::RenderPass {
        vk::SampleCountFlagBits sampleCount;
        
        Scene(const Gpu &gpu LIFETIMEBOUND, vk::SampleCountFlagBits sampleCount);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::rp::Scene::Scene(const Gpu &gpu, vk::SampleCountFlagBits sampleCount)
    : RenderPass { [&] {
        constexpr vk::SubpassDependency2 subpassDependencies[] = {
            // Dependency between opaque pass and weighted blended pass:
            // Since weighted blended uses the result of depth attachment from opaque pass, it must be finished before weighted blended pass.
            vk::SubpassDependency2 {
                0, 1,
                vk::PipelineStageFlagBits::eLateFragmentTests, vk::PipelineStageFlagBits::eEarlyFragmentTests,
                vk::AccessFlagBits::eDepthStencilAttachmentWrite, vk::AccessFlagBits::eDepthStencilAttachmentRead,
            },
            // Dependency between opaque pass and WBOIT composition pass:
            // Color attachments must be written before full-quad pass writes them.
            vk::SubpassDependency2 {
                0, 2,
                vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput,
                vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eColorAttachmentRead,
            },
            // Dependency between blend pass and WBOIT composition pass:
            // Color attachments must be written before they are read as input attachments
            vk::SubpassDependency2 {
                1, 2,
                vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eFragmentShader,
                vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eInputAttachmentRead,
            },
            // Dependency between WBOIT composition pass and inverse tone mapping pass:
            // Composited image attachment must be written before its layout is read as the input attachment.
            vk::SubpassDependency2 {
                2, 3,
                vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eFragmentShader,
                vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eInputAttachmentRead,
            },
        };

        if (sampleCount == vk::SampleCountFlagBits::e1) {
            return RenderPass { gpu.device, vk::RenderPassCreateInfo2 {
                {},
                vku::lvalue({
                    // (0) Opaque MSAA resolve attachment (=result image)
                    vk::AttachmentDescription2 {
                        {},
                        vk::Format::eB8G8R8A8Srgb, vk::SampleCountFlagBits::e1,
                        vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                        {}, {},
                        {}, vk::ImageLayout::eColorAttachmentOptimal,
                    },
                    // (1) Depth/stencil image.
                    vk::AttachmentDescription2 {
                        {},
                        vk::Format::eD32SfloatS8Uint, vk::SampleCountFlagBits::e1,
                        vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
                        vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
                        {}, vk::ImageLayout::eDepthStencilReadOnlyOptimal,
                    },
                    // (2) Accumulation color image.
                    vk::AttachmentDescription2 {
                        {},
                        vk::Format::eR16G16B16A16Sfloat, vk::SampleCountFlagBits::e1,
                        vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
                        {}, {},
                        {}, vk::ImageLayout::eColorAttachmentOptimal,
                    },
                    // (3) Revealage color image.
                    vk::AttachmentDescription2 {
                        {},
                        vk::Format::eR16Unorm, vk::SampleCountFlagBits::e1,
                        vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
                        {}, {},
                        {}, vk::ImageLayout::eColorAttachmentOptimal,
                    },
                }),
                vku::lvalue({
                    // Opaque pass.
                    vk::SubpassDescription2 {
                        {},
                        vk::PipelineBindPoint::eGraphics,
                        {},
                        {},
                        vku::lvalue(vk::AttachmentReference2 { 0, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor }),
                        {},
                        &vku::lvalue(vk::AttachmentReference2 { 1, vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil }),
                    },
                    // Weighted blended pass.
                    vk::SubpassDescription2 {
                        {},
                        vk::PipelineBindPoint::eGraphics,
                        {},
                        {},
                        vku::lvalue({
                            vk::AttachmentReference2 { 2, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor },
                            vk::AttachmentReference2 { 3, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor },
                        }),
                        {},
                        &vku::lvalue(vk::AttachmentReference2 { 1, vk::ImageLayout::eDepthReadOnlyStencilAttachmentOptimal, vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil }),
                    },
                    // Composition pass.
                    vk::SubpassDescription2 {
                        {},
                        vk::PipelineBindPoint::eGraphics,
                        {},
                        vku::lvalue({
                            vk::AttachmentReference2 { 2, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor },
                            vk::AttachmentReference2 { 3, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor },
                        }),
                        vku::lvalue(vk::AttachmentReference2 { 0, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor }),
                    },
                    // Inverse tone mapping pass.
                    vk::SubpassDescription2 {
                        {},
                        vk::PipelineBindPoint::eGraphics,
                        {},
                        vku::lvalue(vk::AttachmentReference2 { 0, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor }),
                        {},
                        {},
                        &vku::lvalue(vk::AttachmentReference2 { 1, vk::ImageLayout::eDepthStencilReadOnlyOptimal, vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil }), // remain the depthlayout from 2nd pass
                    },
                }),
                subpassDependencies,
            } };
        }
        else {
            return RenderPass { gpu.device, vk::RenderPassCreateInfo2 {
                {},
                vku::lvalue({
                    // (0) Opaque MSAA color attachment.
                    vk::AttachmentDescription2 {
                        {},
                        vk::Format::eB8G8R8A8Srgb, sampleCount,
                        vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
                        {}, {},
                        {}, vk::ImageLayout::eColorAttachmentOptimal,
                    },
                    // (1) Opaque MSAA resolve attachment (=result image)
                    vk::AttachmentDescription2 {
                        {},
                        vk::Format::eB8G8R8A8Srgb, vk::SampleCountFlagBits::e1,
                        vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore,
                        {}, {},
                        {}, vk::ImageLayout::eColorAttachmentOptimal,
                    },
                    // (2) Depth/stencil image.
                    vk::AttachmentDescription2 {
                        {},
                        vk::Format::eD32SfloatS8Uint, sampleCount,
                        vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
                        vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
                        {}, vk::ImageLayout::eDepthReadOnlyStencilAttachmentOptimal,
                    },
                    // (3) Stencil resolve image.
                    vk::AttachmentDescription2 {
                        {},
                        gpu.supportS8UintDepthStencilAttachment && !gpu.workaround.depthStencilResolveDifferentFormat
                            ? vk::Format::eS8Uint
                            : vk::Format::eD32SfloatS8Uint,
                        vk::SampleCountFlagBits::e1,
                        vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                        vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                        {},
                        gpu.supportS8UintDepthStencilAttachment && !gpu.workaround.depthStencilResolveDifferentFormat
                            ? vk::ImageLayout::eStencilReadOnlyOptimal
                            : vk::ImageLayout::eDepthStencilReadOnlyOptimal,
                    },
                    // (4) Accumulation color image.
                    vk::AttachmentDescription2 {
                        {},
                        vk::Format::eR16G16B16A16Sfloat, sampleCount,
                        vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
                        {}, {},
                        {}, vk::ImageLayout::eColorAttachmentOptimal,
                    },
                    // (5) Accumulation resolve image.
                    vk::AttachmentDescription2 {
                        {},
                        vk::Format::eR16G16B16A16Sfloat, vk::SampleCountFlagBits::e1,
                        vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eNone,
                        {}, {},
                        {}, vk::ImageLayout::eShaderReadOnlyOptimal,
                    },
                    // (6) Revealage color image.
                    vk::AttachmentDescription2 {
                        {},
                        vk::Format::eR16Unorm, sampleCount,
                        vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
                        {}, {},
                        {}, vk::ImageLayout::eColorAttachmentOptimal,
                    },
                    // (7) Revealage resolve image.
                    vk::AttachmentDescription2 {
                        {},
                        vk::Format::eR16Unorm, vk::SampleCountFlagBits::e1,
                        vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eNone,
                        {}, {},
                        {}, vk::ImageLayout::eShaderReadOnlyOptimal,
                    },
                }),
                vku::lvalue({
                    // Opaque pass.
                    vk::StructureChain {
                        vk::SubpassDescription2 {
                            {},
                            vk::PipelineBindPoint::eGraphics,
                            {},
                            {},
                            vku::lvalue(vk::AttachmentReference2 { 0, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor }),
                            vku::lvalue(vk::AttachmentReference2 { 1, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor }),
                            &vku::lvalue(vk::AttachmentReference2 { 2, vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil }),
                        },
                        vk::SubpassDescriptionDepthStencilResolve {
                            vk::ResolveModeFlagBits::eNone,
                            vk::ResolveModeFlagBits::eSampleZero,
                            &vku::lvalue(vk::AttachmentReference2 {
                                3,
                                gpu.supportS8UintDepthStencilAttachment && !gpu.workaround.depthStencilResolveDifferentFormat
                                    ? vk::ImageLayout::eStencilAttachmentOptimal
                                    : vk::ImageLayout::eDepthStencilAttachmentOptimal,
                                vk::ImageAspectFlagBits::eStencil,
                            }),
                        },
                    }.get(),
                    // Weighted blended pass.
                    vk::StructureChain {
                        vk::SubpassDescription2 {
                            {},
                            vk::PipelineBindPoint::eGraphics,
                            {},
                            {},
                            vku::lvalue({
                                vk::AttachmentReference2 { 4, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor },
                                vk::AttachmentReference2 { 6, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor },
                            }),
                            vku::lvalue({
                                vk::AttachmentReference2 { 5, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor },
                                vk::AttachmentReference2 { 7, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor },
                            }),
                            &vku::lvalue(vk::AttachmentReference2 { 2, vk::ImageLayout::eDepthReadOnlyStencilAttachmentOptimal, vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil }),
                        },
                        vk::SubpassDescriptionDepthStencilResolve {
                            vk::ResolveModeFlagBits::eNone,
                            vk::ResolveModeFlagBits::eSampleZero,
                            &vku::lvalue(vk::AttachmentReference2 {
                                3,
                                gpu.supportS8UintDepthStencilAttachment && !gpu.workaround.depthStencilResolveDifferentFormat
                                    ? vk::ImageLayout::eStencilAttachmentOptimal
                                    : vk::ImageLayout::eDepthStencilAttachmentOptimal,
                                vk::ImageAspectFlagBits::eStencil,
                            }),
                        },
                    }.get(),
                    // Composition pass.
                    vk::SubpassDescription2 {
                        {},
                        vk::PipelineBindPoint::eGraphics,
                        {},
                        vku::lvalue({
                            vk::AttachmentReference2 { 5, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor },
                            vk::AttachmentReference2 { 7, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor },
                        }),
                        vku::lvalue(vk::AttachmentReference2 { 1, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor }),
                    },
                    // Inverse tone mapping pass.
                    vk::SubpassDescription2 {
                        {},
                        vk::PipelineBindPoint::eGraphics,
                        {},
                        vku::lvalue({
                            vk::AttachmentReference2 { 1, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor },
                        }),
                        {},
                        {},
                        &vku::lvalue(vk::AttachmentReference2 {
                            3,
                            gpu.supportS8UintDepthStencilAttachment && !gpu.workaround.depthStencilResolveDifferentFormat
                                ? vk::ImageLayout::eStencilReadOnlyOptimal
                                : vk::ImageLayout::eDepthStencilReadOnlyOptimal,
                            vk::ImageAspectFlagBits::eStencil,
                        }),
                    },
                }),
                subpassDependencies,
            } };
        }
    }() }
    , sampleCount { sampleCount } { }