module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.MousePickingRenderer;

import std;
export import fastgltf;
import vku;

import vk_gltf_viewer.shader.mouse_picking_frag;
import vk_gltf_viewer.shader.node_index_vert;
export import vk_gltf_viewer.vulkan.pl.MousePicking;
import vk_gltf_viewer.vulkan.specialization_constants.SpecializationMap;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class MousePickingRendererSpecialization {
    public:
        std::optional<vk::PrimitiveTopology> topologyClass; // Only list topology will be used in here.
        fastgltf::ComponentType positionComponentType;
        bool positionNormalized;
        std::uint32_t positionMorphTargetCount;
        std::uint32_t skinAttributeCount;

        [[nodiscard]] bool operator==(const MousePickingRendererSpecialization&) const = default;

        [[nodiscard]] vk::raii::Pipeline createPipeline(
            const vk::raii::Device& device LIFETIMEBOUND,
            const pl::MousePicking& pipelineLayout LIFETIMEBOUND
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

struct vk_gltf_viewer::vulkan::pipeline::MousePickingRendererSpecialization::VertexShaderSpecializationData {
    std::uint32_t positionComponentType;
    vk::Bool32 positionNormalized;
    std::uint32_t positionMorphTargetCount;
    std::uint32_t skinAttributeCount;
};

vk::raii::Pipeline vk_gltf_viewer::vulkan::pipeline::MousePickingRendererSpecialization::createPipeline(
    const vk::raii::Device &device,
    const pl::MousePicking& pipelineLayout
) const {
    return { device, nullptr, vk::StructureChain {
        vku::getDefaultGraphicsPipelineCreateInfo(
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
                vku::Shader { shader::mouse_picking_frag, vk::ShaderStageFlagBits::eFragment }).get(),
                *pipelineLayout, 0)
                .setPInputAssemblyState(vku::unsafeAddress(vk::PipelineInputAssemblyStateCreateInfo {
                    {},
                    topologyClass.value_or(vk::PrimitiveTopology::eTriangleList),
                }))
                .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
                    {},
                    false, false,
                    vk::PolygonMode::eFill,
                    vk::CullModeFlagBits::eBack, {},
                    false, false, false, false,
                    1.f,
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
                {},
                vk::Format::eD32Sfloat,
            },
        }.get() };
}

vk_gltf_viewer::vulkan::pipeline::MousePickingRendererSpecialization::VertexShaderSpecializationData vk_gltf_viewer::vulkan::pipeline::MousePickingRendererSpecialization::getVertexShaderSpecializationData() const noexcept {
    return {
        .positionComponentType = getGLComponentType(positionComponentType),
        .positionNormalized = positionNormalized,
        .positionMorphTargetCount = positionMorphTargetCount,
        .skinAttributeCount = skinAttributeCount,
    };
}