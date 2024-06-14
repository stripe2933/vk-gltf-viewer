module;

#include <compare>
#include <string_view>

#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.pipelines.OutlineRenderer;

import vku;

// language=vert
std::string_view vk_gltf_viewer::vulkan::pipelines::OutlineRenderer::vert = R"vert(
#version 450

const vec2 positions[] = vec2[3](
    vec2(-1.0, -3.0),
    vec2(-1.0, 1.0),
    vec2(3.0, 1.0)
);

void main(){
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
)vert";

// language=frag
std::string_view vk_gltf_viewer::vulkan::pipelines::OutlineRenderer::frag = R"frag(
#version 450

layout (location = 0) out vec4 outColor;

// TODO: use input attachment?
layout (set = 0, binding = 0, rg16ui) uniform readonly uimage2D jumpFloodImage;

layout (push_constant, std430) uniform PushConstant {
    vec3 outlineColor;
    float lineWidth;
} pc;

void main(){
    float alpha = 0.0;

    float signedDistance = distance(imageLoad(jumpFloodImage, ivec2(gl_FragCoord.xy)).xy, gl_FragCoord.xy);
    if (gl_FragCoord.x > pc.lineWidth && gl_FragCoord.y > pc.lineWidth && signedDistance > 1.0){
        alpha = smoothstep(pc.lineWidth + 1.0, pc.lineWidth, signedDistance);
    }

    outColor = vec4(pc.outlineColor, alpha);
}
)frag";

vk_gltf_viewer::vulkan::pipelines::OutlineRenderer::DescriptorSetLayouts::DescriptorSetLayouts(
    const vk::raii::Device &device
) : vku::DescriptorSetLayouts<1> {
        device,
        LayoutBindings {
            {},
            vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eFragment },
        },
    } { }

vk_gltf_viewer::vulkan::pipelines::OutlineRenderer::OutlineRenderer(
    const vk::raii::Device &device,
    const shaderc::Compiler &compiler
) : descriptorSetLayouts { device },
    pipelineLayout { createPipelineLayout(device) },
    pipeline { createPipeline(device, compiler) } { }

auto vk_gltf_viewer::vulkan::pipelines::OutlineRenderer::draw(
    vk::CommandBuffer commandBuffer,
    const DescriptorSets &descriptorSets,
    const PushConstant &pushConstant
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, descriptorSets, {});
    commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, pushConstant);
    commandBuffer.draw(3, 1, 0, 0);
}

auto vk_gltf_viewer::vulkan::pipelines::OutlineRenderer::createPipelineLayout(
    const vk::raii::Device &device
) const -> decltype(pipelineLayout) {
    constexpr vk::PushConstantRange pushConstantRange {
        vk::ShaderStageFlagBits::eFragment,
        0, sizeof(PushConstant),
    };
    return { device, vk::PipelineLayoutCreateInfo{
        {},
        descriptorSetLayouts,
        pushConstantRange,
    } };
}

auto vk_gltf_viewer::vulkan::pipelines::OutlineRenderer::createPipeline(
    const vk::raii::Device &device,
    const shaderc::Compiler &compiler
) const -> decltype(pipeline) {
    const auto [_, stages] = createStages(
        device,
        vku::Shader { compiler, vert, vk::ShaderStageFlagBits::eVertex },
        vku::Shader { compiler, frag, vk::ShaderStageFlagBits::eFragment });

    constexpr vk::PipelineRasterizationStateCreateInfo rasterizationState {
        {},
        false, false,
        vk::PolygonMode::eFill,
        vk::CullModeFlagBits::eNone, {},
        {}, {}, {}, {},
        1.0f,
    };

    constexpr std::array blendAttachmentStates {
        vk::PipelineColorBlendAttachmentState {
            true,
            vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
            vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
        },
    };
    const vk::PipelineColorBlendStateCreateInfo colorBlendState {
        {},
        false, {},
        blendAttachmentStates,
    };

    constexpr std::array colorAttachmentFormats { vk::Format::eB8G8R8A8Srgb };

    return { device, nullptr, vk::StructureChain {
        vku::getDefaultGraphicsPipelineCreateInfo(stages, *pipelineLayout, 1)
            .setPRasterizationState(&rasterizationState)
            .setPColorBlendState(&colorBlendState),
        vk::PipelineRenderingCreateInfo {
            {},
            colorAttachmentFormats,
        }
    }.get() };
}