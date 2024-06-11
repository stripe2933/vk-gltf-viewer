module;

#include <compare>
#include <format>
#include <span>
#include <string_view>

#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.pipelines.MeshRenderer;

import vku;

// language=vert
std::string_view vk_gltf_viewer::vulkan::pipelines::MeshRenderer::vert = R"vert(
#version 450
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_8bit_storage : require

// For convinience.
#define TRANSFORM nodeTransforms[pc.nodeIndex]
#define MATERIAL materials[pc.materialIndex]

layout (std430, buffer_reference, buffer_reference_align = 1) readonly buffer Ubytes { uint8_t data[]; };
layout (std430, buffer_reference, buffer_reference_align = 4) readonly buffer Floats { float data[]; };
layout (std430, buffer_reference, buffer_reference_align = 8) readonly buffer FloatReferences { Floats references[]; };

struct NodeTransform {
    mat4 matrix;
    mat4 inverseMatrix;
};

struct Material {
    uint8_t baseColorTexcoordIndex;
    uint8_t metallicRoughnessTexcoordIndex;
    uint8_t normalTexcoordIndex;
    uint8_t occlusionTexcoordIndex;
    uint8_t padding0[4];
    int16_t baseColorTextureIndex;
    int16_t metallicRoughnessTextureIndex;
    int16_t normalTextureIndex;
    int16_t occlusionTextureIndex;
    uint8_t FRAGMENT_DATA[48];
};

layout (location = 0) out vec3 fragPosition;
layout (location = 1) out mat3 fragTBN;
layout (location = 4) out vec2 fragBaseColorTexcoord;
layout (location = 5) out vec2 fragMetallicRoughnessTexcoord;
layout (location = 6) out vec2 fragNormalTexcoord;
layout (location = 7) out vec2 fragOcclusionTexcoord;

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
    Floats positions;
    Floats normals;
    Floats tangents;
    FloatReferences texcoords;
    FloatReferences colors;
    uint8_t positionFloatStride;
    uint8_t normalFloatStride;
    uint8_t tangentFloatStride;
    uint8_t padding[5];
    Ubytes texcoordFloatStrides;
    Ubytes colorFloatStrides;
    uint nodeIndex;
    uint materialIndex;
} pc;

// --------------------
// Functions.
// --------------------

vec2 composeVec2(readonly Floats floats, uint floatStride, uint index){
    return vec2(floats.data[floatStride * index], floats.data[floatStride * index + 1U]);
}

vec3 composeVec3(readonly Floats floats, uint floatStride, uint index){
    return vec3(floats.data[floatStride * index], floats.data[floatStride * index + 1U], floats.data[floatStride * index + 2U]);
}

vec4 composeVec4(readonly Floats floats, uint floatStride, uint index){
    return vec4(floats.data[floatStride * index], floats.data[floatStride * index + 1U], floats.data[floatStride * index + 2U], floats.data[floatStride * index + 3U]);
}

void main(){
    vec3 inPosition = composeVec3(pc.positions, uint(pc.positionFloatStride), gl_VertexIndex);
    vec3 inNormal = composeVec3(pc.normals, uint(pc.normalFloatStride), gl_VertexIndex);

    fragPosition = (TRANSFORM.matrix * vec4(inPosition, 1.0)).xyz;
    fragTBN[2] = normalize(mat3(TRANSFORM.matrix) * inNormal); // N

    if (int(MATERIAL.baseColorTextureIndex) != -1){
        uint texcoordIndex = uint(MATERIAL.baseColorTexcoordIndex);
        fragBaseColorTexcoord = composeVec2(pc.texcoords.references[texcoordIndex], uint(pc.texcoordFloatStrides.data[texcoordIndex]), gl_VertexIndex);
    }
    if (int(MATERIAL.metallicRoughnessTextureIndex) != -1){
        uint texcoordIndex = uint(MATERIAL.metallicRoughnessTexcoordIndex);
        fragMetallicRoughnessTexcoord = composeVec2(pc.texcoords.references[texcoordIndex], uint(pc.texcoordFloatStrides.data[texcoordIndex]), gl_VertexIndex);
    }
    if (int(MATERIAL.normalTextureIndex) != -1){
        vec4 inTangent = composeVec4(pc.tangents, uint(pc.tangentFloatStride), gl_VertexIndex);
        fragTBN[0] = normalize(mat3(TRANSFORM.matrix) * inTangent.xyz); // T
        fragTBN[1] = cross(fragTBN[2], fragTBN[0]) * inTangent.w; // B

        uint texcoordIndex = uint(MATERIAL.normalTexcoordIndex);
        fragNormalTexcoord = composeVec2(pc.texcoords.references[texcoordIndex], uint(pc.texcoordFloatStrides.data[texcoordIndex]), gl_VertexIndex);
    }
    if (int(MATERIAL.occlusionTextureIndex) != -1){
        uint texcoordIndex = uint(MATERIAL.occlusionTexcoordIndex);
        fragOcclusionTexcoord = composeVec2(pc.texcoords.references[texcoordIndex], uint(pc.texcoordFloatStrides.data[texcoordIndex]), gl_VertexIndex);
    }

    gl_Position = camera.projectionView * vec4(fragPosition, 1.0);
}
)vert";

