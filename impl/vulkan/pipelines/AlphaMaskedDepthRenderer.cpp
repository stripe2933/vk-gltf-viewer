module;

#include <array>
#include <compare>
#include <format>
#include <string_view>

#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.pipelines.AlphaMaskedDepthRenderer;

import vku;

// language=vert
std::string_view vk_gltf_viewer::vulkan::pipelines::AlphaMaskedDepthRenderer::vert = R"vert(
#version 460
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_8bit_storage : require

// For convinience.
#define PRIMITIVE primitives[gl_BaseInstance]
#define MATERIAL materials[PRIMITIVE.materialIndex]
#define TRANSFORM nodeTransforms[PRIMITIVE.nodeIndex]

layout (std430, buffer_reference, buffer_reference_align = 1) readonly buffer Ubytes { uint8_t data[]; };
layout (std430, buffer_reference, buffer_reference_align = 8) readonly buffer Vec2Ref { vec2 data; };
layout (std430, buffer_reference, buffer_reference_align = 16) readonly buffer Vec4Ref { vec4 data; };
layout (std430, buffer_reference, buffer_reference_align = 8) readonly buffer Pointers { uint64_t data[]; };

struct Material {
    uint8_t baseColorTexcoordIndex;
    uint8_t metallicRoughnessTexcoordIndex;
    uint8_t normalTexcoordIndex;
    uint8_t occlusionTexcoordIndex;
    uint8_t emissiveTexcoordIndex;
    uint8_t padding0[1];
    int16_t baseColorTextureIndex;
    int16_t metallicRoughnessTextureIndex;
    int16_t normalTextureIndex;
    int16_t occlusionTextureIndex;
    int16_t emissiveTextureIndex;
    vec4 baseColorFactor;
    uint8_t FRAGMENT_DATA[32];
};

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

layout (location = 0) out vec2 fragBaseColorTexcoord;
layout (location = 1) flat out uint primitiveNodeIndex;
layout (location = 2) out float baseColorAlphaFactor;
layout (location = 3) flat out int baseColorTextureIndex;

layout (set = 0, binding = 1) readonly buffer MaterialBuffer {
    Material materials[];
};

layout (set = 1, binding = 0) readonly buffer PrimitiveBuffer {
    Primitive primitives[];
};
layout (set = 1, binding = 1) readonly buffer NodeTransformBuffer {
    mat4 nodeTransforms[];
};

layout (push_constant, std430) uniform PushConstant {
    mat4 projectionView;
} pc;

// --------------------
// Functions.
// --------------------

vec2 getVec2(uint64_t address){
    return Vec2Ref(address).data;
}

vec3 getVec3(uint64_t address){
    return Vec4Ref(address).data.xyz;
}

vec2 getTexcoord(uint texcoordIndex){
    return getVec2(PRIMITIVE.texcoordBufferPtrs.data[texcoordIndex] + uint(PRIMITIVE.texcoordByteStrides.data[texcoordIndex]) * gl_VertexIndex);
}

void main(){
    if (int(MATERIAL.baseColorTextureIndex) != -1){
        fragBaseColorTexcoord = getTexcoord(uint(MATERIAL.baseColorTexcoordIndex));
    }
    primitiveNodeIndex = PRIMITIVE.nodeIndex;
    baseColorAlphaFactor = MATERIAL.baseColorFactor.a;
    baseColorTextureIndex = MATERIAL.baseColorTextureIndex;

    vec3 inPosition = getVec3(PRIMITIVE.pPositionBuffer + uint(PRIMITIVE.positionByteStride) * gl_VertexIndex);
    gl_Position = pc.projectionView * TRANSFORM * vec4(inPosition, 1.0);
}
)vert";

// language=frag
std::string_view vk_gltf_viewer::vulkan::pipelines::AlphaMaskedDepthRenderer::frag = R"frag(
#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout (location = 0) in vec2 fragBaseColorTexcoord;
layout (location = 1) flat in uint primitiveNodeIndex;
layout (location = 2) in float baseColorAlphaFactor;
layout (location = 3) flat in int baseColorTextureIndex;

layout (location = 0) out uint outNodeIndex;
layout (location = 1) out uvec4 jumpFloodCoord;

layout (set = 0, binding = 0) uniform sampler2D textures[];

