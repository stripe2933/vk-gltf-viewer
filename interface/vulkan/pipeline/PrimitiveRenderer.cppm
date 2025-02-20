module;

#include <cassert>

#include <boost/container/static_vector.hpp>
#include <boost/container_hash/hash.hpp>

export module vk_gltf_viewer:vulkan.pipeline.PrimitiveRenderer;

import std;
export import fastgltf;
import vku;
import :helpers.ranges;
import :shader_selector.primitive_vert;
import :shader_selector.primitive_frag;
export import :vulkan.pl.Primitive;
export import :vulkan.rp.Scene;
export import :vulkan.shader_type.TextureTransform;
import :vulkan.specialization_constants.SpecializationMap;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [&](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class PrimitiveRendererSpecialization {
    public:
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
        shader_type::TextureTransform baseColorTextureTransform = shader_type::TextureTransform::None;
        shader_type::TextureTransform metallicRoughnessTextureTransform = shader_type::TextureTransform::None;
        shader_type::TextureTransform normalTextureTransform = shader_type::TextureTransform::None;
        shader_type::TextureTransform occlusionTextureTransform = shader_type::TextureTransform::None;
        shader_type::TextureTransform emissiveTextureTransform = shader_type::TextureTransform::None;
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

            switch (alphaMode) {
                case fastgltf::AlphaMode::Opaque:
                    return { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                        pipelineStages.get(), *pipelineLayout, 1, true, vk::SampleCountFlagBits::e4)
                        .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                            {},
                            true, true, vk::CompareOp::eGreater, // Use reverse Z.
                        }))
                        .setPDynamicState(vku::unsafeAddress(vk::PipelineDynamicStateCreateInfo {
                            {},
                            vku::unsafeProxy({
                                vk::DynamicState::eViewport,
                                vk::DynamicState::eScissor,
                                vk::DynamicState::eCullMode,
                            }),
                        }))
                        .setRenderPass(*sceneRenderPass)
                        .setSubpass(0)
                    };
                case fastgltf::AlphaMode::Mask:
                    return { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                        pipelineStages.get(), *pipelineLayout, 1, true, vk::SampleCountFlagBits::e4)
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
                                vk::DynamicState::eCullMode,
                            }),
                        }))
                        .setRenderPass(*sceneRenderPass)
                        .setSubpass(0)
                    };
                case fastgltf::AlphaMode::Blend:
                    return { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                        pipelineStages.get(), *pipelineLayout, 1, true, vk::SampleCountFlagBits::e4)
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
        };

        struct FragmentShaderSpecializationData {
            std::uint32_t packedTextureTransformTypes = 0x00000;
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
                assert(ranges::one_of(colorComponentCountAndType->first, 3, 4));
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
            FragmentShaderSpecializationData result{};

            if (baseColorTextureTransform != shader_type::TextureTransform::None) {
                result.packedTextureTransformTypes &= ~0xFU;
                result.packedTextureTransformTypes |= static_cast<std::uint32_t>(baseColorTextureTransform);
            }
            if (metallicRoughnessTextureTransform != shader_type::TextureTransform::None) {
                result.packedTextureTransformTypes &= ~0xF0U;
                result.packedTextureTransformTypes |= static_cast<std::uint32_t>(metallicRoughnessTextureTransform) << 4;
            }
            if (normalTextureTransform != shader_type::TextureTransform::None) {
                result.packedTextureTransformTypes &= ~0xF00U;
                result.packedTextureTransformTypes |= static_cast<std::uint32_t>(normalTextureTransform) << 8;
            }
            if (occlusionTextureTransform != shader_type::TextureTransform::None) {
                result.packedTextureTransformTypes &= ~0xF000U;
                result.packedTextureTransformTypes |= static_cast<std::uint32_t>(occlusionTextureTransform) << 12;
            }
            if (emissiveTextureTransform != shader_type::TextureTransform::None) {
                result.packedTextureTransformTypes &= ~0xF0000U;
                result.packedTextureTransformTypes |= static_cast<std::uint32_t>(emissiveTextureTransform) << 16;
            }

            return result;
        }
    };
}