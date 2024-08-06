module;

#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.pipeline.Rec709Renderer;

import std;
import glm;

struct vk_gltf_viewer::vulkan::pipeline::Rec709Renderer::PushConstant {
    glm::i32vec2 hdriImageOffset;
};

vk_gltf_viewer::vulkan::pipeline::Rec709Renderer::DescriptorSetLayout::DescriptorSetLayout(
    const vk::raii::Device &device
) : vku::DescriptorSetLayout<vk::DescriptorType::eStorageImage> {
        device,
        vk::DescriptorSetLayoutCreateInfo {
            {},
            vku::unsafeProxy({
                vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eFragment },
            }),
        },
    } { }

vk_gltf_viewer::vulkan::pipeline::Rec709Renderer::Rec709Renderer(
    const vk::raii::Device &device
) : descriptorSetLayout { device },
    pipelineLayout { device, vk::PipelineLayoutCreateInfo{
        {},
        *descriptorSetLayout,
        vku::unsafeProxy({
            vk::PushConstantRange {
                vk::ShaderStageFlagBits::eFragment,
                0, sizeof(PushConstant),
            },
        }),
    } },
    pipeline { device, nullptr, vk::StructureChain {
        vku::getDefaultGraphicsPipelineCreateInfo(
            createPipelineStages(
                device,
                vku::Shader { COMPILED_SHADER_DIR "/rec709.vert.spv", vk::ShaderStageFlagBits::eVertex },
                vku::Shader { COMPILED_SHADER_DIR "/rec709.frag.spv", vk::ShaderStageFlagBits::eFragment }).get(),
            *pipelineLayout,
            1)
            .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
                {},
                false, false,
                vk::PolygonMode::eFill,
                vk::CullModeFlagBits::eNone, {},
                {}, {}, {}, {},
                1.f,
            })),
        vk::PipelineRenderingCreateInfo {
            {},
            vku::unsafeProxy({ vk::Format::eB8G8R8A8Srgb }),
        },
    }.get() } { }

auto vk_gltf_viewer::vulkan::pipeline::Rec709Renderer::draw(
    vk::CommandBuffer commandBuffer,
    vku::DescriptorSet<DescriptorSetLayout> descriptorSet,
    const vk::Offset2D &passthruOffset
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, descriptorSet, {});
    commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, PushConstant {
        .hdriImageOffset = { passthruOffset.x, passthruOffset.y },
    });
    commandBuffer.draw(3, 1, 0, 0);
}