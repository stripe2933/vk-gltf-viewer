module;

#include <array>
#include <compare>
#include <format>
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

void main(){
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
)vert";

// language=frag
std::string_view vk_gltf_viewer::vulkan::pipelines::Rec709Renderer::frag = R"frag(
#version 450

const vec3 REC_709_LUMA = vec3(0.2126, 0.7152, 0.0722);

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0, rgba16f) uniform readonly image2D hdrImage;

void main(){
    vec4 color = imageLoad(hdrImage, ivec2(gl_FragCoord.xy));
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
            vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eFragment },
        },
    } { }

vk_gltf_viewer::vulkan::pipelines::Rec709Renderer::Rec709Renderer(
    const vk::raii::Device &device,
    const shaderc::Compiler &compiler
) : descriptorSetLayouts { device },
    pipelineLayout { createPipelineLayout(device) },
    pipeline { createPipeline(device, compiler) } { }

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

    constexpr std::array colorAttachmentFormats {
        vk::Format::eB8G8R8A8Srgb,
    };

    return { device, nullptr, vk::StructureChain {
        vku::getDefaultGraphicsPipelineCreateInfo(stages, *pipelineLayout, 1)
            .setPRasterizationState(&rasterizationState),
        vk::PipelineRenderingCreateInfo {
            {},
            colorAttachmentFormats,
        },
    }.get() };
}