export module vk_gltf_viewer:vulkan.pipeline.NodeIndexRenderer;

import std;
export import fastgltf;
import vku;
import :shader.node_index_frag;
import :shader.node_index_vert;
import :shader_selector.mask_node_index_frag;
import :shader_selector.mask_node_index_vert;

export import vk_gltf_viewer.vulkan.pl.PrimitiveNoShading;
export import vk_gltf_viewer.vulkan.rp.MousePicking;
import vk_gltf_viewer.vulkan.specialization_constants.SpecializationMap;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class NodeIndexRendererSpecialization {
    public:
        std::optional<vk::PrimitiveTopology> topologyClass; // Only list topology will be used in here.
        fastgltf::ComponentType positionComponentType;
        bool positionNormalized;
        std::uint32_t positionMorphTargetCount;
        std::uint32_t skinAttributeCount;

        [[nodiscard]] bool operator==(const NodeIndexRendererSpecialization&) const = default;

        [[nodiscard]] vk::raii::Pipeline createPipeline(
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

    private:
        struct VertexShaderSpecializationData {
            std::uint32_t positionComponentType;
            vk::Bool32 positionNormalized;
            std::uint32_t positionMorphTargetCount;
            std::uint32_t skinAttributeCount;
        };

        [[nodiscard]] VertexShaderSpecializationData getVertexShaderSpecializationData() const {
            return {
                .positionComponentType = getGLComponentType(positionComponentType),
                .positionNormalized = positionNormalized,
                .positionMorphTargetCount = positionMorphTargetCount,
                .skinAttributeCount = skinAttributeCount,
            };
        }
    };

    export class MaskNodeIndexRendererSpecialization {
    public:
        std::optional<vk::PrimitiveTopology> topologyClass; // Only list topology will be used in here.
        fastgltf::ComponentType positionComponentType;
        bool positionNormalized;
        std::optional<std::pair<fastgltf::ComponentType, bool>> baseColorTexcoordComponentTypeAndNormalized;
        std::optional<fastgltf::ComponentType> color0AlphaComponentType;
        std::uint32_t positionMorphTargetCount;
        std::uint32_t skinAttributeCount;
        bool baseColorTextureTransform;

        [[nodiscard]] bool operator==(const MaskNodeIndexRendererSpecialization&) const = default;

        [[nodiscard]] vk::raii::Pipeline createPipeline(
            const vk::raii::Device &device,
            const pl::PrimitiveNoShading &pipelineLayout,
            const rp::MousePicking &renderPass
        ) const {
            return { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                createPipelineStages(
                    device,
                    vku::Shader {
                        std::apply(LIFT(shader_selector::mask_node_index_vert), getVertexShaderVariants()),
                        vk::ShaderStageFlagBits::eVertex,
                        vku::unsafeAddress(vk::SpecializationInfo {
                            SpecializationMap<VertexShaderSpecializationData>::value,
                            vku::unsafeProxy(getVertexShaderSpecializationData()),
                        }),
                    },
                    vku::Shader {
                        std::apply(LIFT(shader_selector::mask_node_index_frag), getFragmentShaderVariants()),
                        vk::ShaderStageFlagBits::eFragment,
                        vku::unsafeAddress(vk::SpecializationInfo {
                            SpecializationMap<FragmentShaderSpecializationData>::value,
                            vku::unsafeProxy(getFragmentShaderSpecializationData()),
                        }),
                    }).get(),
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

    private:
        struct VertexShaderSpecializationData {
            std::uint32_t positionComponentType;
            vk::Bool32 positionNormalized;
            std::uint32_t baseColorTexcoordComponentType;
            vk::Bool32 baseColorTexcoordNormalized;
            std::uint32_t color0ComponentType;
            std::uint32_t positionMorphTargetCount;
            std::uint32_t skinAttributeCount;
        };

        struct FragmentShaderSpecializationData {
            vk::Bool32 baseColorTextureTransform;
        };

        [[nodiscard]] std::array<int, 2> getVertexShaderVariants() const noexcept {
            return {
                baseColorTexcoordComponentTypeAndNormalized.has_value(),
                color0AlphaComponentType.has_value(),
            };
        }

        [[nodiscard]] VertexShaderSpecializationData getVertexShaderSpecializationData() const {
            VertexShaderSpecializationData result {
                .positionComponentType = getGLComponentType(positionComponentType),
                .positionNormalized = positionNormalized,
                .color0ComponentType = color0AlphaComponentType.transform(fastgltf::getGLComponentType).value_or(0U),
                .positionMorphTargetCount = positionMorphTargetCount,
                .skinAttributeCount = skinAttributeCount,
            };

            if (baseColorTexcoordComponentTypeAndNormalized) {
                result.baseColorTexcoordComponentType = getGLComponentType(baseColorTexcoordComponentTypeAndNormalized->first);
                result.baseColorTexcoordNormalized = baseColorTexcoordComponentTypeAndNormalized->second;
            }

            return result;
        }

        [[nodiscard]] std::array<int, 2> getFragmentShaderVariants() const noexcept {
            return {
                baseColorTexcoordComponentTypeAndNormalized.has_value(),
                color0AlphaComponentType.has_value(),
            };
        }

        [[nodiscard]] FragmentShaderSpecializationData getFragmentShaderSpecializationData() const {
            return {
                .baseColorTextureTransform = baseColorTextureTransform,
            };
        }
    };
}