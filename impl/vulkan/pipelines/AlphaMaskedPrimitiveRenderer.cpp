module;

#include <compare>
#include <format>
#include <string_view>

#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.pipelines.AlphaMaskedPrimitiveRenderer;

import vku;

// language=vert
std::string_view vk_gltf_viewer::vulkan::pipelines::AlphaMaskedPrimitiveRenderer::vert = R"vert(
#version 460
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_8bit_storage : require

// For convinience.
#define PRIMITIVE primitives[gl_InstanceIndex]
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
    uint8_t FRAGMENT_DATA[48];
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

layout (location = 0) out vec3 fragPosition;
layout (location = 1) out mat3 fragTBN;
layout (location = 4) out vec2 fragBaseColorTexcoord;
layout (location = 5) out vec2 fragMetallicRoughnessTexcoord;
layout (location = 6) out vec2 fragNormalTexcoord;
layout (location = 7) out vec2 fragOcclusionTexcoord;
layout (location = 8) out vec2 fragEmissiveTexcoord;
layout (location = 9) flat out uint baseInstance;

layout (set = 1, binding = 1) readonly buffer MaterialBuffer {
    Material materials[];
};

layout (set = 2, binding = 0) readonly buffer PrimitiveBuffer {
    Primitive primitives[];
};
layout (set = 2, binding = 1) readonly buffer NodeTransformBuffer {
    mat4 nodeTransforms[];
};

layout (push_constant, std430) uniform PushConstant {
    mat4 projectionView;
    vec3 viewPosition;
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

vec4 getVec4(uint64_t address){
    return Vec4Ref(address).data;
}

vec2 getTexcoord(uint texcoordIndex){
    return getVec2(PRIMITIVE.texcoordBufferPtrs.data[texcoordIndex] + uint(PRIMITIVE.texcoordByteStrides.data[texcoordIndex]) * gl_VertexIndex);
}

void main(){
    vec3 inPosition = getVec3(PRIMITIVE.pPositionBuffer + uint(PRIMITIVE.positionByteStride) * gl_VertexIndex);
    vec3 inNormal = getVec3(PRIMITIVE.pNormalBuffer + uint(PRIMITIVE.normalByteStride) * gl_VertexIndex);

    mat4 transform = TRANSFORM;
    fragPosition = (transform * vec4(inPosition, 1.0)).xyz;
    fragTBN[2] = normalize(mat3(transform) * inNormal); // N

    if (int(MATERIAL.baseColorTextureIndex) != -1){
        fragBaseColorTexcoord = getTexcoord(uint(MATERIAL.baseColorTexcoordIndex));
    }
    if (int(MATERIAL.metallicRoughnessTextureIndex) != -1){
        fragMetallicRoughnessTexcoord = getTexcoord(uint(MATERIAL.metallicRoughnessTexcoordIndex));
    }
    if (int(MATERIAL.normalTextureIndex) != -1){
        vec4 inTangent = getVec4(PRIMITIVE.pTangentBuffer + uint(PRIMITIVE.tangentByteStride) * gl_VertexIndex);
        fragTBN[0] = normalize(mat3(transform) * inTangent.xyz); // T
        fragTBN[1] = cross(fragTBN[2], fragTBN[0]) * -inTangent.w; // B

        fragNormalTexcoord = getTexcoord(uint(MATERIAL.normalTexcoordIndex));
    }
    if (int(MATERIAL.occlusionTextureIndex) != -1){
        fragOcclusionTexcoord = getTexcoord(uint(MATERIAL.occlusionTexcoordIndex));
    }
    if (int(MATERIAL.emissiveTextureIndex) != -1){
        fragEmissiveTexcoord = getTexcoord(uint(MATERIAL.emissiveTexcoordIndex));
    }
    baseInstance = gl_BaseInstance;

    gl_Position = pc.projectionView * vec4(fragPosition, 1.0);
}
)vert";

// language=frag
std::string_view vk_gltf_viewer::vulkan::pipelines::AlphaMaskedPrimitiveRenderer::frag = R"frag(
#version 450
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_8bit_storage : require
#extension GL_EXT_scalar_block_layout : require

// For convinience.
#define PRIMITIVE primitives[baseInstance]
#define MATERIAL materials[PRIMITIVE.materialIndex]

const vec3 REC_709_LUMA = vec3(0.2126, 0.7152, 0.0722);
const float ALPHA_CUTOFF = 0.5;

struct SphericalHarmonicBasis{
    float band0[1];
    float band1[3];
    float band2[5];
};

struct Material {
    uint8_t VERTEX_DATA[6];
    int16_t baseColorTextureIndex;
    int16_t metallicRoughnessTextureIndex;
    int16_t normalTextureIndex;
    int16_t occlusionTextureIndex;
    int16_t emissiveTextureIndex;
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    vec3 emissiveFactor;
    uint8_t padding[4];
};

struct Primitive {
    uint8_t VERTEX_DATA[64];
    uint nodeIndex;
    uint materialIndex;
};

