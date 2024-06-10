module;

#include <format>
#include <span>
#include <string_view>

#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.pipelines.MeshRenderer;

import vku;

// language=vert
std::string_view vk_gltf_viewer::vulkan::MeshRenderer::vert = R"vert(
#version 450
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_8bit_storage : require

// For convinience.
#define TRANSFORM nodeTransforms[pc.nodeIndex]
#define MATERIAL materials[pc.materialIndex]

layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer FloatBufferAddress { float data[]; };

struct NodeTransform {
    mat4 matrix;
    mat4 inverseMatrix;
};

struct Material {
    FloatBufferAddress pBaseColorTexcoordBuffer;
    FloatBufferAddress pMetallicRoughnessTexcoordBuffer;
    FloatBufferAddress pNormalTexcoordBuffer;
    FloatBufferAddress pOcclusionTexcoordBuffer;
    uint8_t baseColorTexcoordFloatStride;
    uint8_t metallicRoughnessTexcoordFloatStride;
    uint8_t normalTexcoordFloatStride;
    uint8_t occlusionTexcoordFloatStride;
    uint8_t padding0[12];
    int16_t baseColorTextureIndex;
    int16_t metallicRoughnessTextureIndex;
    int16_t normalTextureIndex;
    int16_t occlusionTextureIndex;
    uint8_t FRAGMENT_DATA[72];
};

layout (location = 0) out vec3 fragPosition;
layout (location = 1) out vec3 fragNormal;
layout (location = 2) out vec2 fragBaseColorTexcoord;
layout (location = 3) out vec2 fragMetallicRoughnessTexcoord;
layout (location = 4) out vec2 fragNormalTexcoord;
layout (location = 5) out vec2 fragOcclusionTexcoord;

layout (set = 0, binding = 0) uniform CameraBuffer {
    mat4 projectionView;
    vec3 viewPosition;
} camera;

layout (set = 1, binding = 1) readonly buffer MaterialBuffer {
    Material materials[];
};

layout (set = 2, binding = 0) readonly buffer NodeTransformBuffer {
    NodeTransform nodeTransforms[];
};

layout (push_constant, std430) uniform PushConstant {
    FloatBufferAddress pPositionBuffer;
    FloatBufferAddress pNormalBuffer;
    uint8_t positionFloatStride;
    uint8_t normalFloatStride;
    uint8_t padding[14];
    uint nodeIndex;
    uint materialIndex;
} pc;

// --------------------
// Functions.
// --------------------

vec2 composeVec2(readonly FloatBufferAddress address, uint floatStride, uint index){
    return vec2(address.data[floatStride * index], address.data[floatStride * index + 1U]);
}

vec3 composeVec3(readonly FloatBufferAddress address, uint floatStride, uint index){
    return vec3(address.data[floatStride * index], address.data[floatStride * index + 1U], address.data[floatStride * index + 2U]);
}

void main(){
    vec3 inPosition = composeVec3(pc.pPositionBuffer, uint(pc.positionFloatStride), gl_VertexIndex);
    vec3 inNormal = composeVec3(pc.pNormalBuffer, uint(pc.normalFloatStride), gl_VertexIndex);

    fragPosition = (TRANSFORM.matrix * vec4(inPosition, 1.0)).xyz;
    fragNormal = transpose(mat3(TRANSFORM.inverseMatrix)) * inNormal;

    if (int(MATERIAL.baseColorTextureIndex) != -1){
        fragBaseColorTexcoord = composeVec2(MATERIAL.pBaseColorTexcoordBuffer, uint(MATERIAL.baseColorTexcoordFloatStride), gl_VertexIndex);
    }
    if (int(MATERIAL.metallicRoughnessTextureIndex) != -1){
        fragMetallicRoughnessTexcoord = composeVec2(MATERIAL.pMetallicRoughnessTexcoordBuffer, uint(MATERIAL.metallicRoughnessTexcoordFloatStride), gl_VertexIndex);
    }
    if (int(MATERIAL.normalTextureIndex) != -1){
        fragNormalTexcoord = composeVec2(MATERIAL.pNormalTexcoordBuffer, uint(MATERIAL.normalTexcoordFloatStride), gl_VertexIndex);
    }
    if (int(MATERIAL.occlusionTextureIndex) != -1){
        fragOcclusionTexcoord = composeVec2(MATERIAL.pOcclusionTexcoordBuffer, uint(MATERIAL.occlusionTexcoordFloatStride), gl_VertexIndex);
    }

    gl_Position = camera.projectionView * vec4(fragPosition, 1.0);
}
)vert";

