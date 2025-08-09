module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.NodeMousePickingRenderPipeline;

import std;
import vku;

import vk_gltf_viewer.shader.node_mouse_picking_frag;
import vk_gltf_viewer.shader.node_mouse_picking_vert;
import vk_gltf_viewer.shader_selector.mask_node_mouse_picking_frag;
import vk_gltf_viewer.shader_selector.mask_node_mouse_picking_vert;
export import vk_gltf_viewer.vulkan.pipeline.PrepassPipelineConfig;
export import vk_gltf_viewer.vulkan.pipeline_layout.MousePicking;
import vk_gltf_viewer.vulkan.specialization_constants.SpecializationMap;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export template <bool Mask>
    class NodeMousePickingRenderPipeline;
    
    export template <>
    class NodeMousePickingRenderPipeline<false> final : public vk::raii::Pipeline {
    public:
        NodeMousePickingRenderPipeline(
            const vk::raii::Device &device LIFETIMEBOUND,
            const pl::MousePicking &pipelineLayout LIFETIMEBOUND,
            const PrepassPipelineConfig<false> &config
        );

    private:
        struct VertexShaderSpecialization;

        [[nodiscard]] static VertexShaderSpecialization getVertexShaderSpecialization(const PrepassPipelineConfig<false> &config) noexcept;
    };
    
    export template <>
    class NodeMousePickingRenderPipeline<true> final : public vk::raii::Pipeline {
    public:
        NodeMousePickingRenderPipeline(
            const vk::raii::Device &device LIFETIMEBOUND,
            const pl::MousePicking &pipelineLayout LIFETIMEBOUND,
            const PrepassPipelineConfig<true> &config
        );

    private:
        struct VertexShaderSpecialization;
        struct FragmentShaderSpecialization;

        [[nodiscard]] static std::array<int, 2> getVertexShaderVariants(const PrepassPipelineConfig<true> &config) noexcept;
        [[nodiscard]] static VertexShaderSpecialization getVertexShaderSpecialization(const PrepassPipelineConfig<true> &config) noexcept;
        [[nodiscard]] static std::array<int, 2> getFragmentShaderVariants(const PrepassPipelineConfig<true> &config) noexcept;
        [[nodiscard]] static FragmentShaderSpecialization getFragmentShaderSpecialization(const PrepassPipelineConfig<true> &config) noexcept;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

// ----- NodeMousePickingRenderPipeline<false> -----

struct vk_gltf_viewer::vulkan::pipeline::NodeMousePickingRenderPipeline<false>::VertexShaderSpecialization {
    std::uint32_t positionComponentType;
    vk::Bool32 positionNormalized;
    std::uint32_t positionMorphTargetCount;
    std::uint32_t skinAttributeCount;
};

vk_gltf_viewer::vulkan::pipeline::NodeMousePickingRenderPipeline<false>::NodeMousePickingRenderPipeline(
    const vk::raii::Device &device,
    const pl::MousePicking &pipelineLayout,
    const PrepassPipelineConfig<false> &config
) : Pipeline { [&] -> Pipeline {
        return { device, nullptr, vk::StructureChain {
            vku::getDefaultGraphicsPipelineCreateInfo(
                createPipelineStages(
                    device,
                    vku::Shader {
                        shader::node_mouse_picking_vert,
                        vk::ShaderStageFlagBits::eVertex,
                        vku::unsafeAddress(vk::SpecializationInfo {
                            SpecializationMap<VertexShaderSpecialization>::value,
                            vku::unsafeProxy(getVertexShaderSpecialization(config)),
                        }),
                    },
                    vku::Shader { shader::node_mouse_picking_frag, vk::ShaderStageFlagBits::eFragment }).get(),
                *pipelineLayout, 0, true)
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
                {},
                vk::Format::eD32Sfloat,
            },
        }.get() };
    }() } { }

[[nodiscard]] auto vk_gltf_viewer::vulkan::pipeline::NodeMousePickingRenderPipeline<false>::getVertexShaderSpecialization(
    const PrepassPipelineConfig<false> &config
) noexcept -> VertexShaderSpecialization {
    return {
        .positionComponentType = getGLComponentType(config.positionComponentType),
        .positionNormalized = config.positionNormalized,
        .positionMorphTargetCount = config.positionMorphTargetCount,
        .skinAttributeCount = config.skinAttributeCount,
    };
}

// ----- NodeMousePickingRenderPipeline<true> -----

