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
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_8bit_storage : require

layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer FloatBufferAddress { float data[]; };

layout (location = 0) out vec3 fragPosition;
layout (location = 1) out vec3 fragNormal;

layout (push_constant, std430) uniform PushConstant {
    mat4 model;
    mat4 projectionView;
    FloatBufferAddress pPositionBuffer;
    FloatBufferAddress pNormalBuffer;
    uint8_t positionByteStride;
    uint8_t normalByteStride;
    uint8_t padding[14];
} pc;

// --------------------
// Functions.
// --------------------

vec3 composeVec3(readonly FloatBufferAddress address, uint floatStride, uint index){
    return vec3(address.data[floatStride * index], address.data[floatStride * index + 1U], address.data[floatStride * index + 2U]);
}

void main(){
    vec3 inPosition = composeVec3(pc.pPositionBuffer, uint(pc.positionByteStride) / 4, gl_VertexIndex);
    vec3 inNormal = composeVec3(pc.pNormalBuffer, uint(pc.normalByteStride) / 4, gl_VertexIndex);

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
    layout (offset = 160)
    vec3 viewPosition;
} pc;

layout (early_fragment_tests) in;

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
    vk::Buffer indexBuffer,
    vk::DeviceSize indexBufferOffset,
    vk::IndexType indexType,
    std::uint32_t drawCount,
    const PushConstant &pushConstant
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    commandBuffer.bindIndexBuffer(indexBuffer, indexBufferOffset, indexType);
    commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eAllGraphics, 0, pushConstant);
    commandBuffer.drawIndexed(drawCount, 1, 0, 0, 0);
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

    constexpr vk::PipelineDepthStencilStateCreateInfo depthStencilState {
        {},
        true, true, vk::CompareOp::eLess,
    };

    constexpr vk::Format colorAttachmentFormat = vk::Format::eR16G16B16A16Sfloat;

    return { device, nullptr, vk::StructureChain {
        vku::getDefaultGraphicsPipelineCreateInfo(stages, *pipelineLayout, 1, true, vk::SampleCountFlagBits::e4)
            .setPDepthStencilState(&depthStencilState),
        vk::PipelineRenderingCreateInfo {
            {},
            colorAttachmentFormat,
            vk::Format::eD32Sfloat,
        }
    }.get() };
}