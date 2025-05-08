module;

#include <cassert>

#include <boost/container/static_vector.hpp>
#include <boost/container_hash/hash.hpp>

export module vk_gltf_viewer:vulkan.pipeline.PrimitiveRenderer;

import std;
export import fastgltf;
import vk_gltf_viewer.helpers;
import vk_gltf_viewer.shader.primitive_frag;
import vk_gltf_viewer.shader.primitive_vert;
import vku;
import :helpers.ranges;
export import :helpers.vulkan;
export import :vulkan.pl.Primitive;
export import :vulkan.rp.Scene;
import :vulkan.specialization_constants.SpecializationMap;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class PrimitiveRendererSpecialization {
    public:
        TopologyClass topologyClass;
        std::uint8_t positionComponentType;
        std::optional<std::uint8_t> normalComponentType;
        std::optional<std::uint8_t> tangentComponentType;
        boost::container::static_vector<std::uint8_t, 4> texcoordComponentTypes;
        std::optional<std::pair<std::uint8_t, std::uint8_t>> colorComponentCountAndType;
        bool fragmentShaderGeneratedTBN;
        std::uint32_t morphTargetWeightCount = 0;
        bool hasPositionMorphTarget = false;
        bool hasNormalMorphTarget = false;
        bool hasTangentMorphTarget = false;
        std::uint32_t skinAttributeCount = 0;
        bool baseColorTextureTransform = false;
        bool metallicRoughnessTextureTransform = false;
        bool normalTextureTransform = false;
        bool occlusionTextureTransform = false;
        bool emissiveTextureTransform = false;
        fastgltf::AlphaMode alphaMode;

        [[nodiscard]] bool operator==(const PrimitiveRendererSpecialization&) const noexcept = default;

        [[nodiscard]] vk::raii::Pipeline createPipeline(
            const vk::raii::Device &device,
            const pl::Primitive &pipelineLayout,
            const rp::Scene &sceneRenderPass
        ) const {
            const auto vertexShaderSpecializationData = getVertexShaderSpecializationData();
            const vk::SpecializationInfo vertexShaderSpecializationInfo {
                SpecializationMap<VertexShaderSpecializationData>::value,
                vk::ArrayProxyNoTemporaries<const VertexShaderSpecializationData> { vertexShaderSpecializationData },
            };

            const auto fragmentShaderSpecializationData = getFragmentShaderSpecializationData();
            const vk::SpecializationInfo fragmentShaderSpecializationInfo {
                SpecializationMap<FragmentShaderSpecializationData>::value,
                vk::ArrayProxyNoTemporaries<const FragmentShaderSpecializationData> { fragmentShaderSpecializationData },
            };

            const std::string vertexShaderEntrypoint = std::format("main_{:n:}", join<'_'>(getVertexShaderVariants()));
            const std::string fragmentShaderEntrypoint = std::format("main_{:n:}", join<'_'>(getFragmentShaderVariants()));
            const vku::RefHolder pipelineStages = createPipelineStages(
                device,
                vku::Shader {
                    shader::primitive_vert,
                    vk::ShaderStageFlagBits::eVertex,
                    &vertexShaderSpecializationInfo,
                    vertexShaderEntrypoint.c_str(),
                },
                vku::Shader {
                    shader::primitive_frag,
                    vk::ShaderStageFlagBits::eFragment,
                    &fragmentShaderSpecializationInfo,
                    fragmentShaderEntrypoint.c_str(),
                });

            const vk::PipelineInputAssemblyStateCreateInfo inputAssemblyStateCreateInfo {
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
            };

            switch (alphaMode) {
                case fastgltf::AlphaMode::Opaque:
                    return { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                        pipelineStages.get(), *pipelineLayout, 1, true, vk::SampleCountFlagBits::e4)
                        .setPInputAssemblyState(&inputAssemblyStateCreateInfo)
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
                        .setRenderPass(*sceneRenderPass)
                        .setSubpass(0)
                    };
                case fastgltf::AlphaMode::Mask:
                    return { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                        pipelineStages.get(), *pipelineLayout, 1, true, vk::SampleCountFlagBits::e4)
                        .setPInputAssemblyState(&inputAssemblyStateCreateInfo)
                        .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                            {},
                            true, true, vk::CompareOp::eGreater, // Use reverse Z.
                        }))
                        .setPMultisampleState(vku::unsafeAddress(vk::PipelineMultisampleStateCreateInfo {
                            {},
                            vk::SampleCountFlagBits::e4,
                            {}, {}, {},
                            true,
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
                        .setRenderPass(*sceneRenderPass)
                        .setSubpass(0)
                    };
                case fastgltf::AlphaMode::Blend:
                    return { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                        pipelineStages.get(), *pipelineLayout, 1, true, vk::SampleCountFlagBits::e4)
                        .setPInputAssemblyState(&inputAssemblyStateCreateInfo)
                        .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
                            {},
                            false, false,
                            vk::PolygonMode::eFill,
                            // Translucent objects' back faces shouldn't be culled.
                            vk::CullModeFlagBits::eNone, {},
                            false, {}, {}, {},
                            1.f,
                        }))
                        .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                            {},
                            // Translucent objects shouldn't interfere with the pre-rendered depth buffer. Use reverse Z.
                            true, false, vk::CompareOp::eGreater,
                        }))
                        .setPColorBlendState(vku::unsafeAddress(vk::PipelineColorBlendStateCreateInfo {
                            {},
                            false, {},
                            vku::unsafeProxy({
                                vk::PipelineColorBlendAttachmentState {
                                    true,
                                    vk::BlendFactor::eOne, vk::BlendFactor::eOne, vk::BlendOp::eAdd,
                                    vk::BlendFactor::eOne, vk::BlendFactor::eOne, vk::BlendOp::eAdd,
                                    vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
                                },
                                vk::PipelineColorBlendAttachmentState {
                                    true,
                                    vk::BlendFactor::eZero, vk::BlendFactor::eOneMinusSrcColor, vk::BlendOp::eAdd,
                                    vk::BlendFactor::eZero, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                                    vk::ColorComponentFlagBits::eR,
                                },
                            }),
                            { 1.f, 1.f, 1.f, 1.f },
                        }))
                        .setPDynamicState(vku::unsafeAddress(vk::PipelineDynamicStateCreateInfo {
                            {},
                            vku::unsafeProxy({
                                vk::DynamicState::eViewport,
                                vk::DynamicState::eScissor,
                                vk::DynamicState::ePrimitiveTopology,
                            }),
                        }))
                        .setRenderPass(*sceneRenderPass)
                        .setSubpass(1)
                    };
            }
            std::unreachable();
        }

    private:
        struct VertexShaderSpecializationData {
            std::uint32_t packedAttributeComponentTypes;
            std::uint32_t colorComponentCount;
            std::uint32_t morphTargetWeightCount;
            std::uint32_t packedMorphTargetAvailability;
            std::uint32_t skinAttributeCount;
        };

        struct FragmentShaderSpecializationData {
            std::uint32_t packedTextureTransforms;
        };

        [[nodiscard]] std::array<int, 3> getVertexShaderVariants() const noexcept {
            return {
                static_cast<int>(texcoordComponentTypes.size()),
                colorComponentCountAndType.has_value(),
                fragmentShaderGeneratedTBN,
            };
        }

        [[nodiscard]] VertexShaderSpecializationData getVertexShaderSpecializationData() const {
            VertexShaderSpecializationData result {
                .morphTargetWeightCount = morphTargetWeightCount,
                .packedMorphTargetAvailability = (hasPositionMorphTarget ? 1U : 0U)
                                               | (hasNormalMorphTarget ? 2U : 0U)
                                               | (hasTangentMorphTarget ? 4U : 0U),
                .skinAttributeCount = skinAttributeCount,
            };

            // Packed components types are:
            // PCT & 0xF -> POSITION
            // (PCT >> 4) & 0xF -> NORMAL
            // (PCT >> 8) & 0xF -> TANGENT
            // (PCT >> 12) & 0xF -> TEXCOORD_0 // <- TEXCOORD_<i> attributes starts from third.
            // (PCT >> 16) & 0xF -> TEXCOORD_1
            // (PCT >> 20) & 0xF -> TEXCOORD_2
            // (PCT >> 24) & 0xF -> TEXCOORD_3
            // (PCT >> 28) & 0xF -> COLOR_0
            result.packedAttributeComponentTypes |= static_cast<std::uint32_t>(positionComponentType);
            if (normalComponentType) {
                result.packedAttributeComponentTypes |= static_cast<std::uint32_t>(*normalComponentType) << 4;
            }
            if (tangentComponentType) {
                result.packedAttributeComponentTypes |= static_cast<std::uint32_t>(*tangentComponentType) << 8;
            }

            for (auto [i, componentType] : std::views::zip(std::views::iota(3), texcoordComponentTypes)) {
                result.packedAttributeComponentTypes |= static_cast<std::uint32_t>(componentType) << (4 * i);
            }

            if (colorComponentCountAndType) {
                assert(ranges::one_of(colorComponentCountAndType->first, { 3, 4 }));
                result.colorComponentCount = colorComponentCountAndType->first;
                result.packedAttributeComponentTypes |= static_cast<std::uint32_t>(colorComponentCountAndType->second) << 28;
            }

            return result;
        }

        [[nodiscard]] std::array<int, 4> getFragmentShaderVariants() const noexcept {
            return {
                static_cast<int>(texcoordComponentTypes.size()),
                colorComponentCountAndType.has_value(),
                fragmentShaderGeneratedTBN,
                static_cast<int>(alphaMode),
            };
        }

        [[nodiscard]] FragmentShaderSpecializationData getFragmentShaderSpecializationData() const {
            std::bitset<5> bits;
            bits.set(0, baseColorTextureTransform);
            bits.set(1, metallicRoughnessTextureTransform);
            bits.set(2, normalTextureTransform);
            bits.set(3, occlusionTextureTransform);
            bits.set(4, emissiveTextureTransform);

            return {
                .packedTextureTransforms = static_cast<std::uint32_t>(bits.to_ulong()),
            };
        }
    };
}