layout (push_constant, std430) uniform PushConstant {
    layout (offset = 64)
    uint hoveringNodeIndex;
    uint selectedNodeIndex;
} pc;

void main(){
    float baseColorAlpha = baseColorAlphaFactor * texture(textures[baseColorTextureIndex + 1], fragBaseColorTexcoord).a;
    if (baseColorAlpha < 0.1) discard;

    outNodeIndex = primitiveNodeIndex;
    if (outNodeIndex == pc.hoveringNodeIndex){
        jumpFloodCoord.xy = uvec2(gl_FragCoord.xy);
    }
    if (outNodeIndex == pc.selectedNodeIndex){
        jumpFloodCoord.zw = uvec2(gl_FragCoord.xy);
    }
}
)frag";

vk_gltf_viewer::vulkan::pipelines::AlphaMaskedDepthRenderer::DescriptorSetLayouts::DescriptorSetLayouts(
    const vk::raii::Device &device,
    std::uint32_t textureCount
) : vku::DescriptorSetLayouts<2, 2> {
        device,
        vk::StructureChain {
            vk::DescriptorSetLayoutCreateInfo {
                vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
                vku::unsafeProxy({
                    vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eCombinedImageSampler, 1 + textureCount, vk::ShaderStageFlagBits::eFragment },
                    vk::DescriptorSetLayoutBinding { 1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex },
                }),
            },
            vk::DescriptorSetLayoutBindingFlagsCreateInfo {
                vku::unsafeProxy({
                    vk::Flags { vk::DescriptorBindingFlagBits::eUpdateAfterBind },
                    vk::DescriptorBindingFlags{},
                }),
            }
        }.get(),
        vk::DescriptorSetLayoutCreateInfo {
            {},
            vku::unsafeProxy({
                vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex },
                vk::DescriptorSetLayoutBinding { 1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex },
            }),
        },
    } { }

auto vk_gltf_viewer::vulkan::pipelines::AlphaMaskedDepthRenderer::DescriptorSets::getDescriptorWrites1(
    const vk::DescriptorBufferInfo &primitiveBufferInfo,
    const vk::DescriptorBufferInfo &nodeTransformBufferInfo
) const -> std::array<vk::WriteDescriptorSet, 2> {
    return {
        getDescriptorWrite<1, 0>().setBufferInfo(primitiveBufferInfo),
        getDescriptorWrite<1, 1>().setBufferInfo(nodeTransformBufferInfo),
    };
}

vk_gltf_viewer::vulkan::pipelines::AlphaMaskedDepthRenderer::AlphaMaskedDepthRenderer(
    const vk::raii::Device &device,
    std::uint32_t textureCount,
    const shaderc::Compiler &compiler
) : descriptorSetLayouts { device, textureCount },
    pipelineLayout { createPipelineLayout(device) },
    pipeline { createPipeline(device, compiler) } { }

auto vk_gltf_viewer::vulkan::pipelines::AlphaMaskedDepthRenderer::bindPipeline(
    vk::CommandBuffer commandBuffer
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
}

auto vk_gltf_viewer::vulkan::pipelines::AlphaMaskedDepthRenderer::bindDescriptorSets(
    vk::CommandBuffer commandBuffer,
    const DescriptorSets &descriptorSets,
    std::uint32_t firstSet
) const -> void {
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, *pipelineLayout, firstSet, descriptorSets, {});
}

auto vk_gltf_viewer::vulkan::pipelines::AlphaMaskedDepthRenderer::pushConstants(
    vk::CommandBuffer commandBuffer,
    const PushConstant &pushConstant
) const -> void {
    commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eAllGraphics, 0, pushConstant);
}

auto vk_gltf_viewer::vulkan::pipelines::AlphaMaskedDepthRenderer::createPipelineLayout(
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

auto vk_gltf_viewer::vulkan::pipelines::AlphaMaskedDepthRenderer::createPipeline(
    const vk::raii::Device &device,
    const shaderc::Compiler &compiler
) const -> decltype(pipeline) {
    return { device, nullptr, vk::StructureChain {
        vku::getDefaultGraphicsPipelineCreateInfo(
            vku::createPipelineStages(
                device,
                vku::Shader { compiler, vert, vk::ShaderStageFlagBits::eVertex },
                vku::Shader { compiler, frag, vk::ShaderStageFlagBits::eFragment }).get(),
            *pipelineLayout,
            2, true)
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