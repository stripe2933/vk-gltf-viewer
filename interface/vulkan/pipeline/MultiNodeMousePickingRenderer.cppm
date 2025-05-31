export module vk_gltf_viewer:vulkan.pipeline.MultiNodeMousePickingRenderer;

import std;
import vku;
import :shader.node_index_vert;
import :shader_selector.mask_node_index_vert;
import :shader.multi_node_mouse_picking_frag;
import :shader_selector.mask_multi_node_mouse_picking_frag;

export import vk_gltf_viewer.helpers.vulkan;
export import vk_gltf_viewer.vulkan.Gpu;
export import vk_gltf_viewer.vulkan.pl.MultiNodeMousePicking;
import vk_gltf_viewer.vulkan.specialization_constants.SpecializationMap;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [&](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class MultiNodeMousePickingRendererSpecialization {
    public:
        std::optional<TopologyClass> topologyClass;
        std::uint8_t positionComponentType = 0;
        std::uint32_t positionMorphTargetWeightCount = 0;
        std::uint32_t skinAttributeCount = 0;

        [[nodiscard]] bool operator==(const MultiNodeMousePickingRendererSpecialization&) const = default;

        [[nodiscard]] vk::raii::Pipeline createPipeline(
            const Gpu &gpu,
            const pl::MultiNodeMousePicking &pipelineLayout
        ) const {
            return { gpu.device, nullptr, vk::StructureChain {
                vku::getDefaultGraphicsPipelineCreateInfo(
                    createPipelineStages(
                        gpu.device,
                        vku::Shader {
                            shader::node_index_vert,
                            vk::ShaderStageFlagBits::eVertex,
                            vku::unsafeAddress(vk::SpecializationInfo {
                                SpecializationMap<VertexShaderSpecializationData>::value,
                                vku::unsafeProxy(getVertexShaderSpecializationData()),
                            }),
                        },
                        vku::Shader { shader::multi_node_mouse_picking_frag, vk::ShaderStageFlagBits::eFragment }).get(),
                    // See doc about Gpu::Workaround::attachmentLessRenderPass.
                    *pipelineLayout, 0, gpu.workaround.attachmentLessRenderPass)
                    .setPInputAssemblyState(vku::unsafeAddress(vk::PipelineInputAssemblyStateCreateInfo {
                        {},
                        topologyClass.transform(getRepresentativePrimitiveTopology).value_or(vk::PrimitiveTopology::eTriangleList),
                    }))
                    .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
                        {},
                        false, false,
                        vk::PolygonMode::eFill,
                        vk::CullModeFlagBits::eNone, {},
                        false, false, false, false,
                        1.f,
                    }))
                    .setPDynamicState(vku::unsafeAddress(vk::PipelineDynamicStateCreateInfo {
                        {},
                        vku::unsafeProxy({
                            vk::DynamicState::eViewport,
                            vk::DynamicState::eScissor,
                            vk::DynamicState::ePrimitiveTopology,
                        }),
                    })),
                vk::PipelineRenderingCreateInfo {
                    {},
                    {},
                    gpu.workaround.attachmentLessRenderPass ? vk::Format::eD32Sfloat : vk::Format::eUndefined,
                },
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

    export class MaskMultiNodeMousePickingRendererSpecialization {
    public:
        std::optional<TopologyClass> topologyClass;
        std::uint8_t positionComponentType;
        std::optional<std::uint8_t> baseColorTexcoordComponentType;
        std::optional<std::uint8_t> colorAlphaComponentType;
        std::uint32_t positionMorphTargetWeightCount = 0;
        std::uint32_t skinAttributeCount = 0;
        bool baseColorTextureTransform = false;

        [[nodiscard]] bool operator==(const MaskMultiNodeMousePickingRendererSpecialization&) const = default;

        [[nodiscard]] vk::raii::Pipeline createPipeline(
            const Gpu &gpu,
            const pl::MultiNodeMousePicking &pipelineLayout
        ) const {
            return { gpu.device, nullptr, vk::StructureChain {
                vku::getDefaultGraphicsPipelineCreateInfo(
                    createPipelineStages(
                        gpu.device,
                        vku::Shader {
                            std::apply(LIFT(shader_selector::mask_node_index_vert), getVertexShaderVariants()),
                            vk::ShaderStageFlagBits::eVertex,
                            vku::unsafeAddress(vk::SpecializationInfo {
                                SpecializationMap<VertexShaderSpecializationData>::value,
                                vku::unsafeProxy(getVertexShaderSpecializationData()),
                            }),
                        },
                        vku::Shader {
                            std::apply(LIFT(shader_selector::mask_multi_node_mouse_picking_frag), getFragmentShaderVariants()),
                            vk::ShaderStageFlagBits::eFragment,
                            vku::unsafeAddress(vk::SpecializationInfo {
                                SpecializationMap<FragmentShaderSpecializationData>::value,
                                vku::unsafeProxy(getFragmentShaderSpecializationData()),
                            }),
                        }).get(),
                    // See doc about Gpu::Workaround::attachmentLessRenderPass.
                    *pipelineLayout, 0, gpu.workaround.attachmentLessRenderPass)
                    .setPInputAssemblyState(vku::unsafeAddress(vk::PipelineInputAssemblyStateCreateInfo {
                        {},
                        topologyClass.transform(getRepresentativePrimitiveTopology).value_or(vk::PrimitiveTopology::eTriangleList),
                    }))
                    .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
                        {},
                        false, false,
                        vk::PolygonMode::eFill,
                        vk::CullModeFlagBits::eNone, {},
                        false, false, false, false,
                        1.f,
                    }))
                    .setPDynamicState(vku::unsafeAddress(vk::PipelineDynamicStateCreateInfo {
                        {},
                        vku::unsafeProxy({
                            vk::DynamicState::eViewport,
                            vk::DynamicState::eScissor,
                            vk::DynamicState::ePrimitiveTopology,
                        }),
                    })),
                vk::PipelineRenderingCreateInfo {
                    {},
                    {},
                    gpu.workaround.attachmentLessRenderPass ? vk::Format::eD32Sfloat : vk::Format::eUndefined,
                },
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