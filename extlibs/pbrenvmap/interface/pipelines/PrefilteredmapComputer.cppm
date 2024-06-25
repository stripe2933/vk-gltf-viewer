module;

#include <cstdint>
#include <array>
#include <compare>
#include <format>
#include <ranges>
#include <span>
#include <string_view>
#include <vector>

#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

export module pbrenvmap:pipelines.PrefilteredmapComputer;

import vku;
export import vulkan_hpp;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

namespace pbrenvmap::pipelines {
    export class PrefilteredmapComputer {
    public:
        struct SpecializationConstants {
            std::uint32_t roughnessLevels;
            std::uint32_t samples;
        };

        struct DescriptorSetLayouts : vku::DescriptorSetLayouts<2> {
            explicit DescriptorSetLayouts(const vk::raii::Device &device, std::uint32_t roughnessLevels);
        };

        struct DescriptorSets : vku::DescriptorSets<DescriptorSetLayouts> {
            using vku::DescriptorSets<DescriptorSetLayouts>::DescriptorSets;

            [[nodiscard]] auto getDescriptorWrites0(
                vk::Sampler cubemapSampler,
                vk::ImageView cubemapImageView,
                auto &&prefilteredmapMipViews
            ) const {
                return vku::RefHolder {
                    [this](const vk::DescriptorImageInfo &cubemapImageInfo, std::span<const vk::DescriptorImageInfo> prefilteredmapImageInfos) {
                        return std::array {
                            getDescriptorWrite<0, 0>().setImageInfo(cubemapImageInfo),
                            getDescriptorWrite<0, 1>().setImageInfo(prefilteredmapImageInfos),
                        };
                    },
                    vk::DescriptorImageInfo { cubemapSampler, cubemapImageView, vk::ImageLayout::eShaderReadOnlyOptimal },
                    std::vector { std::from_range, FWD(prefilteredmapMipViews) | std::views::transform([](vk::ImageView imageView) {
                        return vk::DescriptorImageInfo { {}, imageView, vk::ImageLayout::eGeneral };
                    }) },
                };
            }
        };

        struct PushConstant {
            std::uint32_t cubemapSize;
            std::uint32_t mipLevel;
        };

        DescriptorSetLayouts descriptorSetLayouts;
        vk::raii::PipelineLayout pipelineLayout;
        std::uint32_t roughnessLevels;
        vk::raii::Pipeline pipeline;

        PrefilteredmapComputer(const vk::raii::Device &device, const SpecializationConstants &specializationConstants, const shaderc::Compiler &compiler);

        auto compute(vk::CommandBuffer commandBuffer, const DescriptorSets &descriptorSets, std::uint32_t cubemapSize, std::uint32_t prefilteredmapSize) const -> void;

    private:
        static std::string_view comp;

        [[nodiscard]] auto createPipelineLayout(const vk::raii::Device &device) -> vk::raii::PipelineLayout;
        [[nodiscard]] auto createPipeline(const vk::raii::Device &device, const SpecializationConstants &specializationConstants, const shaderc::Compiler &compiler) const -> vk::raii::Pipeline;
    };
}

// module :private;

// language=comp
std::string_view pbrenvmap::pipelines::PrefilteredmapComputer::comp = R"comp(
#version 450

layout (constant_id = 0) const uint ROUGHNESS_LEVELS = 8;
layout (constant_id = 1) const uint SAMPLES = 1024;

layout (set = 0, binding = 0) uniform samplerCube cubemapSampler;
layout (set = 0, binding = 1) writeonly uniform imageCube prefilteredmapMipImages[ROUGHNESS_LEVELS];

layout (push_constant) uniform PushConstant {
    uint cubemapSize;
    uint mipLevel;
} pc;

layout (local_size_x = 16, local_size_y = 16) in;

// --------------------
// Functions.
// --------------------

vec3 getWorldDirection(uvec3 coord, uint imageSize){
    vec2 texcoord = 2.0 * vec2(coord.xy) / imageSize - 1.0;
    switch (coord.z){
        case 0U: return normalize(vec3(1.0, texcoord.y, -texcoord.x));
        case 1U: return normalize(vec3(-1.0, texcoord.yx));
        case 2U: return normalize(vec3(texcoord.x, -1.0, texcoord.y));
        case 3U: return normalize(vec3(texcoord.x, 1.0, -texcoord.y));
        case 4U: return normalize(vec3(texcoord, 1.0));
        case 5U: return normalize(vec3(-texcoord.x, texcoord.y, -1.0));
    }
    return vec3(0.0); // unreachable.
}

