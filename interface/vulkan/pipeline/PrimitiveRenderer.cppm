module;

#include <cassert>
#include <cstddef>

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

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct TextureTransform {
        enum class Type : std::uint8_t {
            None = 0,
            ScaleAndOffset = 1,
            All = 2,
        };

        Type baseColor = Type::None;
        Type metallicRoughness = Type::None;
        Type normal = Type::None;
        Type occlusion = Type::None;
        Type emissive = Type::None;

        [[nodiscard]] bool operator==(const TextureTransform&) const noexcept = default;

        // For boost::hash_combine.
        [[nodiscard]] friend std::size_t hash_value(const TextureTransform &v) {
            std::size_t seed = 0;
            boost::hash_combine(seed, std::to_underlying(v.baseColor));
            boost::hash_combine(seed, std::to_underlying(v.metallicRoughness));
            boost::hash_combine(seed, std::to_underlying(v.normal));
            boost::hash_combine(seed, std::to_underlying(v.occlusion));
            boost::hash_combine(seed, std::to_underlying(v.emissive));
            return seed;
        }
    };

    [[nodiscard]] vk::raii::Pipeline createPrimitiveRenderer(
        const vk::raii::Device &device,
        const pl::Primitive &pipelineLayout,
        const rp::Scene &sceneRenderPass,
        const boost::container::static_vector<fastgltf::ComponentType, 4> &texcoordComponentTypes,
        const std::optional<std::pair<std::uint8_t, fastgltf::ComponentType>> &colorComponentCountAndType,
        bool fragmentShaderGeneratedTBN,
        const TextureTransform &textureTransform,
        fastgltf::AlphaMode alphaMode
    ) {
        // --------------------
        // Vertex shader specialization constants.
        // --------------------

        struct VertexShaderSpecializationData {
            std::uint32_t packedTexcoordComponentTypes = 0x06060606; // [FLOAT, FLOAT, FLOAT, FLOAT]
            std::uint8_t colorComponentCount = 0;
            std::uint32_t colorComponentType = 5126; // FLOAT
        } vertexShaderSpecializationData{};

        for (auto [i, componentType] : texcoordComponentTypes | ranges::views::enumerate) {
            assert(ranges::one_of(componentType, fastgltf::ComponentType::UnsignedByte, fastgltf::ComponentType::UnsignedShort, fastgltf::ComponentType::Float));

            /* Change the i-th byte (from LSB) to the componentType - fastgltf::ComponentType::Byte, which can be
             * represented by the 1-byte integer.
             *
             * fastgltf::ComponentType::Byte - fastgltf::ComponentType::Byte = 0
             * fastgltf::ComponentType::UnsignedByte - fastgltf::ComponentType::Byte = 1
             * fastgltf::ComponentType::Short - fastgltf::ComponentType::Byte = 2
             * fastgltf::ComponentType::UnsignedShort - fastgltf::ComponentType::Byte = 3
             * fastgltf::ComponentType::Int - fastgltf::ComponentType::Byte = 4
             * fastgltf::ComponentType::UnsignedInt - fastgltf::ComponentType::Byte = 5
             * fastgltf::ComponentType::Float - fastgltf::ComponentType::Byte = 6
             * fastgltf::ComponentType::Double - fastgltf::ComponentType::Byte = 10
             */

            // Step 1: clear the i-th byte (=[8*(i+1):8*i] bits)
            vertexShaderSpecializationData.packedTexcoordComponentTypes &= ~(0xFFU << (8 * i));

            // Step 2: set the i-th byte to the componentType - fastgltf::ComponentType::Byte
            vertexShaderSpecializationData.packedTexcoordComponentTypes
                |= (getGLComponentType(componentType) - getGLComponentType(fastgltf::ComponentType::Byte)) << (8 * i);
        }

        if (colorComponentCountAndType) {
            assert(ranges::one_of(colorComponentCountAndType->first, 3, 4));
            assert(ranges::one_of(colorComponentCountAndType->second, fastgltf::ComponentType::UnsignedByte, fastgltf::ComponentType::UnsignedShort, fastgltf::ComponentType::Float));
            vertexShaderSpecializationData.colorComponentCount = colorComponentCountAndType->first;
            vertexShaderSpecializationData.colorComponentType = getGLComponentType(colorComponentCountAndType->second);
        }

        static constexpr std::array vertexShaderSpecializationMapEntries {
            vk::SpecializationMapEntry {
                0,
                offsetof(VertexShaderSpecializationData, packedTexcoordComponentTypes),
                sizeof(VertexShaderSpecializationData::packedTexcoordComponentTypes),
            },
            vk::SpecializationMapEntry {
                1,
                offsetof(VertexShaderSpecializationData, colorComponentCount),
                sizeof(VertexShaderSpecializationData::colorComponentCount),
            },
            vk::SpecializationMapEntry {
                2,
                offsetof(VertexShaderSpecializationData, colorComponentType),
                sizeof(VertexShaderSpecializationData::colorComponentType),
            },
        };

        const vk::SpecializationInfo vertexShaderSpecializationInfo {
            vertexShaderSpecializationMapEntries,
            vk::ArrayProxyNoTemporaries<const VertexShaderSpecializationData> { vertexShaderSpecializationData },
        };

        // --------------------
        // Fragment shader specialization constants.
        // --------------------

        struct FragmentShaderSpecializationData {
            std::uint32_t packedTextureTransformTypes = 0x00000; // [NONE, NONE, NONE, NONE, NONE]
        } fragmentShaderSpecializationData{};

        if (textureTransform.baseColor != TextureTransform::Type::None) {
            fragmentShaderSpecializationData.packedTextureTransformTypes &= ~0xFU;
            fragmentShaderSpecializationData.packedTextureTransformTypes |= static_cast<std::uint32_t>(textureTransform.baseColor);
        }
        if (textureTransform.metallicRoughness != TextureTransform::Type::None) {
            fragmentShaderSpecializationData.packedTextureTransformTypes &= ~0xF0U;
            fragmentShaderSpecializationData.packedTextureTransformTypes |= static_cast<std::uint32_t>(textureTransform.metallicRoughness) << 4;
        }
        if (textureTransform.normal != TextureTransform::Type::None) {
            fragmentShaderSpecializationData.packedTextureTransformTypes &= ~0xF00U;
            fragmentShaderSpecializationData.packedTextureTransformTypes |= static_cast<std::uint32_t>(textureTransform.normal) << 8;
        }
        if (textureTransform.occlusion != TextureTransform::Type::None) {
            fragmentShaderSpecializationData.packedTextureTransformTypes &= ~0xF000U;
            fragmentShaderSpecializationData.packedTextureTransformTypes |= static_cast<std::uint32_t>(textureTransform.occlusion) << 12;
        }
        if (textureTransform.emissive != TextureTransform::Type::None) {
            fragmentShaderSpecializationData.packedTextureTransformTypes &= ~0xF0000U;
            fragmentShaderSpecializationData.packedTextureTransformTypes |= static_cast<std::uint32_t>(textureTransform.emissive) << 16;
        }

        static constexpr std::array fragmentShaderSpecializationMapEntries {
            vk::SpecializationMapEntry {
                0,
                offsetof(FragmentShaderSpecializationData, packedTextureTransformTypes),
                sizeof(FragmentShaderSpecializationData::packedTextureTransformTypes),
            },
        };

        const vk::SpecializationInfo fragmentShaderSpecializationInfo {
            fragmentShaderSpecializationMapEntries,
            vk::ArrayProxyNoTemporaries<const FragmentShaderSpecializationData> { fragmentShaderSpecializationData },
        };

        const vku::RefHolder pipelineStages = createPipelineStages(
            device,
            vku::Shader {
                shader_selector::primitive_vert(
                    texcoordComponentTypes.size(),
                    colorComponentCountAndType.has_value(),
                    fragmentShaderGeneratedTBN),
                vk::ShaderStageFlagBits::eVertex,
                &vertexShaderSpecializationInfo,
            },
            vku::Shader {
                shader_selector::primitive_frag(
                    texcoordComponentTypes.size(),
                    colorComponentCountAndType.has_value(),
                    fragmentShaderGeneratedTBN,
                    alphaMode),
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
}