// language=frag
std::string_view vk_gltf_viewer::vulkan::pipelines::MeshRenderer::frag = R"frag(
#version 450
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_8bit_storage : require
#extension GL_EXT_scalar_block_layout : require

// For convinience.
#define MATERIAL materials[pc.materialIndex]

const vec3 lightColor = vec3(1.0);

struct SphericalHarmonicBasis{
    float band0[1];
    float band1[3];
    float band2[5];
};

struct Material {
    uint8_t VERTEX_DATA[8];
    int16_t baseColorTextureIndex;
    int16_t metallicRoughnessTextureIndex;
    int16_t normalTextureIndex;
    int16_t occlusionTextureIndex;
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    uint8_t padding1[16];
};

layout (location = 0) in vec3 fragPosition;
layout (location = 1) in mat3 fragTBN;
layout (location = 4) in vec2 fragBaseColorTexcoord;
layout (location = 5) in vec2 fragMetallicRoughnessTexcoord;
layout (location = 6) in vec2 fragNormalTexcoord;
layout (location = 7) in vec2 fragOcclusionTexcoord;

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform CameraBuffer {
    mat4 projectionView;
    vec3 viewPosition;
} camera;
layout (set = 0, binding = 1, scalar) uniform SphericalHarmonicsBuffer {
    vec3 coefficients[9];
} sphericalHarmonics;
layout (set = 0, binding = 2) uniform samplerCube prefilteredmap;
layout (set = 0, binding = 3) uniform sampler2D brdfmap;

layout (set = 1, binding = 0) uniform sampler2D textures[];
layout (set = 1, binding = 1) readonly buffer MaterialBuffer {
    Material materials[];
};

layout (push_constant, std430) uniform PushConstant {
    layout (offset = 64) // VERTEX_DATA
    uint nodeIndex;
    uint materialIndex;
} pc;

layout (early_fragment_tests) in;

