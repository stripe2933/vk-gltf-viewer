module;

#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.pipeline.BrdfmapComputer;

import std;
import vku;

vk_gltf_viewer::vulkan::pipeline::BrdfmapComputer::DescriptorSetLayouts::DescriptorSetLayouts(
    const vk::raii::Device &device
) : vku::DescriptorSetLayouts<1> {
        device,
        vk::DescriptorSetLayoutCreateInfo {
            {},
            vku::unsafeProxy({
                vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute },
            }),
        }
    } { }

vk_gltf_viewer::vulkan::pipeline::BrdfmapComputer::BrdfmapComputer(
    const vk::raii::Device &device,
    const SpecializationConstants &specializationConstants
) : descriptorSetLayouts { device },
    pipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        vku::unsafeProxy(descriptorSetLayouts.getHandles()),
    } },
    pipeline { device, nullptr, vk::ComputePipelineCreateInfo {
        {},
        get<0>(vku::createPipelineStages(
            device,
            // TODO: apply specializationConstants.
            vku::Shader { COMPILED_SHADER_DIR "/brdfmap.comp.spv", vk::ShaderStageFlagBits::eCompute }).get()),
        *pipelineLayout,
    } } { }

auto vk_gltf_viewer::vulkan::pipeline::BrdfmapComputer::compute(
    vk::CommandBuffer commandBuffer,
    const DescriptorSets &descriptorSets,
    const vk::Extent2D &imageSize
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, descriptorSets, {});
    commandBuffer.dispatch(imageSize.width / 16, imageSize.height / 16, 1);
}