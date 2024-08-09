module;

#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.pipeline.SkyboxRenderer;

import std;

vk_gltf_viewer::vulkan::pipeline::SkyboxRenderer::DescriptorSetLayout::DescriptorSetLayout(
    const vk::raii::Device &device,
    const CubemapSampler &sampler
) : vku::DescriptorSetLayout<vk::DescriptorType::eCombinedImageSampler> {
        device,
        vk::DescriptorSetLayoutCreateInfo {
            {},
            vku::unsafeProxy({
                vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, &*sampler },
            }),
        },
    } { }

vk_gltf_viewer::vulkan::pipeline::SkyboxRenderer::SkyboxRenderer(
    const vk::raii::Device &device,
    const CubemapSampler &sampler,
    const buffer::CubeIndices &cubeIndices
) : descriptorSetLayout { device, sampler },
    pipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        *descriptorSetLayout,
        vku::unsafeProxy({
            vk::PushConstantRange {
                vk::ShaderStageFlagBits::eVertex,
                0, sizeof(PushConstant),
            },
        }),
    } },
    pipeline { device, nullptr, vk::StructureChain {
        vku::getDefaultGraphicsPipelineCreateInfo(
            createPipelineStages(
                device,
                vku::Shader { COMPILED_SHADER_DIR "/skybox.vert.spv", vk::ShaderStageFlagBits::eVertex },
                vku::Shader { COMPILED_SHADER_DIR "/skybox.frag.spv", vk::ShaderStageFlagBits::eFragment }).get(),
            *pipelineLayout,
            1, true,
            vk::SampleCountFlagBits::e4)
            .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
                {},
                false, false,
                vk::PolygonMode::eFill,
                vk::CullModeFlagBits::eNone, {},
                {}, {}, {}, {},
                1.f,
            }))
            .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                {},
                true, false, vk::CompareOp::eEqual,
            })),
        vk::PipelineRenderingCreateInfo {
            {},
            vku::unsafeProxy({ vk::Format::eB8G8R8A8Srgb }),
            vk::Format::eD32Sfloat,
        },
    }.get() },
    cubeIndices { cubeIndices } { }

auto vk_gltf_viewer::vulkan::pipeline::SkyboxRenderer::draw(
    vk::CommandBuffer commandBuffer,
    vku::DescriptorSet<DescriptorSetLayout> descriptorSet,
    const PushConstant &pushConstant
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, descriptorSet, {});
    commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, pushConstant);
    commandBuffer.bindIndexBuffer(cubeIndices, 0, vk::IndexType::eUint16);
    commandBuffer.drawIndexed(36, 1, 0, 0, 0);
}