export module vk_gltf_viewer:vulkan.pipeline.NodeIndexRenderer;

import std;

import vk_gltf_viewer.helpers;
import vk_gltf_viewer.shader.mask_node_index_frag;
import vk_gltf_viewer.shader.mask_node_index_vert;
import vk_gltf_viewer.shader.node_index_frag;
import vk_gltf_viewer.shader.node_index_vert;
import vku;
export import :helpers.vulkan;
export import :vulkan.pl.PrimitiveNoShading;
export import :vulkan.rp.MousePicking;
import :vulkan.specialization_constants.SpecializationMap;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class NodeIndexRendererSpecialization {
    public:
        TopologyClass topologyClass;
        std::uint8_t positionComponentType = 0;
        std::uint32_t positionMorphTargetWeightCount = 0;
        std::uint32_t skinAttributeCount = 0;

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
                }))
                .setRenderPass(*renderPass)
                .setSubpass(0),
            };
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

    export class MaskNodeIndexRendererSpecialization {
    public:
        TopologyClass topologyClass;
        std::uint8_t positionComponentType;
        std::optional<std::uint8_t> baseColorTexcoordComponentType;
        std::optional<std::uint8_t> colorAlphaComponentType;
        std::uint32_t positionMorphTargetWeightCount = 0;
        std::uint32_t skinAttributeCount = 0;
        bool baseColorTextureTransform = false;

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
                        shader::mask_node_index_vert,
                        vk::ShaderStageFlagBits::eVertex,
                        vku::unsafeAddress(vk::SpecializationInfo {
                            SpecializationMap<VertexShaderSpecializationData>::value,
                            vku::unsafeProxy(getVertexShaderSpecializationData()),
                        }),
                        std::format("main_{:n:}", join<'_'>(getVertexShaderVariants())).c_str(),
                    },
                    vku::Shader {
                        shader::mask_node_index_frag,
                        vk::ShaderStageFlagBits::eFragment,
                        vku::unsafeAddress(vk::SpecializationInfo {
                            SpecializationMap<FragmentShaderSpecializationData>::value,
                            vku::unsafeProxy(getFragmentShaderSpecializationData()),
                        }),
                        std::format("main_{:n:}", join<'_'>(getFragmentShaderVariants())).c_str(),
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
                }))
                .setRenderPass(*renderPass)
                .setSubpass(0),
            };
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