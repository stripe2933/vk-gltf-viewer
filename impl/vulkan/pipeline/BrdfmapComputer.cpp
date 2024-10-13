module;

#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.pipeline.BrdfmapComputer;

import std;

vk_gltf_viewer::vulkan::pipeline::BrdfmapComputer::DescriptorSetLayout::DescriptorSetLayout(
    const vk::raii::Device &device
) : vku::DescriptorSetLayout<vk::DescriptorType::eStorageImage> {
        device,
        vk::DescriptorSetLayoutCreateInfo {
            {},
            vku::unsafeProxy(vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute }),
        }
    } { }

vk_gltf_viewer::vulkan::pipeline::BrdfmapComputer::BrdfmapComputer(
    const vk::raii::Device &device,
    const SpecializationConstants &specializationConstants
) : descriptorSetLayout { device },
    pipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        *descriptorSetLayout,
    } },
    pipeline { device, nullptr, vk::ComputePipelineCreateInfo {
        {},
        createPipelineStages(
            device,
            vku::Shader::fromSpirvFile(
                COMPILED_SHADER_DIR "/brdfmap.comp.spv",
                vk::ShaderStageFlagBits::eCompute,
                vku::unsafeAddress(vk::SpecializationInfo {
                    vku::unsafeProxy(vk::SpecializationMapEntry { 0, 0, sizeof(SpecializationConstants::numSamples) }),
                    vk::ArrayProxyNoTemporaries<const SpecializationConstants>(specializationConstants),
                }))).get()[0],
        *pipelineLayout,
    } } { }

auto vk_gltf_viewer::vulkan::pipeline::BrdfmapComputer::compute(
    vk::CommandBuffer commandBuffer,
    vku::DescriptorSet<DescriptorSetLayout> descriptorSet,
    const vk::Extent2D &imageSize
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, descriptorSet, {});
    commandBuffer.dispatch(imageSize.width / 16, imageSize.height / 16, 1);
}