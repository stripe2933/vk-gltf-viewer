module;

#include <cstdint>
#include <array>
#include <compare>
#include <format>
#include <string_view>

#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

export module pbrenvmap:pipelines.SphericalHarmonicsComputer;

import vku;
export import vulkan_hpp;

namespace pbrenvmap::pipelines {
    class SphericalHarmonicsComputer {
    public:
        struct DescriptorSetLayouts : vku::DescriptorSetLayouts<2> {
            explicit DescriptorSetLayouts(const vk::raii::Device &device);
        };

        struct DescriptorSets : vku::DescriptorSets<DescriptorSetLayouts> {
            using vku::DescriptorSets<DescriptorSetLayouts>::DescriptorSets;

            [[nodiscard]] auto getDescriptorWrites0(
                vk::ImageView cubemapImageView,
                const vk::DescriptorBufferInfo &reductionBufferInfo
            ) const {
                return vku::RefHolder {
                    [this](const vk::DescriptorImageInfo &cubemapImageInfo, const vk::DescriptorBufferInfo &reductionBufferInfo) {
                        return std::array {
                            getDescriptorWrite<0, 0>().setImageInfo(cubemapImageInfo),
                            getDescriptorWrite<0, 1>().setBufferInfo(reductionBufferInfo),
                        };
                    },
                    vk::DescriptorImageInfo { {}, cubemapImageView, vk::ImageLayout::eGeneral },
                    reductionBufferInfo,
                };
            }
        };

        DescriptorSetLayouts descriptorSetLayouts;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        SphericalHarmonicsComputer(const vk::raii::Device &device, const shaderc::Compiler &compiler);

        auto compute(vk::CommandBuffer commandBuffer, const DescriptorSets &descriptorSets, std::uint32_t cubemapSize) const -> void;

        [[nodiscard]] static auto getWorkgroupCount(std::uint32_t cubemapSize) noexcept -> std::array<std::uint32_t, 3>;

    private:
        static std::string_view comp;

        [[nodiscard]] auto createPipelineLayout(const vk::raii::Device &device) -> vk::raii::PipelineLayout;
        [[nodiscard]] auto createPipeline(const vk::raii::Device &device, const shaderc::Compiler &compiler) const -> vk::raii::Pipeline;
    };
}

// module :private;

// language=comp
std::string_view pbrenvmap::pipelines::SphericalHarmonicsComputer::comp = R"comp(
#version 450
#extension GL_KHR_shader_subgroup_arithmetic : require
#extension GL_EXT_scalar_block_layout : require

struct SphericalHarmonicBasis{
    float band0[1];
    float band1[3];
    float band2[5];
};

layout (set = 0, binding = 0, rgba32f) readonly uniform imageCube cubemapImage;
layout (set = 0, binding = 1, scalar) writeonly buffer ReductionBuffer {
    vec3 coefficients[][9];
} reduction;

layout (local_size_x = 16, local_size_y = 16) in;

shared vec3 sharedData[16][9]; // For gl_SubgroupSize = 16 (minimum requirement)

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

float texelSolidAngle(uvec2 xy, uint cubemapSize){
    vec2 uv = mix(vec2(-1), vec2(1), (xy + 0.5) / cubemapSize);
    return 4.0 * pow(1.0 + dot(uv, uv), -1.5);
}

SphericalHarmonicBasis getSphericalHarmonicBasis(vec3 v){
    return SphericalHarmonicBasis(
        float[1](0.282095),
        float[3](-0.488603 * v.y, 0.488603 * v.z, -0.488603 * v.x),
        float[5](1.092548 * v.x * v.y, -1.092548 * v.y * v.z, 0.315392 * (3.0 * v.z * v.z - 1.0), -1.092548 * v.x * v.z, 0.546274 * (v.x * v.x - v.y * v.y))
    );
}

