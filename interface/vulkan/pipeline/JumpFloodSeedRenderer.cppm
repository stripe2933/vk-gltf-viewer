export module vk_gltf_viewer:vulkan.pipeline.JumpFloodSeedRenderer;

import std;
import vku;
export import :helpers.vulkan;
import :shader.jump_flood_seed_vert;
import :shader.jump_flood_seed_frag;
import :shader_selector.mask_jump_flood_seed_vert;
import :shader_selector.mask_jump_flood_seed_frag;
export import :vulkan.pl.PrimitiveNoShading;
import :vulkan.specialization_constants.SpecializationMap;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [&](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class JumpFloodSeedRendererSpecialization {
    public:
        TopologyClass topologyClass;
        std::uint8_t positionComponentType = 0;
        std::uint32_t positionMorphTargetWeightCount = 0;
        std::uint32_t skinAttributeCount = 0;

        [[nodiscard]] bool operator==(const JumpFloodSeedRendererSpecialization&) const = default;

        [[nodiscard]] vk::raii::Pipeline createPipeline(
            const vk::raii::Device &device,
            const pl::PrimitiveNoShading &pipelineLayout
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
                        [this]() {
                            switch (topologyClass) {
                                case TopologyClass::Point: return vk::PrimitiveTopology::ePointList;
                                case TopologyClass::Line: return vk::PrimitiveTopology::eLineList;
                                case TopologyClass::Triangle: return vk::PrimitiveTopology::eTriangleList;
                                case TopologyClass::Patch: return vk::PrimitiveTopology::ePatchList;
                            }
                            std::unreachable();
                        }(),
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

    private:
        struct VertexShaderSpecializationData {
            std::uint32_t positionComponentType;
            std::uint32_t positionMorphTargetWeightCount;
            std::uint32_t skinAttributeCount;
        };

        [[nodiscard]] VertexShaderSpecializationData getVertexShaderSpecializationData() const {
            return { positionComponentType, positionMorphTargetWeightCount, skinAttributeCount };
        }
    };

    export class MaskJumpFloodSeedRendererSpecialization {
    public:
        TopologyClass topologyClass;
        std::uint8_t positionComponentType;
        std::optional<std::uint8_t> baseColorTexcoordComponentType;
        std::optional<std::uint8_t> colorAlphaComponentType;
        std::uint32_t positionMorphTargetWeightCount = 0;
        std::uint32_t skinAttributeCount = 0;
        bool baseColorTextureTransform = false;

        [[nodiscard]] bool operator==(const MaskJumpFloodSeedRendererSpecialization&) const = default;

        [[nodiscard]] vk::raii::Pipeline createPipeline(
            const vk::raii::Device &device,
            const pl::PrimitiveNoShading &pipelineLayout
        ) const {
            return { device, nullptr, vk::StructureChain {
                vku::getDefaultGraphicsPipelineCreateInfo(
                    createPipelineStages(
                        device,
                        vku::Shader {
                            std::apply(LIFT(shader_selector::mask_jump_flood_seed_vert), getVertexShaderVariants()),
                            vk::ShaderStageFlagBits::eVertex,
                            vku::unsafeAddress(vk::SpecializationInfo {
                                SpecializationMap<VertexShaderSpecializationData>::value,
                                vku::unsafeProxy(getVertexShaderSpecializationData()),
                            }),
                        },
                        vku::Shader {
                            std::apply(LIFT(shader_selector::mask_jump_flood_seed_frag), getFragmentShaderVariants()),
                            vk::ShaderStageFlagBits::eFragment,
                            vku::unsafeAddress(vk::SpecializationInfo {
                                SpecializationMap<FragmentShaderSpecializationData>::value,
                                vku::unsafeProxy(getFragmentShaderSpecializationData()),
                            }),
                        }).get(),
                    *pipelineLayout, 1, true)
                    .setPInputAssemblyState(vku::unsafeAddress(vk::PipelineInputAssemblyStateCreateInfo {
                        {},
                        [this]() {
                            switch (topologyClass) {
                                case TopologyClass::Point: return vk::PrimitiveTopology::ePointList;
                                case TopologyClass::Line: return vk::PrimitiveTopology::eLineList;
                                case TopologyClass::Triangle: return vk::PrimitiveTopology::eTriangleList;
                                case TopologyClass::Patch: return vk::PrimitiveTopology::ePatchList;
                            }
                            std::unreachable();
                        }(),
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

    private:
        struct VertexShaderSpecializationData {
            std::uint32_t positionComponentType;
            std::uint32_t texcoordComponentType = 5126; // FLOAT
            std::uint32_t colorComponentType = 5126; // FLOAT
            std::uint32_t positionMorphTargetWeightCount;
            std::uint32_t skinAttributeCount;
        };

        struct FragmentShaderSpecializationData {
            vk::Bool32 baseColorTextureTransform;
        };

        [[nodiscard]] std::array<int, 2> getVertexShaderVariants() const noexcept {
            return {
                baseColorTexcoordComponentType.has_value(),
                colorAlphaComponentType.has_value(),
            };
        }

        [[nodiscard]] VertexShaderSpecializationData getVertexShaderSpecializationData() const {
            VertexShaderSpecializationData result {
                .positionComponentType = positionComponentType,
                .positionMorphTargetWeightCount = positionMorphTargetWeightCount,
                .skinAttributeCount = skinAttributeCount,
            };

            if (baseColorTexcoordComponentType) {
                result.texcoordComponentType = *baseColorTexcoordComponentType;
            }
            if (colorAlphaComponentType) {
                result.colorComponentType = *colorAlphaComponentType;
            }

            return result;
        }

        [[nodiscard]] std::array<int, 2> getFragmentShaderVariants() const noexcept {
            return {
                baseColorTexcoordComponentType.has_value(),
                colorAlphaComponentType.has_value(),
            };
        }

        [[nodiscard]] FragmentShaderSpecializationData getFragmentShaderSpecializationData() const {
            return {
                .baseColorTextureTransform = baseColorTextureTransform,
            };
        }
    };
}