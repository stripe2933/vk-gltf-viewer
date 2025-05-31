module;

#include <cassert>

#include <boost/container/static_vector.hpp>
#include <boost/container_hash/hash.hpp>

export module vk_gltf_viewer:vulkan.pipeline.PrimitiveRenderer;

import std;
export import fastgltf;
import vku;
import :shader_selector.primitive_vert;
import :shader_selector.primitive_frag;

import vk_gltf_viewer.helpers.ranges;
export import vk_gltf_viewer.helpers.vulkan;
export import vk_gltf_viewer.vulkan.pl.Primitive;
export import vk_gltf_viewer.vulkan.rp.Scene;
import vk_gltf_viewer.vulkan.specialization_constants.SpecializationMap;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [&](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class PrimitiveRendererSpecialization {
    public:
        std::optional<TopologyClass> topologyClass;
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
        bool usePerFragmentEmissiveStencilExport = false;

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

            const vku::RefHolder pipelineStages = createPipelineStages(
                device,
                vku::Shader {
                    std::apply(LIFT(shader_selector::primitive_vert), getVertexShaderVariants()),
                    vk::ShaderStageFlagBits::eVertex,
                    &vertexShaderSpecializationInfo,
                },
                vku::Shader {
                    std::apply(LIFT(shader_selector::primitive_frag), getFragmentShaderVariants()),
                    vk::ShaderStageFlagBits::eFragment,
                    &fragmentShaderSpecializationInfo,
                });

            const vk::PipelineInputAssemblyStateCreateInfo inputAssemblyStateCreateInfo {
                {},
                topologyClass.transform(getRepresentativePrimitiveTopology).value_or(vk::PrimitiveTopology::eTriangleList),
            };

            switch (alphaMode) {
                case fastgltf::AlphaMode::Opaque:
                    return { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                        pipelineStages.get(), *pipelineLayout, 1, true, vk::SampleCountFlagBits::e4)
                        .setPInputAssemblyState(&inputAssemblyStateCreateInfo)
                        .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                            {},
                            true, true, vk::CompareOp::eGreater /* use reverse Z */, false,
                            // Write stencil value as 1 if material is emissive.
                            true,
                            vk::StencilOpState { {}, vk::StencilOp::eReplace, vk::StencilOp::eKeep, vk::CompareOp::eAlways, {}, ~0U, {} /* dynamic state */ },
                            vk::StencilOpState { {}, vk::StencilOp::eReplace, vk::StencilOp::eKeep, vk::CompareOp::eAlways, {}, ~0U, {} /* dynamic state */ },
                        }))
                        .setPDynamicState(vku::unsafeAddress(vk::PipelineDynamicStateCreateInfo {
                            {},
                            vku::unsafeProxy([&]() {
                                boost::container::static_vector<vk::DynamicState, 5> result {
                                    vk::DynamicState::eViewport,
                                    vk::DynamicState::eScissor,
                                    vk::DynamicState::ePrimitiveTopology,
                                    vk::DynamicState::eCullMode,
                                };
                                if (!usePerFragmentEmissiveStencilExport) {
                                    result.push_back(vk::DynamicState::eStencilReference);
                                }
                                return result;
                            }()),
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
                            true, true, vk::CompareOp::eGreater /* use reverse Z */, false,
                            // Write stencil value as 1 if material is emissive.
                            true,
                            vk::StencilOpState { {}, vk::StencilOp::eReplace, vk::StencilOp::eKeep, vk::CompareOp::eAlways, {}, ~0U, {} /* dynamic state */ },
                            vk::StencilOpState { {}, vk::StencilOp::eReplace, vk::StencilOp::eKeep, vk::CompareOp::eAlways, {}, ~0U, {} /* dynamic state */ },
                        }))
                        .setPMultisampleState(vku::unsafeAddress(vk::PipelineMultisampleStateCreateInfo {
                            {},
                            vk::SampleCountFlagBits::e4,
                            {}, {}, {},
                            true,
                        }))
                        .setPDynamicState(vku::unsafeAddress(vk::PipelineDynamicStateCreateInfo {
                            {},
                            vku::unsafeProxy([&]() {
                                boost::container::static_vector<vk::DynamicState, 5> result {
                                    vk::DynamicState::eViewport,
                                    vk::DynamicState::eScissor,
                                    vk::DynamicState::ePrimitiveTopology,
                                    vk::DynamicState::eCullMode,
                                };
                                if (!usePerFragmentEmissiveStencilExport) {
                                    result.push_back(vk::DynamicState::eStencilReference);
                                }
                                return result;
                            }()),
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
                            true, false, vk::CompareOp::eGreater, false,
                            // Write stencil value as 1 if material is emissive.
                            true,
                            vk::StencilOpState { {}, vk::StencilOp::eReplace, {}, vk::CompareOp::eAlways, {}, ~0U, {} /* dynamic state */ },
                            vk::StencilOpState { {}, vk::StencilOp::eReplace, {}, vk::CompareOp::eAlways, {}, ~0U, {} /* dynamic state */ },
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
                            vku::unsafeProxy([&]() {
                                boost::container::static_vector<vk::DynamicState, 4> result {
                                    vk::DynamicState::eViewport,
                                    vk::DynamicState::eScissor,
                                    vk::DynamicState::ePrimitiveTopology,
                                };
                                if (!usePerFragmentEmissiveStencilExport) {
                                    result.push_back(vk::DynamicState::eStencilReference);
                                }
                                return result;
                            }()),
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

        [[nodiscard]] std::array<int, 5> getFragmentShaderVariants() const noexcept {
            return {
                static_cast<int>(texcoordComponentTypes.size()),
                colorComponentCountAndType.has_value(),
                fragmentShaderGeneratedTBN,
                static_cast<int>(alphaMode),
                usePerFragmentEmissiveStencilExport,
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