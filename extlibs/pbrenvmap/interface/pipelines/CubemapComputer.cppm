module;

#include <cstdint>
#include <array>
#include <compare>
#include <format>
#include <string_view>

#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

export module pbrenvmap:pipelines.CubemapComputer;

import vku;
export import vulkan_hpp;
import :details.ranges;

namespace pbrenvmap::pipelines {
    export class CubemapComputer {
    public:
        struct DescriptorSetLayouts : vku::DescriptorSetLayouts<2> {
            explicit DescriptorSetLayouts(const vk::raii::Device &device);
        };

        struct DescriptorSets : vku::DescriptorSets<DescriptorSetLayouts> {
            using vku::DescriptorSets<DescriptorSetLayouts>::DescriptorSets;

            [[nodiscard]] auto getDescriptorWrites0(
                vk::Sampler eqmapSampler,
                vk::ImageView eqmapImageView,
                vk::ImageView cubemapImageView
            ) const {
                return vku::RefHolder {
                    [this](const vk::DescriptorImageInfo &eqmapImageInfo, const vk::DescriptorImageInfo &cubemapImageInfo) {
                        return std::array {
                            getDescriptorWrite<0, 0>().setImageInfo(eqmapImageInfo),
                            getDescriptorWrite<0, 1>().setImageInfo(cubemapImageInfo),
                        };
                    },
                    vk::DescriptorImageInfo { eqmapSampler, eqmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal },
                    vk::DescriptorImageInfo { {}, cubemapImageView, vk::ImageLayout::eGeneral },
                };
            }
        };

        DescriptorSetLayouts descriptorSetLayouts;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        CubemapComputer(const vk::raii::Device &device, const shaderc::Compiler &compiler);

        auto compute(vk::CommandBuffer commandBuffer, const DescriptorSets &descriptorSets, std::uint32_t cubemapSize) const -> void;

    private:
        static std::string_view comp;

        [[nodiscard]] auto createPipelineLayout(const vk::raii::Device &device) -> vk::raii::PipelineLayout;
        [[nodiscard]] auto createPipeline(const vk::raii::Device &device, const shaderc::Compiler &compiler) const -> vk::raii::Pipeline;
    };
}

// module :private;

// language=comp
std::string_view pbrenvmap::pipelines::CubemapComputer::comp = R"comp(
#version 450

const vec2 INV_ATAN = vec2(0.1591, 0.3183);

layout (set = 0, binding = 0) uniform sampler2D eqmapSampler;
layout (set = 0, binding = 1) writeonly uniform imageCube cubemapImage;

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

vec2 sampleSphericalMap(vec3 v){
    return INV_ATAN * vec2(atan(v.z, v.x), asin(v.y)) + 0.5;
}

void main(){
    vec2 uv = sampleSphericalMap(getWorldDirection(gl_GlobalInvocationID, imageSize(cubemapImage).x));
    imageStore(cubemapImage, ivec3(gl_GlobalInvocationID), textureLod(eqmapSampler, uv, 0.0));
}
)comp";

pbrenvmap::pipelines::CubemapComputer::DescriptorSetLayouts::DescriptorSetLayouts(
    const vk::raii::Device &device
) : vku::DescriptorSetLayouts<2> { device, LayoutBindings {
        {},
        vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute },
        vk::DescriptorSetLayoutBinding { 1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute },
    } } { }

pbrenvmap::pipelines::CubemapComputer::CubemapComputer(
    const vk::raii::Device &device,
    const shaderc::Compiler &compiler
) : descriptorSetLayouts { device },
    pipelineLayout { createPipelineLayout(device) },
    pipeline { createPipeline(device, compiler) } { }

auto pbrenvmap::pipelines::CubemapComputer::compute(
    vk::CommandBuffer commandBuffer,
    const DescriptorSets &descriptorSets,
    std::uint32_t cubemapSize
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, descriptorSets, {});
    commandBuffer.dispatch(cubemapSize / 16, cubemapSize / 16, 6);
}

auto pbrenvmap::pipelines::CubemapComputer::createPipelineLayout(
    const vk::raii::Device &device
) -> vk::raii::PipelineLayout {
    return { device, vk::PipelineLayoutCreateInfo {
            {},
            descriptorSetLayouts,
        } };
}

auto pbrenvmap::pipelines::CubemapComputer::createPipeline(
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