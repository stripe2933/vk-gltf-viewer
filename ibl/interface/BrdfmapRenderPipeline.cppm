module;

#include <cstddef>

#include <lifetimebound.hpp>

export module ibl.BrdfmapRenderPipeline;

import std;
export import vku;

import ibl.shader.brdfmap_frag;
import ibl.shader.screen_quad_vert;

namespace ibl {
    export class BrdfmapRenderPipeline {
    public:
        struct SpecializationConstants {
            std::uint32_t numSamples = 1024;
        };

        struct Config {
            SpecializationConstants specializationConstants = {};
        };

        static constexpr vk::ImageUsageFlags requiredResultImageUsageFlags = vk::ImageUsageFlagBits::eColorAttachment;

        BrdfmapRenderPipeline(
            const vk::raii::Device &device LIFETIMEBOUND,
            const vku::Image &resultImage LIFETIMEBOUND,
            const Config &config = {
                .specializationConstants = {
                    .numSamples = 1024,
                },
            }
        );

        void recordCommands(vk::CommandBuffer graphicsCommandBuffer) const;

    private:
        struct PushConstant;

        std::reference_wrapper<const vk::raii::Device> device;
        std::reference_wrapper<const vku::Image> resultImage;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;
        vk::raii::ImageView imageView;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

struct ibl::BrdfmapRenderPipeline::PushConstant {
    float framebufferWidthRcp;
    float framebufferHeightRcp;
};

ibl::BrdfmapRenderPipeline::BrdfmapRenderPipeline(
    const vk::raii::Device &device,
    const vku::Image &resultImage,
    const Config &config
) : device { device },
    resultImage { resultImage },
    pipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        {},
        vku::lvalue(vk::PushConstantRange {
            vk::ShaderStageFlagBits::eFragment,
            0, sizeof(PushConstant),
        }),
    } },
    pipeline { device, nullptr, vk::StructureChain {
        vk::GraphicsPipelineCreateInfo {
            {},
            vku::lvalue({
                vk::PipelineShaderStageCreateInfo {
                    {},
                    vk::ShaderStageFlagBits::eVertex,
                    *vku::lvalue(vk::raii::ShaderModule { device, vk::ShaderModuleCreateInfo {
                        {},
                        shader::screen_quad_vert,
                    } }),
                    "main",
                },
                vk::PipelineShaderStageCreateInfo {
                    {},
                    vk::ShaderStageFlagBits::eFragment,
                    *vku::lvalue(vk::raii::ShaderModule { device, vk::ShaderModuleCreateInfo {
                        {},
                        shader::brdfmap_frag,
                    } }),
                    "main",
                    &vku::lvalue(vk::SpecializationInfo {
                        vku::lvalue(vk::SpecializationMapEntry { 0, offsetof(SpecializationConstants, numSamples), sizeof(SpecializationConstants::numSamples) }),
                        vk::ArrayProxyNoTemporaries<const SpecializationConstants> { config.specializationConstants },
                    }),
                },
            }),
            &vku::lvalue(vk::PipelineVertexInputStateCreateInfo{}),
            &vku::lvalue(vku::defaultPipelineInputAssemblyState(vk::PrimitiveTopology::eTriangleList)),
            nullptr,
            &vku::lvalue(vk::PipelineViewportStateCreateInfo {
                {},
                1, nullptr,
                1, nullptr,
            }),
            &vku::lvalue(vku::defaultPipelineRasterizationState()),
            &vku::lvalue(vk::PipelineMultisampleStateCreateInfo { {}, vk::SampleCountFlagBits::e1 }),
            nullptr,
            &vku::lvalue(vku::defaultPipelineColorBlendState(1)),
            &vku::lvalue(vk::PipelineDynamicStateCreateInfo {
                {},
                vku::lvalue({ vk::DynamicState::eViewport, vk::DynamicState::eScissor }),
            }),
            *pipelineLayout,
        },
        vk::PipelineRenderingCreateInfo {
            {},
            resultImage.format,
        },
    }.get() },
    imageView { device, resultImage.getViewCreateInfo(vk::ImageViewType::e2D) } { }

void ibl::BrdfmapRenderPipeline::recordCommands(vk::CommandBuffer graphicsCommandBuffer) const {
    const auto *d = device.get().getDispatcher();

    const vk::Rect2D renderArea { {}, vku::toExtent2D(resultImage.get().extent) };
    graphicsCommandBuffer.beginRenderingKHR({
        {},
        renderArea,
        1,
        {},
        vku::lvalue(vk::RenderingAttachmentInfo {
            *imageView, vk::ImageLayout::eColorAttachmentOptimal,
            {}, {}, {},
            vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore,
        }),
    }, *d);
    graphicsCommandBuffer.setViewport(0, vku::toViewport(renderArea), *d);
    graphicsCommandBuffer.setScissor(0, renderArea, *d);
    graphicsCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline, *d);
    graphicsCommandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, PushConstant {
        .framebufferWidthRcp = 1.f / resultImage.get().extent.width,
        .framebufferHeightRcp = 1.f / resultImage.get().extent.height,
    }, *d);
    graphicsCommandBuffer.draw(3, 1, 0, 0, *d);
    graphicsCommandBuffer.endRenderingKHR(*d);
}