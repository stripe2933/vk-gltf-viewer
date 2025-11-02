module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.render_pass.Tonemapping;

import std;
export import vulkan_hpp;

import vk_gltf_viewer.helpers.optional;

namespace vk_gltf_viewer::vulkan::rp {
    export struct Tonemapping final : vk::raii::RenderPass {
        struct Config {
            /// Input image format
            vk::Format inputFormat;

            /// Output image format
            vk::Format outputFormat;

            /// Whether the input image is preserved after the render pass.
            /// If <tt>true</tt>, <tt>storeOp=NONE</tt> is used for the input attachment.
            /// If <tt>false</tt>, <tt>storeOp=DONT_CARE</tt> is used.
            bool inputImagePreserved = true;

            /// View mask used when using VK_KHR_multiview.
            std::optional<std::uint32_t> multiviewMask = std::nullopt;
        };

        Tonemapping(
            const vk::raii::Device &device LIFETIMEBOUND,
            const Config &config
        );
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::rp::Tonemapping::Tonemapping(
    const vk::raii::Device &device,
    const Config &config
) : RenderPass { [&] {
        const vk::AttachmentDescription attachmentDescriptions[] = {
            vk::AttachmentDescription {
                {},
                config.inputFormat, vk::SampleCountFlagBits::e1,
                vk::AttachmentLoadOp::eLoad, config.inputImagePreserved ? vk::AttachmentStoreOp::eNone : vk::AttachmentStoreOp::eDontCare,
                {}, {},
                vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            },
            vk::AttachmentDescription {
                {},
                config.outputFormat, vk::SampleCountFlagBits::e1,
                vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore,
                {}, {},
                {}, vk::ImageLayout::eShaderReadOnlyOptimal,
            },
        };

        constexpr vk::AttachmentReference inputAttachmentReference { 0, vk::ImageLayout::eShaderReadOnlyOptimal };
        constexpr vk::AttachmentReference colorAttachmentReference { 1, vk::ImageLayout::eColorAttachmentOptimal };

        const vk::SubpassDescription subpassDescription {
            {},
            vk::PipelineBindPoint::eGraphics,
            inputAttachmentReference,
            colorAttachmentReference,
        };

        constexpr std::uint32_t correlationMasks[] = { 0U };

        vk::StructureChain createInfo {
            vk::RenderPassCreateInfo {
                {},
                attachmentDescriptions,
                subpassDescription,
            },
            vk::RenderPassMultiviewCreateInfo {
                {},
                {},
                correlationMasks,
            },
        };
        if (config.multiviewMask) {
            createInfo.get<vk::RenderPassMultiviewCreateInfo>().setViewMasks(*config.multiviewMask);
        }
        else {
            createInfo.unlink<vk::RenderPassMultiviewCreateInfo>();
        }

        return vk::raii::RenderPass { device, createInfo.get() };
    }() } { }