// language=frag
std::string_view vk_gltf_viewer::vulkan::MeshRenderer::frag = R"frag(
#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_8bit_storage : require

// For convinience.
#define MATERIAL materials[pc.materialIndex]

const vec3 lightColor = vec3(1.0);

struct Material {
    uint8_t VERTEX_DATA[48];
    int16_t baseColorTextureIndex;
    int16_t metallicRoughnessTextureIndex;
    int16_t normalTextureIndex;
    int16_t occlusionTextureIndex;
    uint8_t padding1[8];
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    uint8_t padding2[32];
};

layout (location = 0) in vec3 fragPosition;
layout (location = 1) in vec3 fragNormal;
layout (location = 2) in vec2 fragBaseColorTexcoord;
layout (location = 3) in vec2 fragMetallicRoughnessTexcoord;
layout (location = 4) in vec2 fragNormalTexcoord;
layout (location = 5) in vec2 fragOcclusionTexcoord;

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform CameraBuffer {
    mat4 projectionView;
    vec3 viewPosition;
} camera;

layout (set = 1, binding = 0) uniform sampler2D textures[];
layout (set = 1, binding = 1) readonly buffer MaterialBuffer {
    Material materials[];
};

layout (push_constant, std430) uniform PushConstant {
    layout (offset = 32) // VERTEX_DATA
    uint nodeIndex;
    uint materialIndex;
} pc;

layout (early_fragment_tests) in;

void main(){
    vec4 baseColor = MATERIAL.baseColorFactor;
    if (int(MATERIAL.baseColorTextureIndex) != -1){
        baseColor *= texture(textures[uint(MATERIAL.baseColorTextureIndex)], fragBaseColorTexcoord);
    }

    float metallic = MATERIAL.metallicFactor, roughness = MATERIAL.roughnessFactor;
    if (int(MATERIAL.metallicRoughnessTextureIndex) != -1){
        vec4 metallicRoughness = texture(textures[uint(MATERIAL.metallicRoughnessTextureIndex)], fragMetallicRoughnessTexcoord);
        metallic *= metallicRoughness.b;
        roughness *= metallicRoughness.g;
    }

    float occlusion = 1.0;
    if (int(MATERIAL.occlusionTextureIndex) != -1){
        occlusion += MATERIAL.occlusionStrength * (texture(textures[uint(MATERIAL.occlusionTextureIndex)], fragOcclusionTexcoord).r - 1.0);
    }

    outColor = baseColor;
}
)frag";

vk_gltf_viewer::vulkan::MeshRenderer::DescriptorSetLayouts::DescriptorSetLayouts(
    const vk::raii::Device &device,
    std::uint32_t textureCount
) : vku::DescriptorSetLayouts<1, 2, 1> {
        device,
        LayoutBindings {
            {},
            vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eAllGraphics },
        },
        LayoutBindings {
            vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
            vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eCombinedImageSampler, textureCount, vk::ShaderStageFlagBits::eFragment },
            vk::DescriptorSetLayoutBinding { 1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAllGraphics },
            std::array { vku::toFlags(vk::DescriptorBindingFlagBits::eUpdateAfterBind), vk::DescriptorBindingFlags{} },
        },
        LayoutBindings {
            {},
            vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex },
        },
    } { }

vk_gltf_viewer::vulkan::MeshRenderer::MeshRenderer(
    const vk::raii::Device &device,
    std::uint32_t textureCount,
    const shaderc::Compiler &compiler
) : descriptorSetLayouts { device, textureCount },
    pipelineLayout { createPipelineLayout(device) },
    pipeline { createPipeline(device, compiler) } { }

auto vk_gltf_viewer::vulkan::MeshRenderer::bindPipeline(
    vk::CommandBuffer commandBuffer
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);

}

auto vk_gltf_viewer::vulkan::MeshRenderer::bindDescriptorSets(
    vk::CommandBuffer commandBuffer,
    const DescriptorSets &descriptorSets,
    std::uint32_t firstSet
) const -> void {
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, *pipelineLayout,
        firstSet, std::span { descriptorSets }.subspan(firstSet), {});
}

auto vk_gltf_viewer::vulkan::MeshRenderer::pushConstants(
    vk::CommandBuffer commandBuffer,
    const PushConstant &pushConstant
) const -> void {
    commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eAllGraphics, 0, pushConstant);
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
        descriptorSetLayouts,
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