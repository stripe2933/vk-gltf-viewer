module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.NodeIndexRenderPipeline;

import std;
export import fastgltf;
import vku;

import vk_gltf_viewer.shader.node_index_frag;
import vk_gltf_viewer.shader.node_index_vert;
export import vk_gltf_viewer.vulkan.pl.PrimitiveNoShading;
export import vk_gltf_viewer.vulkan.rp.MousePicking;
import vk_gltf_viewer.vulkan.specialization_constants.SpecializationMap;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class NodeIndexRenderPipelineSpecialization {
    public:
        std::optional<vk::PrimitiveTopology> topologyClass; // Only list topology will be used in here.
        fastgltf::ComponentType positionComponentType;
        bool positionNormalized;
        std::uint32_t positionMorphTargetCount;
        std::uint32_t skinAttributeCount;

        [[nodiscard]] bool operator==(const NodeIndexRenderPipelineSpecialization&) const = default;

        [[nodiscard]] vk::raii::Pipeline createPipeline(
            const vk::raii::Device &device LIFETIMEBOUND,
            const pl::PrimitiveNoShading &pipelineLayout LIFETIMEBOUND,
            const rp::MousePicking &renderPass LIFETIMEBOUND
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

struct vk_gltf_viewer::vulkan::pipeline::NodeIndexRenderPipelineSpecialization::VertexShaderSpecializationData {
    std::uint32_t positionComponentType;
    vk::Bool32 positionNormalized;
    std::uint32_t positionMorphTargetCount;
    std::uint32_t skinAttributeCount;
};

[[nodiscard]] vk::raii::Pipeline vk_gltf_viewer::vulkan::pipeline::NodeIndexRenderPipelineSpecialization::createPipeline(
    const vk::raii::Device &device,
    const pl::PrimitiveNoShading &pipelineLayout,
    const rp::MousePicking &renderPass
) const {
    return { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
        createPipelineStages(
            device,
            vku::Shader {
                shader::node_index_vert,
                vk::ShaderStageFlagBits::eVertex,
                vku::unsafeAddress(vk::SpecializationInfo {
                    SpecializationMap<VertexShaderSpecializationData>::value,
                    vku::unsafeProxy(getVertexShaderSpecializationData()),
                }),
            },
            vku::Shader { shader::node_index_frag, vk::ShaderStageFlagBits::eFragment }).get(),
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
        }))
        .setRenderPass(*renderPass)
        .setSubpass(0),
    };
}

[[nodiscard]] vk_gltf_viewer::vulkan::pipeline::NodeIndexRenderPipelineSpecialization::VertexShaderSpecializationData vk_gltf_viewer::vulkan::pipeline::NodeIndexRenderPipelineSpecialization::getVertexShaderSpecializationData() const noexcept {
    return {
        .positionComponentType = getGLComponentType(positionComponentType),
        .positionNormalized = positionNormalized,
        .positionMorphTargetCount = positionMorphTargetCount,
        .skinAttributeCount = skinAttributeCount,
    };
}