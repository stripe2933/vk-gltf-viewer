module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline.JumpFloodSeedRenderPipeline;

import std;
import vku;

import vk_gltf_viewer.shader.jump_flood_seed_frag;
import vk_gltf_viewer.shader.jump_flood_seed_vert;
import vk_gltf_viewer.shader_selector.mask_jump_flood_seed_frag;
import vk_gltf_viewer.shader_selector.mask_jump_flood_seed_vert;
export import vk_gltf_viewer.vulkan.pipeline.PrepassPipelineConfig;
export import vk_gltf_viewer.vulkan.pipeline_layout.PrimitiveNoShading;
import vk_gltf_viewer.vulkan.specialization_constants.SpecializationMap;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export template <bool Mask>
    class JumpFloodSeedRenderPipeline;
    
    export template <>
    class JumpFloodSeedRenderPipeline<false> final : public vk::raii::Pipeline {
    public:
        JumpFloodSeedRenderPipeline(
            const vk::raii::Device &device LIFETIMEBOUND,
            const pl::PrimitiveNoShading &pipelineLayout LIFETIMEBOUND,
            const PrepassPipelineConfig<false> &config,
            std::uint32_t viewMask
        );

    private:
        struct VertexShaderSpecialization;

        [[nodiscard]] static VertexShaderSpecialization getVertexShaderSpecialization(const PrepassPipelineConfig<false> &config) noexcept;
    };
    
    export template <>
    class JumpFloodSeedRenderPipeline<true> final : public vk::raii::Pipeline {
    public:
        JumpFloodSeedRenderPipeline(
            const vk::raii::Device &device LIFETIMEBOUND,
            const pl::PrimitiveNoShading &pipelineLayout LIFETIMEBOUND,
            const PrepassPipelineConfig<true> &config,
            std::uint32_t viewMask
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

// ----- JumpFloodSeedRenderPipeline<false> -----

struct vk_gltf_viewer::vulkan::pipeline::JumpFloodSeedRenderPipeline<false>::VertexShaderSpecialization {
    std::uint32_t positionComponentType;
    vk::Bool32 positionNormalized;
    std::uint32_t positionMorphTargetCount;
    std::uint32_t skinAttributeCount;
};

vk_gltf_viewer::vulkan::pipeline::JumpFloodSeedRenderPipeline<false>::JumpFloodSeedRenderPipeline(
    const vk::raii::Device &device,
    const pl::PrimitiveNoShading &pipelineLayout,
    const PrepassPipelineConfig<false> &config,
    std::uint32_t viewMask
) : Pipeline { device, nullptr, vk::StructureChain {
        vk::GraphicsPipelineCreateInfo {
            {},
            vku::lvalue({
                vk::PipelineShaderStageCreateInfo {
                    {},
                    vk::ShaderStageFlagBits::eVertex,
                    *vku::lvalue(vk::raii::ShaderModule { device, vk::ShaderModuleCreateInfo {
                        {},
                        shader::jump_flood_seed_vert,
                    } }),
                    "main",
                    &vku::lvalue(vk::SpecializationInfo {
                        SpecializationMap<VertexShaderSpecialization>::value,
                        vk::ArrayProxyNoTemporaries<const VertexShaderSpecialization> { vku::lvalue(getVertexShaderSpecialization(config)) },
                    }),
                },
                vk::PipelineShaderStageCreateInfo {
                    {},
                    vk::ShaderStageFlagBits::eFragment,
                    *vku::lvalue(vk::raii::ShaderModule { device, vk::ShaderModuleCreateInfo {
                        {},
                        shader::jump_flood_seed_frag,
                    } }),
                    "main",
                },
            }),
            &vku::lvalue(vk::PipelineVertexInputStateCreateInfo{}),
            &vku::lvalue(vku::defaultPipelineInputAssemblyState(vku::getListPrimitiveTopology(config.topologyClass.value_or(vku::TopologyClass::eTriangle)))),
            nullptr,
            &vku::lvalue(vk::PipelineViewportStateCreateInfo {
                {},
                1, nullptr,
                1, nullptr,
            }),
            &vku::lvalue(vku::defaultPipelineRasterizationState({}, vk::CullModeFlagBits::eBack)),
            &vku::lvalue(vk::PipelineMultisampleStateCreateInfo { {}, vk::SampleCountFlagBits::e1 }),
            &vku::lvalue(vk::PipelineDepthStencilStateCreateInfo {
                {},
                true, true, vk::CompareOp::eGreater, // Use reverse Z.
            }),
            &vku::lvalue(vku::defaultPipelineColorBlendState(1)),
            &vku::lvalue(vk::PipelineDynamicStateCreateInfo {
                {},
                vku::lvalue({
                    vk::DynamicState::eViewport,
                    vk::DynamicState::eScissor,
                    vk::DynamicState::ePrimitiveTopology,
                    vk::DynamicState::eCullMode,
                }),
            }),
            *pipelineLayout,
        },
        vk::PipelineRenderingCreateInfo {
            viewMask,
            vku::lvalue(vk::Format::eR16G16Uint),
            vk::Format::eD32Sfloat,
        }
    }.get() } { }

[[nodiscard]] auto vk_gltf_viewer::vulkan::pipeline::JumpFloodSeedRenderPipeline<false>::getVertexShaderSpecialization(
    const PrepassPipelineConfig<false> &config
) noexcept -> VertexShaderSpecialization {
    return {
        .positionComponentType = getGLComponentType(config.positionComponentType),
        .positionNormalized = config.positionNormalized,
        .positionMorphTargetCount = config.positionMorphTargetCount,
        .skinAttributeCount = config.skinAttributeCount,
    };
}

// ----- JumpFloodSeedRenderPipeline<true> -----

struct vk_gltf_viewer::vulkan::pipeline::JumpFloodSeedRenderPipeline<true>::VertexShaderSpecialization {
    std::uint32_t positionComponentType;
    vk::Bool32 positionNormalized;
    std::uint32_t baseColorTexcoordComponentType;
    vk::Bool32 baseColorTexcoordNormalized;
    std::uint32_t color0ComponentType;
    std::uint32_t positionMorphTargetCount;
    std::uint32_t skinAttributeCount;
};

struct vk_gltf_viewer::vulkan::pipeline::JumpFloodSeedRenderPipeline<true>::FragmentShaderSpecialization {
    vk::Bool32 useTextureTransform;
};

vk_gltf_viewer::vulkan::pipeline::JumpFloodSeedRenderPipeline<true>::JumpFloodSeedRenderPipeline::JumpFloodSeedRenderPipeline(
    const vk::raii::Device &device,
    const pl::PrimitiveNoShading &pipelineLayout,
    const PrepassPipelineConfig<true> &config,
    std::uint32_t viewMask
) : Pipeline { device, nullptr, vk::StructureChain {
        vk::GraphicsPipelineCreateInfo {
            {},
            vku::lvalue({
                vk::PipelineShaderStageCreateInfo {
                    {},
                    vk::ShaderStageFlagBits::eVertex,
                    *vku::lvalue(vk::raii::ShaderModule { device, vk::ShaderModuleCreateInfo {
                        {},
                        vku::lvalue(std::apply(LIFT(shader_selector::mask_jump_flood_seed_vert), getVertexShaderVariants(config))),
                    } }),
                    "main",
                    &vku::lvalue(vk::SpecializationInfo {
                        SpecializationMap<VertexShaderSpecialization>::value,
                        vk::ArrayProxyNoTemporaries<const VertexShaderSpecialization> { vku::lvalue(getVertexShaderSpecialization(config)) },
                    }),
                },
                vk::PipelineShaderStageCreateInfo {
                    {},
                    vk::ShaderStageFlagBits::eFragment,
                    *vku::lvalue(vk::raii::ShaderModule { device, vk::ShaderModuleCreateInfo {
                        {},
                        vku::lvalue(std::apply(LIFT(shader_selector::mask_jump_flood_seed_frag), getFragmentShaderVariants(config))),
                    } }),
                    "main",
                    &vku::lvalue(vk::SpecializationInfo {
                        SpecializationMap<FragmentShaderSpecialization>::value,
                        vk::ArrayProxyNoTemporaries<const FragmentShaderSpecialization> { vku::lvalue(getFragmentShaderSpecialization(config)) },
                    }),
                },
            }),
            &vku::lvalue(vk::PipelineVertexInputStateCreateInfo{}),
            &vku::lvalue(vku::defaultPipelineInputAssemblyState(vku::getListPrimitiveTopology(config.topologyClass.value_or(vku::TopologyClass::eTriangle)))),
            nullptr,
            &vku::lvalue(vk::PipelineViewportStateCreateInfo {
                {},
                1, nullptr,
                1, nullptr,
            }),
            &vku::lvalue(vku::defaultPipelineRasterizationState({}, vk::CullModeFlagBits::eBack)),
            &vku::lvalue(vk::PipelineMultisampleStateCreateInfo { {}, vk::SampleCountFlagBits::e1 }),
            &vku::lvalue(vk::PipelineDepthStencilStateCreateInfo {
                {},
                true, true, vk::CompareOp::eGreater, // Use reverse Z.
            }),
            &vku::lvalue(vku::defaultPipelineColorBlendState(1)),
            &vku::lvalue(vk::PipelineDynamicStateCreateInfo {
                {},
                vku::lvalue({
                    vk::DynamicState::eViewport,
                    vk::DynamicState::eScissor,
                    vk::DynamicState::ePrimitiveTopology,
                    vk::DynamicState::eCullMode,
                }),
            }),
            *pipelineLayout,
        },
        vk::PipelineRenderingCreateInfo {
            viewMask,
            vku::lvalue(vk::Format::eR16G16Uint),
            vk::Format::eD32Sfloat,
        }
    }.get() } { }

std::array<int, 2> vk_gltf_viewer::vulkan::pipeline::JumpFloodSeedRenderPipeline<true>::getVertexShaderVariants(
    const PrepassPipelineConfig<true> &config
) noexcept {
    return {
        config.baseColorTexcoordComponentTypeAndNormalized.has_value(),
        config.color0AlphaComponentType.has_value(),
    };
}

auto vk_gltf_viewer::vulkan::pipeline::JumpFloodSeedRenderPipeline<true>::getVertexShaderSpecialization(
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

std::array<int, 2> vk_gltf_viewer::vulkan::pipeline::JumpFloodSeedRenderPipeline<true>::getFragmentShaderVariants(
    const PrepassPipelineConfig<true> &config
) noexcept {
    return {
        config.baseColorTexcoordComponentTypeAndNormalized.has_value(),
        config.color0AlphaComponentType.has_value(),
    };
}

auto vk_gltf_viewer::vulkan::pipeline::JumpFloodSeedRenderPipeline<true>::getFragmentShaderSpecialization(
    const PrepassPipelineConfig<true> &config
) noexcept -> FragmentShaderSpecialization {
    return { config.useTextureTransform };
}

// Explicit template instantiations.
extern template class vk_gltf_viewer::vulkan::pipeline::JumpFloodSeedRenderPipeline<false>;
extern template class vk_gltf_viewer::vulkan::pipeline::JumpFloodSeedRenderPipeline<true>;