void main(){
    vec3 coefficients[9] = vec3[9](vec3(0), vec3(0), vec3(0), vec3(0), vec3(0), vec3(0), vec3(0), vec3(0), vec3(0));

    uint cubemapImageSize = imageSize(cubemapImage).x;
    float solidAngle = texelSolidAngle(gl_GlobalInvocationID.xy, cubemapImageSize);
    for (uint faceIndex = 0; faceIndex < 6U; ++faceIndex){
        vec3 L = solidAngle * imageLoad(cubemapImage, ivec3(gl_GlobalInvocationID.xy, faceIndex)).rgb;
        vec3 normal = getWorldDirection(ivec3(gl_GlobalInvocationID.xy, faceIndex), cubemapImageSize);
        SphericalHarmonicBasis basis = getSphericalHarmonicBasis(normal);

        coefficients[0] += L * basis.band0[0];
        coefficients[1] += L * basis.band1[0];
        coefficients[2] += L * basis.band1[1];
        coefficients[3] += L * basis.band1[2];
        coefficients[4] += L * basis.band2[0];
        coefficients[5] += L * basis.band2[1];
        coefficients[6] += L * basis.band2[2];
        coefficients[7] += L * basis.band2[3];
        coefficients[8] += L * basis.band2[4];
    }

    vec3 subgroupCoefficients[9] = vec3[9](
        subgroupAdd(coefficients[0]),
        subgroupAdd(coefficients[1]),
        subgroupAdd(coefficients[2]),
        subgroupAdd(coefficients[3]),
        subgroupAdd(coefficients[4]),
        subgroupAdd(coefficients[5]),
        subgroupAdd(coefficients[6]),
        subgroupAdd(coefficients[7]),
        subgroupAdd(coefficients[8])
    );
    if (subgroupElect()){
        sharedData[gl_SubgroupID] = subgroupCoefficients;
    }

    memoryBarrierShared();
    barrier();

    if (gl_SubgroupID == 0U){
        bool isInvocationInSubgroup = gl_SubgroupInvocationID < gl_NumSubgroups;
        coefficients = vec3[9](
            subgroupAdd(isInvocationInSubgroup ? sharedData[gl_SubgroupInvocationID][0] : vec3(0.0)),
            subgroupAdd(isInvocationInSubgroup ? sharedData[gl_SubgroupInvocationID][1] : vec3(0.0)),
            subgroupAdd(isInvocationInSubgroup ? sharedData[gl_SubgroupInvocationID][2] : vec3(0.0)),
            subgroupAdd(isInvocationInSubgroup ? sharedData[gl_SubgroupInvocationID][3] : vec3(0.0)),
            subgroupAdd(isInvocationInSubgroup ? sharedData[gl_SubgroupInvocationID][4] : vec3(0.0)),
            subgroupAdd(isInvocationInSubgroup ? sharedData[gl_SubgroupInvocationID][5] : vec3(0.0)),
            subgroupAdd(isInvocationInSubgroup ? sharedData[gl_SubgroupInvocationID][6] : vec3(0.0)),
            subgroupAdd(isInvocationInSubgroup ? sharedData[gl_SubgroupInvocationID][7] : vec3(0.0)),
            subgroupAdd(isInvocationInSubgroup ? sharedData[gl_SubgroupInvocationID][8] : vec3(0.0))
        );
    }

    if (gl_LocalInvocationID.xy == uvec2(0)){
        // TODO: why reduction.coefficients[gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x] = coefficients; doesn't work?
        for (uint i = 0; i < 9U; ++i){
            reduction.coefficients[gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x][i] = coefficients[i];
        }
    }
}
)comp";

pbrenvmap::pipelines::SphericalHarmonicsComputer::DescriptorSetLayouts::DescriptorSetLayouts(
    const vk::raii::Device &device
) : vku::DescriptorSetLayouts<2> { device, LayoutBindings {
        {},
        vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute },
        vk::DescriptorSetLayoutBinding { 1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute },
    } } { }

pbrenvmap::pipelines::SphericalHarmonicsComputer::SphericalHarmonicsComputer(
    const vk::raii::Device &device,
    const shaderc::Compiler &compiler
) : descriptorSetLayouts { device },
    pipelineLayout { createPipelineLayout(device) },
    pipeline { createPipeline(device, compiler) } { }

auto pbrenvmap::pipelines::SphericalHarmonicsComputer::compute(
    vk::CommandBuffer commandBuffer,
    const DescriptorSets &descriptorSets,
    std::uint32_t cubemapSize
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, descriptorSets, {});
    const std::array workgroupCount = getWorkgroupCount(cubemapSize);
    commandBuffer.dispatch(get<0>(workgroupCount), get<1>(workgroupCount), get<2>(workgroupCount));
}

auto pbrenvmap::pipelines::SphericalHarmonicsComputer::getWorkgroupCount(
    std::uint32_t cubemapSize
) noexcept -> std::array<std::uint32_t, 3> {
    return { cubemapSize / 16U, cubemapSize / 16U, 1 };
}

auto pbrenvmap::pipelines::SphericalHarmonicsComputer::createPipelineLayout(
    const vk::raii::Device &device
) -> vk::raii::PipelineLayout {
    return { device, vk::PipelineLayoutCreateInfo {
        {},
        descriptorSetLayouts,
    } };
}

auto pbrenvmap::pipelines::SphericalHarmonicsComputer::createPipeline(
    const vk::raii::Device &device,
    const shaderc::Compiler &compiler
    ) const -> vk::raii::Pipeline {
    const auto [_, stages] = vku::createStages(device,
        vku::Shader { compiler, comp, vk::ShaderStageFlagBits::eCompute });
    return { device, nullptr, vk::ComputePipelineCreateInfo {
        {},
        get<0>(stages),
        *pipelineLayout,
    } };
}