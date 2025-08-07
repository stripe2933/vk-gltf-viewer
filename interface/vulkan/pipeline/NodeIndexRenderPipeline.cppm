module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.NodeIndexRenderPipeline;

import std;
export import fastgltf;
import vku;

import vk_gltf_viewer.shader.node_index_frag;
import vk_gltf_viewer.shader.node_index_vert;
export import vk_gltf_viewer.vulkan.pipeline.PrepassPipelineConfig;
export import vk_gltf_viewer.vulkan.pipeline_layout.PrimitiveNoShading;
export import vk_gltf_viewer.vulkan.render_pass.MousePicking;
import vk_gltf_viewer.vulkan.specialization_constants.SpecializationMap;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class NodeIndexRenderPipeline final : public vk::raii::Pipeline {
    public:
        NodeIndexRenderPipeline(
            const vk::raii::Device &device LIFETIMEBOUND,
            const pl::PrimitiveNoShading &pipelineLayout LIFETIMEBOUND,
            const rp::MousePicking &renderPass LIFETIMEBOUND,
            const PrepassPipelineConfig<false> &config
        );

    private:
        struct VertexShaderSpecialization;

        [[nodiscard]] static VertexShaderSpecialization getVertexShaderSpecialization(const PrepassPipelineConfig<false> &config) noexcept;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

struct vk_gltf_viewer::vulkan::pipeline::NodeIndexRenderPipeline::VertexShaderSpecialization {
    std::uint32_t positionComponentType;
    vk::Bool32 positionNormalized;
    std::uint32_t positionMorphTargetCount;
    std::uint32_t skinAttributeCount;
};

vk_gltf_viewer::vulkan::pipeline::NodeIndexRenderPipeline::NodeIndexRenderPipeline(
    const vk::raii::Device &device,
    const pl::PrimitiveNoShading &pipelineLayout,
    const rp::MousePicking &renderPass,
    const PrepassPipelineConfig<false> &config
) : Pipeline { [&] -> Pipeline {
        return { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
            createPipelineStages(
                device,
                vku::Shader {
                    shader::node_index_vert,
                    vk::ShaderStageFlagBits::eVertex,
                    vku::unsafeAddress(vk::SpecializationInfo {
                        SpecializationMap<VertexShaderSpecialization>::value,
                        vku::unsafeProxy(getVertexShaderSpecialization(config)),
                    }),
                },
                vku::Shader { shader::node_index_frag, vk::ShaderStageFlagBits::eFragment }).get(),
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
            }))
            .setRenderPass(*renderPass)
            .setSubpass(0),
        };
    }() } { }

[[nodiscard]] vk_gltf_viewer::vulkan::pipeline::NodeIndexRenderPipeline::VertexShaderSpecialization vk_gltf_viewer::vulkan::pipeline::NodeIndexRenderPipeline::getVertexShaderSpecialization(const PrepassPipelineConfig<false> &config) noexcept {
    return {
        .positionComponentType = getGLComponentType(config.positionComponentType),
        .positionNormalized = config.positionNormalized,
        .positionMorphTargetCount = config.positionMorphTargetCount,
        .skinAttributeCount = config.skinAttributeCount,
    };
}