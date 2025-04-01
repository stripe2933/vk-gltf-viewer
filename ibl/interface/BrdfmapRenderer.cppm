module;

#include <cstddef>

#include <lifetimebound.hpp>

export module ibl:BrdfmapRenderer;

import std;
export import vku;
import :shader.brdfmap_frag;
import :shader.screen_quad_vert;

namespace ibl {
    export class BrdfmapRenderer {
    public:
        struct SpecializationConstants {
            std::uint32_t numSamples = 1024;
        };

        struct Config {
            SpecializationConstants specializationConstants;
        };

        static constexpr vk::ImageUsageFlags requiredResultImageUsageFlags = vk::ImageUsageFlagBits::eColorAttachment;

        BrdfmapRenderer(
            const vk::raii::Device &device LIFETIMEBOUND,
            const vku::Image &resultImage LIFETIMEBOUND,
            const Config &config
        ) : device { device },
            resultImage { resultImage },
            pipelineLayout { device, vk::PipelineLayoutCreateInfo {
                {},
                {},
                vku::unsafeProxy(vk::PushConstantRange {
                    vk::ShaderStageFlagBits::eFragment,
                    0, sizeof(PushConstant),
                }),
            } },
            pipeline { device, nullptr, vk::StructureChain {
                vku::getDefaultGraphicsPipelineCreateInfo(
                    vku::createPipelineStages(
                        device,
                        vku::Shader { shader::screen_quad_vert, vk::ShaderStageFlagBits::eVertex },
                        vku::Shader {
                            shader::brdfmap_frag,
                            vk::ShaderStageFlagBits::eFragment,
                            // TODO: use vku::SpecializationMap when available.
                            vku::unsafeAddress(vk::SpecializationInfo {
                                vku::unsafeProxy(vk::SpecializationMapEntry { 0, offsetof(SpecializationConstants, numSamples), sizeof(SpecializationConstants::numSamples) }),
                                vk::ArrayProxyNoTemporaries<const SpecializationConstants> { config.specializationConstants },
                            }),
                        }).get(),
                    *pipelineLayout, 1)
                    .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
                        {},
                        false, false,
                        vk::PolygonMode::eFill,
                        vk::CullModeFlagBits::eNone, {},
                        false, 0.f, 0.f, 0.f,
                        1.f,
                    })),
                vk::PipelineRenderingCreateInfo {
                    {},
                    resultImage.format,
                },
            }.get() },
            attachmentGroup { vku::toExtent2D(resultImage.extent) } {
            attachmentGroup.addColorAttachment(device, resultImage);
        }

        void recordCommands(vk::CommandBuffer graphicsCommandBuffer) const {
            const auto *d = device.get().getDispatcher();

            graphicsCommandBuffer.beginRenderingKHR(attachmentGroup.getRenderingInfo(
                vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore }), *d);
            graphicsCommandBuffer.setViewport(0, vku::toViewport(attachmentGroup.extent), *d);
            graphicsCommandBuffer.setScissor(0, vk::Rect2D { {}, attachmentGroup.extent }, *d);
            graphicsCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline, *d);
            graphicsCommandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, PushConstant {
                .framebufferWidthRcp = 1.f / resultImage.get().extent.width,
                .framebufferHeightRcp = 1.f / resultImage.get().extent.height,
            }, *d);
            graphicsCommandBuffer.draw(3, 1, 0, 0, *d);
            graphicsCommandBuffer.endRenderingKHR(*d);
        }

    private:
        struct PushConstant {
            float framebufferWidthRcp;
            float framebufferHeightRcp;
        };

        std::reference_wrapper<const vk::raii::Device> device;
        std::reference_wrapper<const vku::Image> resultImage;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;
        vku::AttachmentGroup attachmentGroup;
    };
}