module;

#include <compare>
#include <string_view>

#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.pipelines.Rec709Renderer;

import vku;

// language=vert
std::string_view vk_gltf_viewer::vulkan::pipelines::Rec709Renderer::vert = R"vert(
#version 460

const vec2 positions[] = vec2[3](
    vec2(-1.0, -3.0),
    vec2(-1.0, 1.0),
    vec2(3.0, 1.0)
);

layout (location = 0) out vec2 fragTexcoord;

void main(){
    fragTexcoord = 0.5 * (positions[gl_VertexIndex] + 1.0);
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
)vert";

// language=frag
std::string_view vk_gltf_viewer::vulkan::pipelines::Rec709Renderer::frag = R"frag(
#version 450

const vec3 REC_709_LUMA = vec3(0.2126, 0.7152, 0.0722);

layout (location = 0) in vec2 fragTexcoord;

layout (location = 0) out vec4 outColor;

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput inputImage;

void main(){
    vec4 color = subpassLoad(inputImage);
    float luminance = dot(color.rgb, REC_709_LUMA);
    outColor = vec4(color.rgb / (1.0 + luminance), color.a);
}
)frag";

vk_gltf_viewer::vulkan::pipelines::Rec709Renderer::DescriptorSetLayouts::DescriptorSetLayouts(
    const vk::raii::Device &device
) : vku::DescriptorSetLayouts<1> {
        device,
        LayoutBindings {
            {},
            vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eInputAttachment, 1, vk::ShaderStageFlagBits::eFragment },
        },
    } { }

vk_gltf_viewer::vulkan::pipelines::Rec709Renderer::Rec709Renderer(
    const vk::raii::Device &device,
    vk::RenderPass renderPass,
    std::uint32_t subpass,
    const shaderc::Compiler &compiler
) : descriptorSetLayouts { device },
    pipelineLayout { createPipelineLayout(device) },
    pipeline { createPipeline(device, renderPass, subpass, compiler) } { }

auto vk_gltf_viewer::vulkan::pipelines::Rec709Renderer::draw(
    vk::CommandBuffer commandBuffer,
    const DescriptorSets &descriptorSets
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, descriptorSets, {});
    commandBuffer.draw(3, 1, 0, 0);
}

auto vk_gltf_viewer::vulkan::pipelines::Rec709Renderer::createPipelineLayout(
    const vk::raii::Device &device
) const -> decltype(pipelineLayout) {
    return { device, vk::PipelineLayoutCreateInfo{
        {},
        descriptorSetLayouts,
    } };
}

auto vk_gltf_viewer::vulkan::pipelines::Rec709Renderer::createPipeline(
    const vk::raii::Device &device,
    vk::RenderPass renderPass,
    std::uint32_t subpass,
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
        1.f,
    };

    return {
        device,
        nullptr,
        vku::getDefaultGraphicsPipelineCreateInfo(stages, *pipelineLayout, 1)
            .setPRasterizationState(&rasterizationState)
            .setRenderPass(renderPass)
            .setSubpass(subpass)
    };
}