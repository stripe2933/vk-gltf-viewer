module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.JumpFloodSeedRenderPipeline;

import std;
export import fastgltf;
import vku;

import vk_gltf_viewer.shader.jump_flood_seed_frag;
import vk_gltf_viewer.shader.jump_flood_seed_vert;
export import vk_gltf_viewer.vulkan.pipeline_layout.PrimitiveNoShading;
import vk_gltf_viewer.vulkan.specialization_constants.SpecializationMap;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class JumpFloodSeedRenderPipeline final : public vk::raii::Pipeline {
    public:
        struct Config {
            std::optional<vk::PrimitiveTopology> topologyClass; // Only list topology will be used in here.
            fastgltf::ComponentType positionComponentType;
            bool positionNormalized;
            std::uint32_t positionMorphTargetCount;
            std::uint32_t skinAttributeCount;

            [[nodiscard]] bool operator==(const Config&) const = default;
        };

        JumpFloodSeedRenderPipeline(
            const vk::raii::Device &device LIFETIMEBOUND,
            const pl::PrimitiveNoShading &pipelineLayout LIFETIMEBOUND,
            const Config &config
        );

    private:
        struct VertexShaderSpecialization;

        [[nodiscard]] static VertexShaderSpecialization getVertexShaderSpecialization(const Config &config) noexcept;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

struct vk_gltf_viewer::vulkan::pipeline::JumpFloodSeedRenderPipeline::VertexShaderSpecialization {
    std::uint32_t positionComponentType;
    vk::Bool32 positionNormalized;
    std::uint32_t positionMorphTargetCount;
    std::uint32_t skinAttributeCount;
};

vk_gltf_viewer::vulkan::pipeline::JumpFloodSeedRenderPipeline::JumpFloodSeedRenderPipeline(
    const vk::raii::Device &device,
    const pl::PrimitiveNoShading &pipelineLayout,
    const Config &config
) : Pipeline { [&] -> Pipeline {
        return { device, nullptr, vk::StructureChain {
            vku::getDefaultGraphicsPipelineCreateInfo(
                createPipelineStages(
                    device,
                    vku::Shader {
                        shader::jump_flood_seed_vert,
                        vk::ShaderStageFlagBits::eVertex,
                        vku::unsafeAddress(vk::SpecializationInfo {
                            SpecializationMap<VertexShaderSpecialization>::value,
                            vku::unsafeProxy(getVertexShaderSpecialization(config)),
                        }),
                    },
                    vku::Shader { shader::jump_flood_seed_frag, vk::ShaderStageFlagBits::eFragment }).get(),
                *pipelineLayout, 1, true)
                .setPInputAssemblyState(vku::unsafeAddress(vk::PipelineInputAssemblyStateCreateInfo {
                    {},
                    config.topologyClass.value_or(vk::PrimitiveTopology::eTriangleList),
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
    }() } { }

[[nodiscard]] vk_gltf_viewer::vulkan::pipeline::JumpFloodSeedRenderPipeline::VertexShaderSpecialization vk_gltf_viewer::vulkan::pipeline::JumpFloodSeedRenderPipeline::getVertexShaderSpecialization(const Config &config) noexcept {
    return {
        .positionComponentType = getGLComponentType(config.positionComponentType),
        .positionNormalized = config.positionNormalized,
        .positionMorphTargetCount = config.positionMorphTargetCount,
        .skinAttributeCount = config.skinAttributeCount,
    };
}