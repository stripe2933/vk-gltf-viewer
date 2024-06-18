module;

#include <compare>
#include <format>
#include <string_view>
#include <vector>

#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.pipelines.SkyboxRenderer;

import vku;

// language=vert
std::string_view vk_gltf_viewer::vulkan::pipelines::SkyboxRenderer::vert = R"vert(
#version 450

const vec3[] positions = vec3[8](
    vec3(-1.0, -1.0, -1.0),
    vec3(-1.0, -1.0,  1.0),
    vec3(-1.0,  1.0, -1.0),
    vec3(-1.0,  1.0,  1.0),
    vec3( 1.0, -1.0, -1.0),
    vec3( 1.0, -1.0,  1.0),
    vec3( 1.0,  1.0, -1.0),
    vec3( 1.0,  1.0,  1.0)
);

layout (location = 0) out vec3 fragPosition;

layout (push_constant) uniform PushConstant {
    mat4 projectionView;
} pc;

void main() {
    fragPosition = positions[gl_VertexIndex];
    gl_Position = (pc.projectionView * vec4(fragPosition, 1.0)).xyww;
}
)vert";

// language=frag
std::string_view vk_gltf_viewer::vulkan::pipelines::SkyboxRenderer::frag = R"frag(
#version 450

layout (location = 0) in vec3 fragPosition;

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform samplerCube cubemapSampler;

layout (early_fragment_tests) in;

void main() {
    outColor = vec4(textureLod(cubemapSampler, fragPosition, 0.0).rgb, 1.0);
}
)frag";

vk_gltf_viewer::vulkan::pipelines::SkyboxRenderer::DescriptorSetLayouts::DescriptorSetLayouts(
    const vk::raii::Device &device,
    const vk::Sampler &sampler
) : vku::DescriptorSetLayouts<1> { device, LayoutBindings {
        {},
        vk::DescriptorSetLayoutBinding {
            0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, &sampler
        },
    } } { }

vk_gltf_viewer::vulkan::pipelines::SkyboxRenderer::SkyboxRenderer(
    const Gpu &gpu,
    const shaderc::Compiler &compiler
) : sampler { createSampler(gpu.device) },
    descriptorSetLayouts { gpu.device, *sampler },
    pipelineLayout { createPipelineLayout(gpu.device) },
    pipeline { createPipeline(gpu.device, compiler) },
    indexBuffer { createIndexBuffer(gpu.allocator) } { }

auto vk_gltf_viewer::vulkan::pipelines::SkyboxRenderer::draw(
    vk::CommandBuffer commandBuffer,
    const DescriptorSets &descriptorSets,
    const PushConstant &pushConstant
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, descriptorSets, {});
    commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eAllGraphics, 0, pushConstant);
    commandBuffer.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint16);
    commandBuffer.drawIndexed(36, 1, 0, 0, 0);
}

auto vk_gltf_viewer::vulkan::pipelines::SkyboxRenderer::createSampler(
    const vk::raii::Device &device
) const -> decltype(sampler) {
    return { device, vk::SamplerCreateInfo {
        {},
        vk::Filter::eLinear, vk::Filter::eLinear, {},
        {}, {}, {},
        {},
        {}, {},
        {}, {},
        0.f, vk::LodClampNone,
    } };
}

auto vk_gltf_viewer::vulkan::pipelines::SkyboxRenderer::createPipelineLayout(
    const vk::raii::Device &device
) const -> vk::raii::PipelineLayout {
    constexpr vk::PushConstantRange pushConstantRange {
        vk::ShaderStageFlagBits::eAllGraphics,
        0, sizeof(PushConstant),
    };
    return { device, vk::PipelineLayoutCreateInfo {
        {},
        descriptorSetLayouts,
        pushConstantRange,
    } };
}

auto vk_gltf_viewer::vulkan::pipelines::SkyboxRenderer::createPipeline(
    const vk::raii::Device &device,
    const shaderc::Compiler &compiler
) const -> vk::raii::Pipeline {
    const auto [_, stages] = createStages(
        device,
        vku::Shader { compiler, vert, vk::ShaderStageFlagBits::eVertex },
        vku::Shader { compiler, frag, vk::ShaderStageFlagBits::eFragment });

    constexpr vk::PipelineRasterizationStateCreateInfo rasterizationState {
        {},
        vk::False, vk::False,
        vk::PolygonMode::eFill,
        vk::CullModeFlagBits::eNone, {},
        {}, {}, {}, {},
        1.f,
    };
    constexpr vk::PipelineDepthStencilStateCreateInfo depthStencilState {
        {},
        vk::True, vk::True, vk::CompareOp::eLessOrEqual,
    };

    constexpr vk::Format colorAttachmentFormat = vk::Format::eR16G16B16A16Sfloat;

    return { device, nullptr, vk::StructureChain {
        vku::getDefaultGraphicsPipelineCreateInfo(stages, *pipelineLayout, 1, true, vk::SampleCountFlagBits::e4)
            .setPRasterizationState(&rasterizationState)
            .setPDepthStencilState(&depthStencilState),
        vk::PipelineRenderingCreateInfo {
            {},
            colorAttachmentFormat,
            vk::Format::eD32Sfloat,
        },
    }.get() };
}

auto vk_gltf_viewer::vulkan::pipelines::SkyboxRenderer::createIndexBuffer(
    vma::Allocator allocator
) const -> decltype(indexBuffer) {
    return {
        allocator,
        std::from_range, std::vector<std::uint16_t> {
            2, 6, 7, 2, 3, 7, 0, 4, 5, 0, 1, 5, 0, 2, 6, 0, 4, 6,
            1, 3, 7, 1, 5, 7, 0, 2, 3, 0, 1, 3, 4, 6, 7, 4, 5, 7,
        },
        vk::BufferUsageFlagBits::eIndexBuffer,
    };
}