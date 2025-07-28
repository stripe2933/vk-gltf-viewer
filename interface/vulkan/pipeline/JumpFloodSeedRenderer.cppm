module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.JumpFloodSeedRenderer;

import std;
export import fastgltf;
import vku;

import vk_gltf_viewer.shader.jump_flood_seed_frag;
import vk_gltf_viewer.shader.jump_flood_seed_vert;
export import vk_gltf_viewer.vulkan.pl.Primitive;
import vk_gltf_viewer.vulkan.specialization_constants.SpecializationMap;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class JumpFloodSeedRendererSpecialization {
    public:
        std::optional<vk::PrimitiveTopology> topologyClass; // Only list topology will be used in here.
        fastgltf::ComponentType positionComponentType;
        bool positionNormalized;
        std::uint32_t positionMorphTargetCount;
        std::uint32_t skinAttributeCount;

        [[nodiscard]] bool operator==(const JumpFloodSeedRendererSpecialization&) const = default;

        [[nodiscard]] vk::raii::Pipeline createPipeline(
            const vk::raii::Device &device LIFETIMEBOUND,
            const pl::Primitive &pipelineLayout LIFETIMEBOUND
        ) const;

    private:
        struct VertexShaderSpecializationData;

        [[nodiscard]] VertexShaderSpecializationData getVertexShaderSpecializationData() const noexcept;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

struct vk_gltf_viewer::vulkan::pipeline::JumpFloodSeedRendererSpecialization::VertexShaderSpecializationData {
    std::uint32_t positionComponentType;
    vk::Bool32 positionNormalized;
    std::uint32_t positionMorphTargetCount;
    std::uint32_t skinAttributeCount;
};

[[nodiscard]] vk::raii::Pipeline vk_gltf_viewer::vulkan::pipeline::JumpFloodSeedRendererSpecialization::createPipeline(
    const vk::raii::Device &device,
    const pl::Primitive &pipelineLayout
) const {
    return { device, nullptr, vk::StructureChain {
        vku::getDefaultGraphicsPipelineCreateInfo(
            createPipelineStages(
                device,
                vku::Shader {
                    shader::jump_flood_seed_vert,
                    vk::ShaderStageFlagBits::eVertex,
                    vku::unsafeAddress(vk::SpecializationInfo {
                        SpecializationMap<VertexShaderSpecializationData>::value,
                        vku::unsafeProxy(getVertexShaderSpecializationData()),
                    }),
                },
                vku::Shader { shader::jump_flood_seed_frag, vk::ShaderStageFlagBits::eFragment }).get(),
            *pipelineLayout, 1, true)
            .setPInputAssemblyState(vku::unsafeAddress(vk::PipelineInputAssemblyStateCreateInfo {
                {},
                topologyClass.value_or(vk::PrimitiveTopology::eTriangleList),
            }))
            .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                {},
                true, true, vk::CompareOp::eGreater, // Use reverse Z.
            }))
            .setPDynamicState(vku::unsafeAddress(vk::PipelineDynamicStateCreateInfo {
                {},
                vku::unsafeProxy({
                    vk::DynamicState::eViewport,
                    vk::DynamicState::eScissor,
                    vk::DynamicState::ePrimitiveTopology,
                    vk::DynamicState::eCullMode,
                }),
            })),
        vk::PipelineRenderingCreateInfo {
            {},
            vku::unsafeProxy(vk::Format::eR16G16Uint),
            vk::Format::eD32Sfloat,
        }
    }.get() };
}

[[nodiscard]] vk_gltf_viewer::vulkan::pipeline::JumpFloodSeedRendererSpecialization::VertexShaderSpecializationData vk_gltf_viewer::vulkan::pipeline::JumpFloodSeedRendererSpecialization::getVertexShaderSpecializationData() const noexcept {
    return {
        .positionComponentType = getGLComponentType(positionComponentType),
        .positionNormalized = positionNormalized,
        .positionMorphTargetCount = positionMorphTargetCount,
        .skinAttributeCount = skinAttributeCount,
    };
}