module;

#include <array>
#include <compare>
#include <format>
#include <string_view>

#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.pipelines.DepthRenderer;

import vku;

// language=vert
std::string_view vk_gltf_viewer::vulkan::pipelines::DepthRenderer::vert = R"vert(
#version 460
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_8bit_storage : require

// For convinience.
#define PRIMITIVE primitives[gl_BaseInstance]
#define TRANSFORM nodeTransforms[PRIMITIVE.nodeIndex]

layout (std430, buffer_reference, buffer_reference_align = 1) readonly buffer Ubytes { uint8_t data[]; };
layout (std430, buffer_reference, buffer_reference_align = 8) readonly buffer Vec2Ref { vec2 data; };
layout (std430, buffer_reference, buffer_reference_align = 16) readonly buffer Vec4Ref { vec4 data; };
layout (std430, buffer_reference, buffer_reference_align = 8) readonly buffer Pointers { uint64_t data[]; };

struct Primitive {
    uint64_t pPositionBuffer;
    uint64_t pNormalBuffer;
    uint64_t pTangentBuffer;
    Pointers texcoordBufferPtrs;
    Pointers colorBufferPtrs;
    uint8_t positionByteStride;
    uint8_t normalByteStride;
    uint8_t tangentByteStride;
    uint8_t padding[5];
    Ubytes texcoordByteStrides;
    Ubytes colorByteStrides;
    uint nodeIndex;
    uint materialIndex;
};

layout (location = 0) flat out uint primitiveNodeIndex;

layout (set = 0, binding = 0) readonly buffer PrimitiveBuffer {
    Primitive primitives[];
};
layout (set = 0, binding = 1) readonly buffer NodeTransformBuffer {
    mat4 nodeTransforms[];
};

layout (push_constant, std430) uniform PushConstant {
    mat4 projectionView;
} pc;

// --------------------
// Functions.
// --------------------

vec3 getVec3(uint64_t address){
    return Vec4Ref(address).data.xyz;
}

void main(){
    primitiveNodeIndex = PRIMITIVE.nodeIndex;

    vec3 inPosition = getVec3(PRIMITIVE.pPositionBuffer + uint(PRIMITIVE.positionByteStride) * gl_VertexIndex);
    gl_Position = pc.projectionView * TRANSFORM * vec4(inPosition, 1.0);
}
)vert";

// language=frag
std::string_view vk_gltf_viewer::vulkan::pipelines::DepthRenderer::frag = R"frag(
#version 450

layout (location = 0) flat in uint primitiveNodeIndex;

layout (location = 0) out uint outNodeIndex;
layout (location = 1) out uvec4 jumpFloodCoord;

layout (push_constant, std430) uniform PushConstant {
    layout (offset = 64)
    uint hoveringNodeIndex;
    uint selectedNodeIndex;
} pc;

layout (early_fragment_tests) in;

void main(){
    outNodeIndex = primitiveNodeIndex;
    if (outNodeIndex == pc.hoveringNodeIndex){
        jumpFloodCoord.xy = uvec2(gl_FragCoord.xy);
    }
    if (outNodeIndex == pc.selectedNodeIndex){
        jumpFloodCoord.zw = uvec2(gl_FragCoord.xy);
    }
}
)frag";

vk_gltf_viewer::vulkan::pipelines::DepthRenderer::DescriptorSetLayouts::DescriptorSetLayouts(
    const vk::raii::Device &device
) : vku::DescriptorSetLayouts<2> {
        device,
        vk::DescriptorSetLayoutCreateInfo {
            {},
            vku::unsafeProxy({
                vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex },
                vk::DescriptorSetLayoutBinding { 1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex },
            }),
        },
    } { }

auto vk_gltf_viewer::vulkan::pipelines::DepthRenderer::DescriptorSets::getDescriptorWrites0(
    const vk::DescriptorBufferInfo &primitiveBufferInfo [[clang::lifetimebound]],
    const vk::DescriptorBufferInfo &nodeTransformBufferInfo [[clang::lifetimebound]]
) const -> std::array<vk::WriteDescriptorSet, 2> {
    return std::array {
        getDescriptorWrite<0, 0>().setBufferInfo(primitiveBufferInfo),
        getDescriptorWrite<0, 1>().setBufferInfo(nodeTransformBufferInfo),
    };
}

vk_gltf_viewer::vulkan::pipelines::DepthRenderer::DepthRenderer(
    const vk::raii::Device &device,
    const shaderc::Compiler &compiler
) : descriptorSetLayouts { device },
    pipelineLayout { createPipelineLayout(device) },
    pipeline { createPipeline(device, compiler) } { }

auto vk_gltf_viewer::vulkan::pipelines::DepthRenderer::bindPipeline(
    vk::CommandBuffer commandBuffer
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
}

auto vk_gltf_viewer::vulkan::pipelines::DepthRenderer::bindDescriptorSets(
    vk::CommandBuffer commandBuffer,
    const DescriptorSets &descriptorSets,
    std::uint32_t firstSet
) const -> void {
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, *pipelineLayout, firstSet, descriptorSets, {});
}

auto vk_gltf_viewer::vulkan::pipelines::DepthRenderer::pushConstants(
    vk::CommandBuffer commandBuffer,
    const PushConstant &pushConstant
) const -> void {
    commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eAllGraphics, 0, pushConstant);
}

auto vk_gltf_viewer::vulkan::pipelines::DepthRenderer::createPipelineLayout(
    const vk::raii::Device &device
) const -> decltype(pipelineLayout) {
    return { device, vk::PipelineLayoutCreateInfo{
        {},
        vku::unsafeProxy(descriptorSetLayouts.getHandles()),
        vku::unsafeProxy({
            vk::PushConstantRange {
                vk::ShaderStageFlagBits::eAllGraphics,
                0, sizeof(PushConstant),
            },
        }),
    } };
}

auto vk_gltf_viewer::vulkan::pipelines::DepthRenderer::createPipeline(
    const vk::raii::Device &device,
    const shaderc::Compiler &compiler
) const -> decltype(pipeline) {
    const auto [_, stages] = createStages(
        device,
        vku::Shader { compiler, vert, vk::ShaderStageFlagBits::eVertex },
        vku::Shader { compiler, frag, vk::ShaderStageFlagBits::eFragment });

    return { device, nullptr, vk::StructureChain {
        vku::getDefaultGraphicsPipelineCreateInfo(stages, *pipelineLayout, 2, true)
            .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                {},
                true, true, vk::CompareOp::eGreater, // Use reverse Z.
            }))
            .setPDynamicState(vku::unsafeAddress(vk::PipelineDynamicStateCreateInfo {
                {},
                vku::unsafeProxy({
                    vk::DynamicState::eViewport,
                    vk::DynamicState::eScissor,
                    vk::DynamicState::eCullMode,
                }),
            })),
        vk::PipelineRenderingCreateInfo {
            {},
            vku::unsafeProxy({ vk::Format::eR32Uint, vk::Format::eR16G16B16A16Uint }),
            vk::Format::eD32Sfloat,
        }
    }.get() };
}