layout (location = 0) in vec3 fragPosition;
layout (location = 1) in mat3 fragTBN;
layout (location = 4) in vec2 fragBaseColorTexcoord;
layout (location = 5) in vec2 fragMetallicRoughnessTexcoord;
layout (location = 6) in vec2 fragNormalTexcoord;
layout (location = 7) in vec2 fragOcclusionTexcoord;
layout (location = 8) in vec2 fragEmissiveTexcoord;
layout (location = 9) flat in uint baseInstance;

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0, scalar) uniform SphericalHarmonicsBuffer {
    vec3 coefficients[9];
} sphericalHarmonics;
layout (set = 0, binding = 1) uniform samplerCube prefilteredmap;
layout (set = 0, binding = 2) uniform sampler2D brdfmap;

layout (set = 1, binding = 0) uniform sampler2D textures[];
layout (set = 1, binding = 1) readonly buffer MaterialBuffer {
    Material materials[];
};

layout (set = 2, binding = 0) readonly buffer PrimitiveBuffer {
    Primitive primitives[];
};

layout (push_constant, std430) uniform PushConstant {
    mat4 projectionView;
    vec3 viewPosition;
} pc;

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
    vec4 baseColor = MATERIAL.baseColorFactor * texture(textures[int(MATERIAL.baseColorTextureIndex) + 1], fragBaseColorTexcoord);

    vec2 metallicRoughness = vec2(MATERIAL.metallicFactor, MATERIAL.roughnessFactor) * texture(textures[int(MATERIAL.metallicRoughnessTextureIndex) + 1], fragMetallicRoughnessTexcoord).bg;
    float metallic = metallicRoughness.x;
    float roughness = metallicRoughness.y;

    vec3 N;
    if (int(MATERIAL.normalTextureIndex) != -1){
        vec3 tangentNormal = texture(textures[int(MATERIAL.normalTextureIndex) + 1], fragNormalTexcoord).rgb;
        vec3 scaledNormal = (2.0 * tangentNormal - 1.0) * vec3(MATERIAL.normalScale, MATERIAL.normalScale, 1.0);
        N = normalize(fragTBN * scaledNormal);
    }
    else {
        N = normalize(fragTBN[2]);
    }

    float occlusion = 1.0 + MATERIAL.occlusionStrength * (texture(textures[int(MATERIAL.occlusionTextureIndex) + 1], fragOcclusionTexcoord).r - 1.0);

    vec3 emissive = MATERIAL.emissiveFactor * texture(textures[int(MATERIAL.emissiveTextureIndex) + 1], fragEmissiveTexcoord).rgb;

    vec3 V = normalize(pc.viewPosition - fragPosition);
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

    // Compare the luminance of color and emissive.
    // If emissive is brighter, use it.
    if (dot(emissive, REC_709_LUMA) > dot(color, REC_709_LUMA)){
        color = emissive;
    }

    float alpha = baseColor.a;
    // Apply sharpness to the alpha.
    // See: https://bgolus.medium.com/anti-aliased-alpha-test-the-esoteric-alpha-to-coverage-8b177335ae4f.
    alpha = (alpha - ALPHA_CUTOFF) / max(fwidth(alpha), 1e-4) + 0.5;

    outColor = vec4(color, alpha);
}
)frag";

vk_gltf_viewer::vulkan::pipelines::AlphaMaskedPrimitiveRenderer::AlphaMaskedPrimitiveRenderer(
    const vk::raii::Device &device,
    vk::PipelineLayout primitiveRendererPipelineLayout,
    const shaderc::Compiler &compiler
) : pipeline { createPipeline(device, primitiveRendererPipelineLayout, compiler) } { }

auto vk_gltf_viewer::vulkan::pipelines::AlphaMaskedPrimitiveRenderer::bindPipeline(
    vk::CommandBuffer commandBuffer
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
}

auto vk_gltf_viewer::vulkan::pipelines::AlphaMaskedPrimitiveRenderer::createPipeline(
    const vk::raii::Device &device,
    vk::PipelineLayout primitiveRendererPipelineLayout,
    const shaderc::Compiler &compiler
) const -> decltype(pipeline) {
    const auto [_, stages] = createStages(
        device,
        vku::Shader { compiler, vert, vk::ShaderStageFlagBits::eVertex },
        vku::Shader { compiler, frag, vk::ShaderStageFlagBits::eFragment });

    return { device, nullptr, vk::StructureChain {
        vku::getDefaultGraphicsPipelineCreateInfo(stages, primitiveRendererPipelineLayout, 1, true, vk::SampleCountFlagBits::e4)
            .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                {},
                true, true, vk::CompareOp::eGreater, // Use reverse Z.
            }))
            .setPMultisampleState(vku::unsafeAddress(vk::PipelineMultisampleStateCreateInfo {
                {},
                vk::SampleCountFlagBits::e4,
                {}, {}, {},
                true,
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
            vku::unsafeProxy({ vk::Format::eR16G16B16A16Sfloat }),
            vk::Format::eD32Sfloat,
        }
    }.get() };
}