struct vk_gltf_viewer::vulkan::pipeline::NodeMousePickingRenderPipeline<true>::VertexShaderSpecialization {
    std::uint32_t positionComponentType;
    vk::Bool32 positionNormalized;
    std::uint32_t baseColorTexcoordComponentType;
    vk::Bool32 baseColorTexcoordNormalized;
    std::uint32_t color0ComponentType;
    std::uint32_t positionMorphTargetCount;
    std::uint32_t skinAttributeCount;
};

struct vk_gltf_viewer::vulkan::pipeline::NodeMousePickingRenderPipeline<true>::FragmentShaderSpecialization {
    vk::Bool32 useTextureTransform;
};

vk_gltf_viewer::vulkan::pipeline::NodeMousePickingRenderPipeline<true>::NodeMousePickingRenderPipeline(
    const vk::raii::Device &device,
    const pl::MousePicking &pipelineLayout,
    const PrepassPipelineConfig<true> &config
) : Pipeline { [&] -> Pipeline {
        return { device, nullptr, vk::StructureChain {
            vku::getDefaultGraphicsPipelineCreateInfo(
                createPipelineStages(
                    device,
                    vku::Shader {
                        std::apply(LIFT(shader_selector::mask_node_mouse_picking_vert), getVertexShaderVariants(config)),
                        vk::ShaderStageFlagBits::eVertex,
                        vku::unsafeAddress(vk::SpecializationInfo {
                            SpecializationMap<VertexShaderSpecialization>::value,
                            vku::unsafeProxy(getVertexShaderSpecialization(config)),
                        }),
                    },
                    vku::Shader {
                        std::apply(LIFT(shader_selector::mask_node_mouse_picking_frag), getFragmentShaderVariants(config)),
                        vk::ShaderStageFlagBits::eFragment,
                        vku::unsafeAddress(vk::SpecializationInfo {
                            SpecializationMap<FragmentShaderSpecialization>::value,
                            vku::unsafeProxy(getFragmentShaderSpecialization(config)),
                        }),
                    }).get(),
                *pipelineLayout, 0, true)
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
                {},
                vk::Format::eD32Sfloat,
            },
        }.get() };
    }() } { }

std::array<int, 2> vk_gltf_viewer::vulkan::pipeline::NodeMousePickingRenderPipeline<true>::getVertexShaderVariants(
    const PrepassPipelineConfig<true> &config
) noexcept {
    return {
        config.baseColorTexcoordComponentTypeAndNormalized.has_value(),
        config.color0AlphaComponentType.has_value(),
    };
}

auto vk_gltf_viewer::vulkan::pipeline::NodeMousePickingRenderPipeline<true>::getVertexShaderSpecialization(
    const PrepassPipelineConfig<true> &config
) noexcept -> VertexShaderSpecialization {
    VertexShaderSpecialization result {
        .positionComponentType = getGLComponentType(config.positionComponentType),
        .positionNormalized = config.positionNormalized,
        .color0ComponentType = config.color0AlphaComponentType.transform(fastgltf::getGLComponentType).value_or(0U),
        .positionMorphTargetCount = config.positionMorphTargetCount,
        .skinAttributeCount = config.skinAttributeCount,
    };

    if (config.baseColorTexcoordComponentTypeAndNormalized) {
        result.baseColorTexcoordComponentType = getGLComponentType(config.baseColorTexcoordComponentTypeAndNormalized->first);
        result.baseColorTexcoordNormalized = config.baseColorTexcoordComponentTypeAndNormalized->second;
    }

    return result;
}

std::array<int, 2> vk_gltf_viewer::vulkan::pipeline::NodeMousePickingRenderPipeline<true>::getFragmentShaderVariants(
    const PrepassPipelineConfig<true> &config
) noexcept {
    return {
        config.baseColorTexcoordComponentTypeAndNormalized.has_value(),
        config.color0AlphaComponentType.has_value(),
    };
}

auto vk_gltf_viewer::vulkan::pipeline::NodeMousePickingRenderPipeline<true>::getFragmentShaderSpecialization(
    const PrepassPipelineConfig<true> &config
) noexcept -> FragmentShaderSpecialization {
    return { config.useTextureTransform };
}

// Explicit template instantiations.
extern template class vk_gltf_viewer::vulkan::pipeline::NodeMousePickingRenderPipeline<false>;
extern template class vk_gltf_viewer::vulkan::pipeline::NodeMousePickingRenderPipeline<true>;