// --------------------
// Functions.
// --------------------

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness){
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

SphericalHarmonicBasis getSphericalHarmonicBasis(vec3 v){
    return SphericalHarmonicBasis(
        float[1](0.282095),
        float[3](-0.488603 * v.y, 0.488603 * v.z, -0.488603 * v.x),
        float[5](1.092548 * v.x * v.y, -1.092548 * v.y * v.z, 0.315392 * (3.0 * v.z * v.z - 1.0), -1.092548 * v.x * v.z, 0.546274 * (v.x * v.x - v.y * v.y))
    );
}

vec3 computeDiffuseIrradiance(vec3 normal){
    SphericalHarmonicBasis basis = getSphericalHarmonicBasis(normal);
    vec3 irradiance
        = 3.141593 * (sphericalHarmonics.coefficients[0] * basis.band0[0])
        + 2.094395 * (sphericalHarmonics.coefficients[1] * basis.band1[0]
                   +  sphericalHarmonics.coefficients[2] * basis.band1[1]
                   +  sphericalHarmonics.coefficients[3] * basis.band1[2])
        + 0.785398 * (sphericalHarmonics.coefficients[4] * basis.band2[0]
                   +  sphericalHarmonics.coefficients[5] * basis.band2[1]
                   +  sphericalHarmonics.coefficients[6] * basis.band2[2]
                   +  sphericalHarmonics.coefficients[7] * basis.band2[3]
                   +  sphericalHarmonics.coefficients[8] * basis.band2[4]);
    return irradiance / 3.141593;
}

void main(){
    vec4 baseColor = MATERIAL.baseColorFactor;
    if (int(MATERIAL.baseColorTextureIndex) != -1) {
        baseColor *= texture(textures[uint(MATERIAL.baseColorTextureIndex)], fragBaseColorTexcoord);
    }

    float metallic = MATERIAL.metallicFactor, roughness = MATERIAL.roughnessFactor;
    if (int(MATERIAL.metallicRoughnessTextureIndex) != -1){
        vec2 metallicRoughness = texture(textures[uint(MATERIAL.metallicRoughnessTextureIndex)], fragMetallicRoughnessTexcoord).bg;
        metallic *= metallicRoughness.x;
        roughness *= metallicRoughness.y;
    }

    vec3 N;
    if (int(MATERIAL.normalTextureIndex) != -1){
        N = (2.0 * texture(textures[uint(MATERIAL.normalTextureIndex)], fragNormalTexcoord).xyz - 1.0) * vec3(MATERIAL.normalScale, MATERIAL.normalScale, 1.0);
        N = normalize(fragTBN * N);
    }
    else {
        N = normalize(fragTBN[2]);
    }

    float occlusion = 1.0;
    if (int(MATERIAL.occlusionTextureIndex) != -1){
        occlusion += MATERIAL.occlusionStrength * (texture(textures[uint(MATERIAL.occlusionTextureIndex)], fragOcclusionTexcoord).r - 1.0);
    }

    vec3 V = normalize(camera.viewPosition - fragPosition);
    vec3 R = reflect(-V, N);

    vec3 F0 = mix(vec3(0.04), baseColor.rgb, metallic);
    vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);

    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    vec3 irradiance = computeDiffuseIrradiance(N);
    vec3 diffuse    = irradiance * baseColor.rgb;

    uint prefilteredmapMipLevels = textureQueryLevels(prefilteredmap);
    vec3 prefilteredColor = textureLod(prefilteredmap, R, roughness * (prefilteredmapMipLevels - 1U)).rgb;
    vec2 brdf  = texture(brdfmap, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

    vec3 color = (kD * diffuse + specular) * occlusion;
    outColor = vec4(color, 1.0);
}
)frag";

vk_gltf_viewer::vulkan::pipelines::MeshRenderer::DescriptorSetLayouts::DescriptorSetLayouts(
    const vk::raii::Device &device,
    const vk::Sampler &sampler,
    std::uint32_t textureCount
) : vku::DescriptorSetLayouts<4, 2, 1> {
        device,
        LayoutBindings {
            {},
            vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eAllGraphics },
            vk::DescriptorSetLayoutBinding { 1, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eFragment },
            vk::DescriptorSetLayoutBinding { 2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, &sampler },
            vk::DescriptorSetLayoutBinding { 3, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, &sampler },
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

vk_gltf_viewer::vulkan::pipelines::MeshRenderer::MeshRenderer(
    const vk::raii::Device &device,
    std::uint32_t textureCount,
    const shaderc::Compiler &compiler
) : sampler { createSampler(device) },
    descriptorSetLayouts { device, *sampler, textureCount },
    pipelineLayout { createPipelineLayout(device) },
    pipeline { createPipeline(device, compiler) } { }

auto vk_gltf_viewer::vulkan::pipelines::MeshRenderer::bindPipeline(
    vk::CommandBuffer commandBuffer
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);

}

auto vk_gltf_viewer::vulkan::pipelines::MeshRenderer::bindDescriptorSets(
    vk::CommandBuffer commandBuffer,
    const DescriptorSets &descriptorSets,
    std::uint32_t firstSet
) const -> void {
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, *pipelineLayout,
        firstSet, std::span { descriptorSets }.subspan(firstSet), {});
}

auto vk_gltf_viewer::vulkan::pipelines::MeshRenderer::pushConstants(
    vk::CommandBuffer commandBuffer,
    const PushConstant &pushConstant
) const -> void {
    commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eAllGraphics, 0, pushConstant);
}

auto vk_gltf_viewer::vulkan::pipelines::MeshRenderer::createSampler(
    const vk::raii::Device &device
) const -> decltype(sampler) {
    return { device, vk::SamplerCreateInfo {
        {},
        vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
        {}, {}, {},
        {},
        vk::True, 16.f,
        {}, {},
        {}, vk::LodClampNone,
    } };
}

auto vk_gltf_viewer::vulkan::pipelines::MeshRenderer::createPipelineLayout(
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

auto vk_gltf_viewer::vulkan::pipelines::MeshRenderer::createPipeline(
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