// Van Der Corpus sequence
// @see http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
float vdcSequence(uint bits){
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

// Hammersley sequence
// @see http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
vec2 hammersleySequence(uint i, uint N){
    return vec2(float(i) / float(N), vdcSequence(i));
}

// GGX NDF via importance sampling
vec3 importanceSampleGGX(vec2 Xi, vec3 N, float roughness){
    const float PI = 3.14159265359;

    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;

    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (alpha2 - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    // from spherical coordinates to cartesian coordinates
    vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

    // from tangent-space vector to world-space sample vector
    vec3 up        = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent   = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

// Normal Distribution
float dGgx(float dotNH, float roughness){
    const float PI = 3.14159265359;

    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float denom = dotNH * dotNH * (alpha2 - 1.0) + 1.0;
    return alpha2 / (PI * denom * denom);
}

void main(){
    const float PI = 3.14159265359;

    uvec2 prefilteredmapImageSize = imageSize(prefilteredmapMipImages[pc.mipLevel]);
    if (gl_GlobalInvocationID.x >= prefilteredmapImageSize.x || gl_GlobalInvocationID.y >= prefilteredmapImageSize.y){
        return;
    }

    float roughness = float(pc.mipLevel) / float(ROUGHNESS_LEVELS - 1U);
    float saTexel  = 4.0 * PI / (6 * pc.cubemapSize * pc.cubemapSize);

    // tagent space from origin point
    vec3 N = getWorldDirection(ivec3(gl_GlobalInvocationID), prefilteredmapImageSize.x);
    // assume view direction always equal to outgoing direction
    vec3 V = N;

    float totalWeight = 0.0;
    vec3 prefilteredColor = vec3(0.0);
    for (uint i = 0; i < SAMPLES; ++i){
        // generate sample vector towards the alignment of the specular lobe
        vec2 Xi = hammersleySequence(i, SAMPLES);
        vec3 H = importanceSampleGGX(Xi, N, roughness);
        vec3 L = reflect(-V, H);

        float dotNL = dot(N, L);
        if (dotNL > 0.0){
            float dotNH = max(dot(N, H), 0.0);
            // sample from the environment's mip level based on roughness/pdf
            float D = dGgx(dotNH, roughness);
            float pdf = 0.25 * D + 0.0001;

            float saSample = 1.0 / (SAMPLES * pdf + 0.0001);
            float mipLevel = roughness == 0.0 ? 0.0 : 0.5 * log2(saSample / saTexel);

            // TODO: don't know why, but use original L flips the prefilteredmap in y-axis.
            prefilteredColor += textureLod(cubemapSampler, vec3(L.x, -L.y, L.z), mipLevel).rgb * dotNL;
            totalWeight += dotNL;
        }
    }
    prefilteredColor /= totalWeight;

    imageStore(prefilteredmapMipImages[pc.mipLevel], ivec3(gl_GlobalInvocationID), vec4(prefilteredColor, 1.0));
}
)comp";

pbrenvmap::pipelines::PrefilteredmapComputer::DescriptorSetLayouts::DescriptorSetLayouts(
    const vk::raii::Device &device,
    std::uint32_t roughnessLevels
) : vku::DescriptorSetLayouts<2> { device, LayoutBindings {
    vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
    vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute },
    vk::DescriptorSetLayoutBinding { 1, vk::DescriptorType::eStorageImage, roughnessLevels, vk::ShaderStageFlagBits::eCompute },
    std::array { vk::DescriptorBindingFlags{}, vk::Flags { vk::DescriptorBindingFlagBits::eUpdateAfterBind } },
} } { }

pbrenvmap::pipelines::PrefilteredmapComputer::PrefilteredmapComputer(
    const vk::raii::Device &device,
    const SpecializationConstants &specializationConstants,
    const shaderc::Compiler &compiler
) : descriptorSetLayouts { device, specializationConstants.roughnessLevels },
    pipelineLayout { createPipelineLayout(device) },
    roughnessLevels { specializationConstants.roughnessLevels },
    pipeline { createPipeline(device, specializationConstants, compiler) } { }

auto pbrenvmap::pipelines::PrefilteredmapComputer::compute(
    vk::CommandBuffer commandBuffer,
    const DescriptorSets &descriptorSets,
    std::uint32_t cubemapSize,
    std::uint32_t prefilteredmapSize
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, descriptorSets, {});
    for (std::uint32_t mipLevel : std::views::iota(0U, roughnessLevels)) {
        if (mipLevel == 0U) {
            commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, PushConstant { cubemapSize, mipLevel });
        }
        else {
            commandBuffer.pushConstants<std::uint32_t>(*pipelineLayout, vk::ShaderStageFlagBits::eCompute, offsetof(PushConstant, mipLevel), mipLevel);
        }
        commandBuffer.dispatch(
            vku::divCeil(prefilteredmapSize >> mipLevel, 16U),
            vku::divCeil(prefilteredmapSize >> mipLevel, 16U),
            6);
    }
}

auto pbrenvmap::pipelines::PrefilteredmapComputer::createPipelineLayout(
    const vk::raii::Device &device
) -> vk::raii::PipelineLayout {
    return { device, vk::PipelineLayoutCreateInfo {
        {},
        descriptorSetLayouts,
        vku::unsafeProxy({
            vk::PushConstantRange {
                vk::ShaderStageFlagBits::eCompute,
                0, sizeof(PushConstant),
            },
        }),
    } };
}

auto pbrenvmap::pipelines::PrefilteredmapComputer::createPipeline(
    const vk::raii::Device &device,
    const SpecializationConstants &specializationConstants,
    const shaderc::Compiler &compiler
) const -> vk::raii::Pipeline {
    // TODO: handle specialization constants.
    const auto [_, stages] = vku::createStages(device,
        vku::Shader { compiler, comp, vk::ShaderStageFlagBits::eCompute });
    return { device, nullptr, vk::ComputePipelineCreateInfo {
        {},
        get<0>(stages),
        *pipelineLayout,
    } };
}