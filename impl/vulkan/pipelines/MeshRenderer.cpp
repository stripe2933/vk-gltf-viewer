module;

#include <format>
#include <string_view>

#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.pipelines.MeshRenderer;

import vku;

// language=vert
std::string_view vk_gltf_viewer::vulkan::MeshRenderer::vert = R"vert(
#version 450

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inNormal;

layout (location = 0) out vec3 fragPosition;
layout (location = 1) out vec3 fragNormal;

layout (push_constant) uniform PushConstant {
    mat4 model;
    mat4 projectionView;
    vec3 viewPosition;
} pc;

void main(){
    fragPosition = (pc.model * vec4(inPosition, 1.0)).xyz;
    fragNormal = transpose(inverse(mat3(pc.model))) * inNormal;
    gl_Position = pc.projectionView * vec4(fragPosition, 1.0);
}
)vert";

// language=frag
std::string_view vk_gltf_viewer::vulkan::MeshRenderer::frag = R"frag(
#version 450

const vec3 lightColor = vec3(1.0);

layout (location = 0) in vec3 fragPosition;
layout (location = 1) in vec3 fragNormal;

layout (location = 0) out vec4 outColor;

layout (push_constant) uniform PushConstant {
    mat4 model;
    mat4 projectionView;
    vec3 viewPosition;
} pc;

void main(){
    vec3 normal = normalize(fragNormal);
    vec3 lightDir = normalize(pc.viewPosition - fragPosition);
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;
    outColor = vec4(diffuse, 1.0);
}
)frag";

vk_gltf_viewer::vulkan::MeshRenderer::MeshRenderer(
    const vk::raii::Device &device,
    const shaderc::Compiler &compiler
) : pipelineLayout { createPipelineLayout(device) },
    pipeline { createPipeline(device, compiler) } { }

auto vk_gltf_viewer::vulkan::MeshRenderer::draw(
    vk::CommandBuffer commandBuffer,
    const vku::Buffer &indexBuffer,
    const vku::Buffer &vertexBuffer,
    const PushConstant &pushConstant
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    commandBuffer.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint32);
    commandBuffer.bindVertexBuffers(0, vertexBuffer.buffer, { 0 });
    commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eAllGraphics, 0, pushConstant);
    commandBuffer.drawIndexed(indexBuffer.size / sizeof(std::uint32_t), 1, 0, 0, 0);
}

auto vk_gltf_viewer::vulkan::MeshRenderer::createPipelineLayout(
    const vk::raii::Device &device
) const -> decltype(pipelineLayout) {
    constexpr vk::PushConstantRange pushConstantRange {
        vk::ShaderStageFlagBits::eAllGraphics,
        0, sizeof(PushConstant),
    };
    return { device, vk::PipelineLayoutCreateInfo{
        {},
        {},
        pushConstantRange,
    } };
}

auto vk_gltf_viewer::vulkan::MeshRenderer::createPipeline(
    const vk::raii::Device &device,
    const shaderc::Compiler &compiler
) const -> decltype(pipeline) {
    const auto [_, stages] = createStages(
        device,
        vku::Shader { compiler, vert, vk::ShaderStageFlagBits::eVertex },
        vku::Shader { compiler, frag, vk::ShaderStageFlagBits::eFragment });

    constexpr vk::VertexInputBindingDescription bindingDescription {
        0,
        sizeof(glm::vec3) * 2,
        vk::VertexInputRate::eVertex
    };
    constexpr std::array attributeDescriptions {
        vk::VertexInputAttributeDescription {
            0,
            0,
            vk::Format::eR32G32B32Sfloat,
            0,
        },
        vk::VertexInputAttributeDescription {
            1,
            0,
            vk::Format::eR32G32B32Sfloat,
            sizeof(glm::vec3),
        }
    };
    const vk::PipelineVertexInputStateCreateInfo vertexInputState {
        {},
        bindingDescription,
        attributeDescriptions,
    };

    constexpr vk::Format colorAttachmentFormat = vk::Format::eB8G8R8A8Srgb;

    return { device, nullptr, vk::StructureChain {
        vku::getDefaultGraphicsPipelineCreateInfo(stages, *pipelineLayout, 1)
            .setPVertexInputState(&vertexInputState),
        vk::PipelineRenderingCreateInfo {
            {},
            colorAttachmentFormat,
        }
    }.get() };
}