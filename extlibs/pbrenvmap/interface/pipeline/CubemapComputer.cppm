module;

#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

export module pbrenvmap:pipeline.CubemapComputer;

import std;
import vku;
export import vulkan_hpp;
import :details.ranges;

namespace pbrenvmap::pipeline {
    export class CubemapComputer {
    public:
        struct DescriptorSetLayouts : vku::DescriptorSetLayouts<2> {
            explicit DescriptorSetLayouts(const vk::raii::Device &device, const vk::Sampler &sampler);
        };

        struct DescriptorSets : vku::DescriptorSets<DescriptorSetLayouts> {
            using vku::DescriptorSets<DescriptorSetLayouts>::DescriptorSets;

            [[nodiscard]] auto getDescriptorWrites0(
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
                    vk::DescriptorImageInfo { {}, eqmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal },
                    vk::DescriptorImageInfo { {}, cubemapImageView, vk::ImageLayout::eGeneral },
                };
            }
        };

        vk::raii::Sampler eqmapSampler;
        DescriptorSetLayouts descriptorSetLayouts;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        CubemapComputer(const vk::raii::Device &device, const shaderc::Compiler &compiler);

        auto compute(vk::CommandBuffer commandBuffer, const DescriptorSets &descriptorSets, std::uint32_t cubemapSize) const -> void;

    private:
        static std::string_view comp;

        [[nodiscard]] auto createEqmapSampler(const vk::raii::Device &device) const -> decltype(eqmapSampler);
        [[nodiscard]] auto createPipelineLayout(const vk::raii::Device &device) const -> vk::raii::PipelineLayout;
        [[nodiscard]] auto createPipeline(const vk::raii::Device &device, const shaderc::Compiler &compiler) const -> vk::raii::Pipeline;
    };
}

// module :private;

// language=comp
std::string_view pbrenvmap::pipeline::CubemapComputer::comp = R"comp(
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

pbrenvmap::pipeline::CubemapComputer::DescriptorSetLayouts::DescriptorSetLayouts(
    const vk::raii::Device &device,
    const vk::Sampler &sampler
) : vku::DescriptorSetLayouts<2> {
        device,
        vk::DescriptorSetLayoutCreateInfo {
            {},
            vku::unsafeProxy({
                vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute, &sampler },
                vk::DescriptorSetLayoutBinding { 1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute },
            }),
        },
    } { }

pbrenvmap::pipeline::CubemapComputer::CubemapComputer(
    const vk::raii::Device &device,
    const shaderc::Compiler &compiler
) : eqmapSampler { createEqmapSampler(device) } ,
    descriptorSetLayouts { device, *eqmapSampler },
    pipelineLayout { createPipelineLayout(device) },
    pipeline { createPipeline(device, compiler) } { }

auto pbrenvmap::pipeline::CubemapComputer::compute(
    vk::CommandBuffer commandBuffer,
    const DescriptorSets &descriptorSets,
    std::uint32_t cubemapSize
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, descriptorSets, {});
    commandBuffer.dispatch(cubemapSize / 16, cubemapSize / 16, 6);
}

auto pbrenvmap::pipeline::CubemapComputer::createEqmapSampler(
    const vk::raii::Device &device
) const -> decltype(eqmapSampler) {
    return { device, vk::SamplerCreateInfo {
        {},
        vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eNearest,
        {}, {}, {},
        {},
        false, {},
        {}, {},
        0.f, vk::LodClampNone,
    } };
}

auto pbrenvmap::pipeline::CubemapComputer::createPipelineLayout(
    const vk::raii::Device &device
) const -> vk::raii::PipelineLayout {
    return { device, vk::PipelineLayoutCreateInfo {
        {},
        vku::unsafeProxy(descriptorSetLayouts.getHandles()),
    } };
}

auto pbrenvmap::pipeline::CubemapComputer::createPipeline(
    const vk::raii::Device &device,
    const shaderc::Compiler &compiler
) const -> vk::raii::Pipeline {
    return { device, nullptr, vk::ComputePipelineCreateInfo {
        {},
        get<0>(vku::createPipelineStages(
            device,
            vku::Shader { compiler, comp, vk::ShaderStageFlagBits::eCompute }).get()),
        *pipelineLayout,
    } };
}