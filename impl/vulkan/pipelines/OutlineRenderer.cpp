module;

#include <array>
#include <compare>
#include <format>
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

layout (set = 0, binding = 0, rgba16ui) uniform readonly uimage2D jumpFloodImage;

layout (push_constant, std430) uniform PushConstant {
    vec3 outlineColor;
    float outlineThickness;
    ivec2 passthruOffset;
    bool useZwComponent;
} pc;

void main(){
    ivec2 sampleCoord = ivec2(gl_FragCoord.xy) - pc.passthruOffset;
    uvec4 inputTexel = imageLoad(jumpFloodImage, sampleCoord);
    float signedDistance = distance(pc.useZwComponent ? inputTexel.zw : inputTexel.xy, sampleCoord);
    outColor.rgb = pc.outlineColor;
    outColor.a = signedDistance > 1.0 ? smoothstep(pc.outlineThickness + 1.0, pc.outlineThickness, signedDistance) : 0.0;
}
)frag";

vk_gltf_viewer::vulkan::pipelines::OutlineRenderer::DescriptorSetLayouts::DescriptorSetLayouts(
    const vk::raii::Device &device
) : vku::DescriptorSetLayouts<1> {
        device,
        vk::DescriptorSetLayoutCreateInfo {
            {},
            vku::unsafeProxy({
                vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eFragment },
            }),
        },
    } { }

vk_gltf_viewer::vulkan::pipelines::OutlineRenderer::OutlineRenderer(
    const vk::raii::Device &device,
    const shaderc::Compiler &compiler
) : descriptorSetLayouts { device },
    pipelineLayout { createPipelineLayout(device) },
    pipeline { createPipeline(device, compiler) } { }

auto vk_gltf_viewer::vulkan::pipelines::OutlineRenderer::bindPipeline(
    vk::CommandBuffer commandBuffer
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
}

auto vk_gltf_viewer::vulkan::pipelines::OutlineRenderer::bindDescriptorSets(
    vk::CommandBuffer commandBuffer,
    const DescriptorSets &descriptorSets
) const -> void {
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, descriptorSets, {});
}

auto vk_gltf_viewer::vulkan::pipelines::OutlineRenderer::pushConstants(
    vk::CommandBuffer commandBuffer,
    const PushConstant &pushConstant
) const -> void {
    commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, pushConstant);
}

auto vk_gltf_viewer::vulkan::pipelines::OutlineRenderer::draw(
    vk::CommandBuffer commandBuffer
) const -> void {
    commandBuffer.draw(3, 1, 0, 0);
}

auto vk_gltf_viewer::vulkan::pipelines::OutlineRenderer::createPipelineLayout(
    const vk::raii::Device &device
) const -> decltype(pipelineLayout) {
    return { device, vk::PipelineLayoutCreateInfo{
        {},
        vku::unsafeProxy(descriptorSetLayouts.getHandles()),
        vku::unsafeProxy({
            vk::PushConstantRange {
                vk::ShaderStageFlagBits::eFragment,
                0, sizeof(PushConstant),
            },
        }),
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

    return { device, nullptr, vk::StructureChain {
        vku::getDefaultGraphicsPipelineCreateInfo(stages, *pipelineLayout, 1)
            .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
                {},
                false, false,
                vk::PolygonMode::eFill,
                vk::CullModeFlagBits::eNone, {},
                {}, {}, {}, {},
                1.0f,
            }))
            .setPColorBlendState(vku::unsafeAddress(vk::PipelineColorBlendStateCreateInfo {
                {},
                false, {},
                vku::unsafeProxy({
                    vk::PipelineColorBlendAttachmentState {
                        true,
                        vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                        vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd,
                        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
                    },
                }),
            })),
        vk::PipelineRenderingCreateInfo {
            {},
            vku::unsafeProxy({ vk::Format::eB8G8R8A8Srgb }),
        },
    }.get() };
}