module;

#include <string_view>

#include <shaderc/shaderc.hpp>

module vk_gltf_viewer;
import :vulkan.pipelines.TriangleRenderer;

import vku;

// language=vert
std::string_view vk_gltf_viewer::vulkan::TriangleRenderer::vert = R"vert(
#version 450

const vec2 vertices[] = vec2[3](
    vec2(-0.5, 0.5),
    vec2(0.5, 0.5),
    vec2(0.0, -0.5)
);
const vec3 colors[] = vec3[3](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0)
);

layout (location = 0) out vec3 fragColor;

void main(){
    gl_Position = vec4(vertices[gl_VertexIndex], 0.0, 1.0);
    fragColor = colors[gl_VertexIndex];
}
)vert";

// language=frag
std::string_view vk_gltf_viewer::vulkan::TriangleRenderer::frag = R"frag(
#version 450

layout (location = 0) in vec3 fragColor;

layout (location = 0) out vec4 outColor;

void main(){
    outColor = vec4(fragColor, 1.0);
}
)frag";

vk_gltf_viewer::vulkan::TriangleRenderer::TriangleRenderer(
    const vk::raii::Device &device,
    const shaderc::Compiler &compiler
) : pipelineLayout { device, vk::PipelineLayoutCreateInfo{} },
    pipeline { createPipeline(device, compiler) } { }

auto vk_gltf_viewer::vulkan::TriangleRenderer::draw(
    vk::CommandBuffer commandBuffer
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    commandBuffer.draw(3, 1, 0, 0);
}

auto vk_gltf_viewer::vulkan::TriangleRenderer::createPipeline(
    const vk::raii::Device &device,
    const shaderc::Compiler &compiler
) const -> decltype(pipeline) {
    const auto [_, stages] = vku::createStages(
        device,
        vku::Shader {
            compiler,
            vert,
            vk::ShaderStageFlagBits::eVertex
        },
        vku::Shader {
            compiler,
            frag,
            vk::ShaderStageFlagBits::eFragment
        });

    constexpr vk::Format colorAttachmentFormat = vk::Format::eB8G8R8A8Srgb;

    return { device, nullptr, vk::StructureChain {
        vku::getDefaultGraphicsPipelineCreateInfo(stages, *pipelineLayout, 1),
        vk::PipelineRenderingCreateInfo {
            {},
            colorAttachmentFormat,
        }
    }.get() };
}