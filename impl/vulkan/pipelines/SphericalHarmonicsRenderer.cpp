module;

#include <compare>
#include <format>
#include <string_view>
#include <vector>

#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.pipelines.SphericalHarmonicsRenderer;

import vku;

// language=vert
std::string_view vk_gltf_viewer::vulkan::pipelines::SphericalHarmonicsRenderer::vert = R"vert(
#version 450

const vec3[] positions = vec3[8](
    vec3(-1.0, -1.0, -1.0),
    vec3(-1.0, -1.0,  1.0),
    vec3(-1.0,  1.0, -1.0),
    vec3(-1.0,  1.0,  1.0),
    vec3( 1.0, -1.0, -1.0),
    vec3( 1.0, -1.0,  1.0),
    vec3( 1.0,  1.0, -1.0),
    vec3( 1.0,  1.0,  1.0)
);

layout (location = 0) out vec3 fragPosition;

layout (push_constant) uniform PushConstant {
    mat4 projectionView;
} pc;

void main() {
    fragPosition = positions[gl_VertexIndex];
    gl_Position = (pc.projectionView * vec4(fragPosition, 1.0)).xyww;
}
)vert";

// language=frag
std::string_view vk_gltf_viewer::vulkan::pipelines::SphericalHarmonicsRenderer::frag = R"frag(
#version 450

#extension GL_EXT_scalar_block_layout : require

struct SphericalHarmonicBasis{
    float band0[1];
    float band1[3];
    float band2[5];
};

layout (location = 0) in vec3 fragPosition;

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0, scalar) uniform SphericalHarmonicsBuffer {
    vec3 coefficients[9];
} sphericalHarmonics;

layout (early_fragment_tests) in;

// --------------------
// Functions.
// --------------------

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

void main() {
    outColor = vec4(computeDiffuseIrradiance(normalize(fragPosition)), 1.0);
}
)frag";

vk_gltf_viewer::vulkan::pipelines::SphericalHarmonicsRenderer::DescriptorSetLayouts::DescriptorSetLayouts(
    const vk::raii::Device &device
) : vku::DescriptorSetLayouts<1> { device, LayoutBindings {
        {},
        vk::DescriptorSetLayoutBinding {
            0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eFragment,
        },
    } } { }

vk_gltf_viewer::vulkan::pipelines::SphericalHarmonicsRenderer::SphericalHarmonicsRenderer(
    const Gpu &gpu,
    const shaderc::Compiler &compiler
) : descriptorSetLayouts { gpu.device },
    pipelineLayout { createPipelineLayout(gpu.device) },
    pipeline { createPipeline(gpu.device, compiler) },
    indexBuffer { createIndexBuffer(gpu.allocator) } { }

auto vk_gltf_viewer::vulkan::pipelines::SphericalHarmonicsRenderer::draw(
    vk::CommandBuffer commandBuffer,
    const DescriptorSets &descriptorSets,
    const PushConstant &pushConstant
) const -> void {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, descriptorSets, {});
    commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eAllGraphics, 0, pushConstant);
    commandBuffer.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint16);
    commandBuffer.drawIndexed(36, 1, 0, 0, 0);
}

auto vk_gltf_viewer::vulkan::pipelines::SphericalHarmonicsRenderer::createPipelineLayout(
    const vk::raii::Device &device
) const -> vk::raii::PipelineLayout {
    return { device, vk::PipelineLayoutCreateInfo {
        {},
        descriptorSetLayouts,
        vku::unsafeProxy({
            vk::PushConstantRange {
                vk::ShaderStageFlagBits::eAllGraphics,
                0, sizeof(PushConstant),
            },
        }),
    } };
}

auto vk_gltf_viewer::vulkan::pipelines::SphericalHarmonicsRenderer::createPipeline(
    const vk::raii::Device &device,
    const shaderc::Compiler &compiler
) const -> vk::raii::Pipeline {
    const auto [_, stages] = createStages(
        device,
        vku::Shader { compiler, vert, vk::ShaderStageFlagBits::eVertex },
        vku::Shader { compiler, frag, vk::ShaderStageFlagBits::eFragment });

    return { device, nullptr, vk::StructureChain {
        vku::getDefaultGraphicsPipelineCreateInfo(stages, *pipelineLayout, 1, true, vk::SampleCountFlagBits::e4)
            .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
                {},
                vk::False, vk::False,
                vk::PolygonMode::eFill,
                vk::CullModeFlagBits::eNone, {},
                {}, {}, {}, {},
                1.f,
            }))
            .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                {},
                vk::True, vk::True, vk::CompareOp::eLessOrEqual,
            })),
        vk::PipelineRenderingCreateInfo {
            {},
            vku::unsafeProxy({ vk::Format::eR16G16B16A16Sfloat }),
            vk::Format::eD32Sfloat,
        },
    }.get() };
}

auto vk_gltf_viewer::vulkan::pipelines::SphericalHarmonicsRenderer::createIndexBuffer(
    vma::Allocator allocator
) const -> decltype(indexBuffer) {
    return {
        allocator,
        std::from_range, std::vector<std::uint16_t> {
            2, 6, 7, 2, 3, 7, 0, 4, 5, 0, 1, 5, 0, 2, 6, 0, 4, 6,
            1, 3, 7, 1, 5, 7, 0, 2, 3, 0, 1, 3, 4, 6, 7, 4, 5, 7,
        },
        vk::BufferUsageFlagBits::eIndexBuffer,
    };
}