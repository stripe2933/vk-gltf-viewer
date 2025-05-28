module;

#include <boost/container/static_vector.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.Frame;

import std;
import imgui.vulkan;
import :global;
import :gltf.algorithm.bounding_box;
import :helpers.concepts;
import :helpers.fastgltf;
import :helpers.functional;
import :helpers.optional;
import :helpers.ranges;
import :math.extended_arithmetic;
import :vulkan.ag.MousePicking;
import :vulkan.buffer.IndirectDrawCommands;
import :vulkan.shader_type.Accessor;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

constexpr auto NO_INDEX = std::numeric_limits<std::uint16_t>::max();

[[nodiscard]] constexpr vk::PrimitiveTopology getPrimitiveTopology(fastgltf::PrimitiveType type) noexcept {
    switch (type) {
    case fastgltf::PrimitiveType::Points:
        return vk::PrimitiveTopology::ePointList;
    case fastgltf::PrimitiveType::Lines:
        return vk::PrimitiveTopology::eLineList;
    // There is no GL_LINE_LOOP equivalent in Vulkan, so we use GL_LINE_STRIP instead.
    case fastgltf::PrimitiveType::LineLoop:
    case fastgltf::PrimitiveType::LineStrip:
        return vk::PrimitiveTopology::eLineStrip;
    case fastgltf::PrimitiveType::Triangles:
        return vk::PrimitiveTopology::eTriangleList;
    case fastgltf::PrimitiveType::TriangleStrip:
        return vk::PrimitiveTopology::eTriangleStrip;
    case fastgltf::PrimitiveType::TriangleFan:
        return vk::PrimitiveTopology::eTriangleFan;
    }
    std::unreachable();
}

vk_gltf_viewer::vulkan::Frame::Frame(const SharedData &sharedData)
    : sharedData { sharedData }
    , descriptorPool { createDescriptorPool() }
    , computeCommandPool { sharedData.gpu.device, vk::CommandPoolCreateInfo { {}, sharedData.gpu.queueFamilies.compute } }
    , graphicsCommandPool { sharedData.gpu.device, vk::CommandPoolCreateInfo { {}, sharedData.gpu.queueFamilies.graphicsPresent } }
    , scenePrepassFinishSema { sharedData.gpu.device, vk::SemaphoreCreateInfo{} }
    , sceneRenderingFinishSema { sharedData.gpu.device, vk::SemaphoreCreateInfo{} }
    , jumpFloodFinishSema { sharedData.gpu.device, vk::SemaphoreCreateInfo{} } {
    // Allocate descriptor sets.
    std::tie(mousePickingSet, multiNodeMousePickingSet, hoveringNodeJumpFloodSet, selectedNodeJumpFloodSet, hoveringNodeOutlineSet, selectedNodeOutlineSet, weightedBlendedCompositionSet, inverseToneMappingSet, bloomSet, bloomApplySet)
        = allocateDescriptorSets(*descriptorPool, std::tie(
            sharedData.mousePickingRenderer.descriptorSetLayout,
            sharedData.multiNodeMousePickingDescriptorSetLayout,
            sharedData.jumpFloodComputer.descriptorSetLayout,
            sharedData.jumpFloodComputer.descriptorSetLayout,
            sharedData.outlineRenderer.descriptorSetLayout,
            sharedData.outlineRenderer.descriptorSetLayout,
            sharedData.weightedBlendedCompositionRenderer.descriptorSetLayout,
            sharedData.inverseToneMappingRenderer.descriptorSetLayout,
            sharedData.bloomComputer.descriptorSetLayout,
            sharedData.bloomApplyRenderer.descriptorSetLayout));

    // Allocate per-frame command buffers.
    std::tie(jumpFloodCommandBuffer) = vku::allocateCommandBuffers<1>(*sharedData.gpu.device, *computeCommandPool);
    std::tie(scenePrepassCommandBuffer, sceneRenderingCommandBuffer, compositionCommandBuffer)
        = vku::allocateCommandBuffers<3>(*sharedData.gpu.device, *graphicsCommandPool);
}

vk_gltf_viewer::vulkan::Frame::UpdateResult vk_gltf_viewer::vulkan::Frame::update(const ExecutionTask &task) {
    UpdateResult result{};

    // --------------------
    // Update CPU resources.
    // --------------------

    // Retrieve the mouse picking result from the buffer.
    visit(multilambda {
        [&](const vk::Rect2D&) {
            if (!gltfAsset) return;

            const std::span packedBits = gltfAsset->mousePickingResultBuffer.asRange<const std::uint32_t>();
            std::vector<std::size_t> &indices = result.mousePickingResult.emplace<std::vector<std::size_t>>();
            for (std::size_t nodeIndex = 0; nodeIndex < task.gltf->asset.nodes.size(); ++nodeIndex) {
                const std::uint32_t accessBlock = packedBits[nodeIndex / 32];
                const std::uint32_t bitmask = 1U << (nodeIndex % 32);
                if (accessBlock & bitmask) {
                    indices.push_back(nodeIndex);
                }
            }
        },
        [&](const vk::Offset2D&) {
            if (!gltfAsset) return;

            const std::uint16_t hoveringNodeIndex = gltfAsset->mousePickingResultBuffer.asValue<const std::uint32_t>();
            if (hoveringNodeIndex != NO_INDEX) {
                result.mousePickingResult.emplace<std::size_t>(hoveringNodeIndex);
            }
        },
        [](std::monostate) { }
    }, mousePickingInput);

    // If passthru extent is different from the current's, dependent images have to be recreated.
    if (!passthruResources || passthruResources->extent != task.passthruRect.extent) {
        // TODO: can this operation be non-blocking?
        const vk::raii::Fence fence { sharedData.gpu.device, vk::FenceCreateInfo{} };
        vku::executeSingleCommand(*sharedData.gpu.device, *graphicsCommandPool, sharedData.gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
            passthruResources.emplace(sharedData, task.passthruRect.extent, cb);
        }, *fence);
        std::ignore = sharedData.gpu.device.waitForFences(*fence, true, ~0ULL); // TODO: failure handling

        std::vector<vk::DescriptorImageInfo> bloomSetDescriptorInfos;
        if (sharedData.gpu.supportShaderImageLoadStoreLod) {
            bloomSetDescriptorInfos.push_back({ {}, *passthruResources->bloomImageView, vk::ImageLayout::eGeneral });
        }
        else {
            bloomSetDescriptorInfos.append_range(passthruResources->bloomMipImageViews | std::views::transform([this](vk::ImageView imageView) {
                return vk::DescriptorImageInfo{ {}, imageView, vk::ImageLayout::eGeneral };
            }));
        }

        sharedData.gpu.device.updateDescriptorSets({
            mousePickingSet.getWriteOne<0>({ {}, *passthruResources->mousePickingAttachmentGroup.getColorAttachment(0).view, vk::ImageLayout::eShaderReadOnlyOptimal }),
            hoveringNodeJumpFloodSet.getWriteOne<0>({ {}, *passthruResources->hoveringNodeOutlineJumpFloodResources.imageView, vk::ImageLayout::eGeneral }),
            selectedNodeJumpFloodSet.getWriteOne<0>({ {}, *passthruResources->selectedNodeOutlineJumpFloodResources.imageView, vk::ImageLayout::eGeneral }),
            weightedBlendedCompositionSet.getWrite<0>(vku::unsafeProxy({
                vk::DescriptorImageInfo { {}, *passthruResources->sceneWeightedBlendedAttachmentGroup.getColorAttachment(0).view, vk::ImageLayout::eShaderReadOnlyOptimal },
                vk::DescriptorImageInfo { {}, *passthruResources->sceneWeightedBlendedAttachmentGroup.getColorAttachment(1).view, vk::ImageLayout::eShaderReadOnlyOptimal },
            })),
            inverseToneMappingSet.getWriteOne<0>({ {}, *passthruResources->sceneOpaqueAttachmentGroup.getColorAttachment(0).view, vk::ImageLayout::eShaderReadOnlyOptimal }),
            bloomSet.getWriteOne<0>({ {}, *passthruResources->bloomImageView, vk::ImageLayout::eGeneral }),
            bloomSet.getWrite<1>(bloomSetDescriptorInfos),
            bloomApplySet.getWriteOne<0>({ {}, *passthruResources->sceneOpaqueAttachmentGroup.getColorAttachment(0).view, vk::ImageLayout::eGeneral }),
            bloomApplySet.getWriteOne<1>({ {}, *passthruResources->bloomMipImageViews[0], vk::ImageLayout::eShaderReadOnlyOptimal }),
        }, {});
    }

    projectionViewMatrix = task.camera.projection * task.camera.view;
    viewPosition = inverse(task.camera.view)[3];
    translationlessProjectionViewMatrix = task.camera.projection * glm::mat4 { glm::mat3 { task.camera.view } };
    passthruOffset = task.passthruRect.offset;
    mousePickingInput = task.mousePickingInput;

    const auto criteriaGetter = [&](const fastgltf::Primitive &primitive) {
        const bool usePerFragmentEmissiveStencilExport = global::bloom.raw().mode == global::Bloom::Mode::PerFragment;
        CommandSeparationCriteria result {
            .subpass = 0U,
            .indexType = value_if(primitive.type == fastgltf::PrimitiveType::LineLoop || primitive.indicesAccessor.has_value(), [&]() {
                return sharedData.gltfAsset.value().combinedIndexBuffers.getIndexInfo(primitive).first;
            }),
            .primitiveTopology = getPrimitiveTopology(primitive.type),
            // By default, the default primitive doesn't have a material and therefore isn't unlit.
            // If per-fragment stencil export is disabled, dynamic stencil reference state has to be used, and its
            // reference value is 0.
            .stencilReference = value_if(!usePerFragmentEmissiveStencilExport, 0U),
            .cullMode = vk::CullModeFlagBits::eBack,
        };

        const auto &accessors = sharedData.gltfAsset->primitiveAttributes.getAccessors(primitive);

        // glTF 2.0 specification:
        //   Points or Lines with no NORMAL attribute SHOULD be rendered without lighting and instead use the sum of the
        //   base color value (as defined above, multiplied by COLOR_0 when present) and the emissive value.
        const bool isPrimitivePointsOrLineWithoutNormal
            = ranges::one_of(primitive.type, { fastgltf::PrimitiveType::Points, fastgltf::PrimitiveType::Lines, fastgltf::PrimitiveType::LineLoop, fastgltf::PrimitiveType::LineStrip })
            && !accessors.normalAccessor;

        if (primitive.materialIndex) {
            const fastgltf::Material &material = task.gltf->asset.materials[*primitive.materialIndex];
            result.subpass = material.alphaMode == fastgltf::AlphaMode::Blend;

            if (material.unlit || isPrimitivePointsOrLineWithoutNormal) {
                result.pipeline = sharedData.getUnlitPrimitiveRenderer({
                    .topologyClass = value_if(!sharedData.gpu.supportDynamicPrimitiveTopologyUnrestricted, [&]() { return getTopologyClass(getPrimitiveTopology(primitive.type)); }),
                    .positionComponentType = accessors.positionAccessor.componentType,
                    .baseColorTexcoordComponentType = material.pbrData.baseColorTexture.transform([&](const fastgltf::TextureInfo &textureInfo) {
                        return accessors.texcoordAccessors.at(textureInfo.texCoordIndex).componentType;
                    }),
                    .colorComponentCountAndType = accessors.colorAccessor.transform([](const auto &info) {
                        return std::pair { info.componentCount, info.componentType };
                    }),
                    .positionMorphTargetWeightCount = static_cast<std::uint32_t>(accessors.positionMorphTargetAccessors.size()),
                    .skinAttributeCount = static_cast<std::uint32_t>(accessors.jointsAccessors.size()),
                    .baseColorTextureTransform = material.pbrData.baseColorTexture && material.pbrData.baseColorTexture->transform,
                    .alphaMode = material.alphaMode,
                });

                // Disable stencil reference dynamic state when using unlit rendering pipeline.
                result.stencilReference.reset();
            }
            else {
                result.pipeline = sharedData.getPrimitiveRenderer({
                    .topologyClass = value_if(!sharedData.gpu.supportDynamicPrimitiveTopologyUnrestricted, [&]() { return getTopologyClass(getPrimitiveTopology(primitive.type)); }),
                    .positionComponentType = accessors.positionAccessor.componentType,
                    .normalComponentType = accessors.normalAccessor.transform([](const shader_type::Accessor &accessor) {
                        return accessor.componentType;
                     }),
                    .tangentComponentType = accessors.tangentAccessor.transform([](const shader_type::Accessor &accessor) {
                        return accessor.componentType;
                     }),
                    .texcoordComponentTypes = accessors.texcoordAccessors
                        | std::views::transform([](const auto &info) {
                            return info.componentType;
                        })
                        | std::views::take(4) // Avoid bad_alloc for static_vector.
                        | std::ranges::to<boost::container::static_vector<std::uint8_t, 4>>(),
                    .colorComponentCountAndType = accessors.colorAccessor.transform([](const auto &info) {
                        return std::pair { info.componentCount, info.componentType };
                    }),
                    .fragmentShaderGeneratedTBN = !accessors.normalAccessor.has_value(),
                    .morphTargetWeightCount = static_cast<std::uint32_t>(std::max({
                        accessors.positionMorphTargetAccessors.size(),
                        accessors.normalMorphTargetAccessors.size(),
                        accessors.tangentMorphTargetAccessors.size(),
                    })),
                    .hasPositionMorphTarget = !accessors.positionMorphTargetAccessors.empty(),
                    .hasNormalMorphTarget = !accessors.normalMorphTargetAccessors.empty(),
                    .hasTangentMorphTarget = !accessors.tangentMorphTargetAccessors.empty(),
                    .skinAttributeCount = static_cast<std::uint32_t>(accessors.jointsAccessors.size()),
                    .baseColorTextureTransform = material.pbrData.baseColorTexture && material.pbrData.baseColorTexture->transform,
                    .metallicRoughnessTextureTransform = material.pbrData.metallicRoughnessTexture && material.pbrData.metallicRoughnessTexture->transform,
                    .normalTextureTransform = material.normalTexture && material.normalTexture->transform,
                    .occlusionTextureTransform = material.occlusionTexture && material.occlusionTexture->transform,
                    .emissiveTextureTransform = material.emissiveTexture && material.emissiveTexture->transform,
                    .alphaMode = material.alphaMode,
                    .usePerFragmentEmissiveStencilExport = usePerFragmentEmissiveStencilExport,
                });

                if (!usePerFragmentEmissiveStencilExport) {
                    result.stencilReference.emplace(material.emissiveStrength > 1.f ? 1U : 0U);
                }
            }
            result.cullMode = material.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack;
        }
        else if (isPrimitivePointsOrLineWithoutNormal) {
            result.pipeline = sharedData.getUnlitPrimitiveRenderer({
                .topologyClass = value_if(!sharedData.gpu.supportDynamicPrimitiveTopologyUnrestricted, [&]() { return getTopologyClass(getPrimitiveTopology(primitive.type)); }),
                .positionComponentType = accessors.positionAccessor.componentType,
                .colorComponentCountAndType = accessors.colorAccessor.transform([](const auto &info) {
                    return std::pair { info.componentCount, info.componentType };
                }),
                .positionMorphTargetWeightCount = static_cast<std::uint32_t>(accessors.positionMorphTargetAccessors.size()),
                .skinAttributeCount = static_cast<std::uint32_t>(accessors.jointsAccessors.size()),
            });

            // Disable stencil reference dynamic state when using unlit rendering pipeline.
            result.stencilReference.reset();
        }
        else {
            result.pipeline = sharedData.getPrimitiveRenderer({
                .topologyClass = value_if(!sharedData.gpu.supportDynamicPrimitiveTopologyUnrestricted, [&]() { return getTopologyClass(getPrimitiveTopology(primitive.type)); }),
                // TANGENT, TEXCOORD_<i> and their corresponding morph targets are unnecessary as there is no texture.
                .positionComponentType = accessors.positionAccessor.componentType,
                .normalComponentType = accessors.normalAccessor.transform([](const shader_type::Accessor &accessor) {
                    return accessor.componentType;
                }),
                .colorComponentCountAndType = accessors.colorAccessor.transform([](const auto &info) {
                    return std::pair { info.componentCount, info.componentType };
                }),
                .fragmentShaderGeneratedTBN = !accessors.normalAccessor.has_value(),
                .morphTargetWeightCount = std::max<std::uint32_t>(
                    accessors.positionMorphTargetAccessors.size(),
                    accessors.normalMorphTargetAccessors.size()),
                .hasPositionMorphTarget = !accessors.positionMorphTargetAccessors.empty(),
                .hasNormalMorphTarget = !accessors.normalMorphTargetAccessors.empty(),
                .skinAttributeCount = static_cast<std::uint32_t>(accessors.jointsAccessors.size()),
                .usePerFragmentEmissiveStencilExport = usePerFragmentEmissiveStencilExport,
            });
        }
        return result;
    };

    const auto mousePickingCriteriaGetter = [&](const fastgltf::Primitive &primitive) {
        CommandSeparationCriteriaNoShading result{
            .indexType = value_if(primitive.type == fastgltf::PrimitiveType::LineLoop || primitive.indicesAccessor.has_value(), [&]() {
                return sharedData.gltfAsset.value().combinedIndexBuffers.getIndexInfo(primitive).first;
            }),
            .primitiveTopology = getPrimitiveTopology(primitive.type),
            .cullMode = vk::CullModeFlagBits::eBack,
        };

        const auto &accessors = sharedData.gltfAsset->primitiveAttributes.getAccessors(primitive);
        if (primitive.materialIndex) {
            const fastgltf::Material& material = task.gltf->asset.materials[*primitive.materialIndex];
            if (material.alphaMode == fastgltf::AlphaMode::Mask) {
                result.pipeline = sharedData.getMaskNodeIndexRenderer({
                    .topologyClass = value_if(!sharedData.gpu.supportDynamicPrimitiveTopologyUnrestricted, [&]() { return getTopologyClass(getPrimitiveTopology(primitive.type)); }),
                    .positionComponentType = accessors.positionAccessor.componentType,
                    .baseColorTexcoordComponentType = material.pbrData.baseColorTexture.transform([&](const fastgltf::TextureInfo &textureInfo) {
                        return accessors.texcoordAccessors.at(textureInfo.texCoordIndex).componentType;
                    }),
                    .colorAlphaComponentType = accessors.colorAccessor.and_then([](const auto &info) {
                        // Alpha value exists only if COLOR_0 is Vec4 type.
                        return value_if(info.componentCount == 4, info.componentType);
                    }),
                    .positionMorphTargetWeightCount = static_cast<std::uint32_t>(accessors.positionMorphTargetAccessors.size()),
                    .skinAttributeCount = static_cast<std::uint32_t>(accessors.jointsAccessors.size()),
                    .baseColorTextureTransform = material.pbrData.baseColorTexture && material.pbrData.baseColorTexture->transform,
                });
            }
            else {
                result.pipeline = sharedData.getNodeIndexRenderer({
                    .topologyClass = value_if(!sharedData.gpu.supportDynamicPrimitiveTopologyUnrestricted, [&]() { return getTopologyClass(getPrimitiveTopology(primitive.type)); }),
                    .positionComponentType = accessors.positionAccessor.componentType,
                    .positionMorphTargetWeightCount = static_cast<std::uint32_t>(accessors.positionMorphTargetAccessors.size()),
                    .skinAttributeCount = static_cast<std::uint32_t>(accessors.jointsAccessors.size()),
                });
            }
            result.cullMode = material.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack;
        }
        else {
            result.pipeline = sharedData.getNodeIndexRenderer({
                .topologyClass = value_if(!sharedData.gpu.supportDynamicPrimitiveTopologyUnrestricted, [&]() { return getTopologyClass(getPrimitiveTopology(primitive.type)); }),
                .positionComponentType = accessors.positionAccessor.componentType,
                .positionMorphTargetWeightCount = static_cast<std::uint32_t>(accessors.positionMorphTargetAccessors.size()),
                .skinAttributeCount = static_cast<std::uint32_t>(accessors.jointsAccessors.size()),
            });
        }
        return result;
    };

    const auto multiNodeMousePickingCriteriaGetter = [&](const fastgltf::Primitive &primitive) {
        CommandSeparationCriteriaNoShading result{
            .indexType = value_if(primitive.type == fastgltf::PrimitiveType::LineLoop || primitive.indicesAccessor.has_value(), [&]() {
                return sharedData.gltfAsset.value().combinedIndexBuffers.getIndexInfo(primitive).first;
            }),
            .primitiveTopology = getPrimitiveTopology(primitive.type),
            .cullMode = vk::CullModeFlagBits::eNone,
        };

        const auto &accessors = sharedData.gltfAsset->primitiveAttributes.getAccessors(primitive);
        if (primitive.materialIndex) {
            const fastgltf::Material& material = task.gltf->asset.materials[*primitive.materialIndex];
            if (material.alphaMode == fastgltf::AlphaMode::Mask) {
                result.pipeline = sharedData.getMaskMultiNodeMousePickingRenderer({
                    .topologyClass = value_if(!sharedData.gpu.supportDynamicPrimitiveTopologyUnrestricted, [&]() { return getTopologyClass(getPrimitiveTopology(primitive.type)); }),
                    .positionComponentType = accessors.positionAccessor.componentType,
                    .baseColorTexcoordComponentType = material.pbrData.baseColorTexture.transform([&](const fastgltf::TextureInfo &textureInfo) {
                        return accessors.texcoordAccessors.at(textureInfo.texCoordIndex).componentType;
                    }),
                    .colorAlphaComponentType = accessors.colorAccessor.and_then([](const auto &info) {
                        // Alpha value exists only if COLOR_0 is Vec4 type.
                        return value_if(info.componentCount == 4, info.componentType);
                    }),
                    .positionMorphTargetWeightCount = static_cast<std::uint32_t>(accessors.positionMorphTargetAccessors.size()),
                    .skinAttributeCount = static_cast<std::uint32_t>(accessors.jointsAccessors.size()),
                    .baseColorTextureTransform = material.pbrData.baseColorTexture && material.pbrData.baseColorTexture->transform,
                });
            }
            else {
                result.pipeline = sharedData.getMultiNodeMousePickingRenderer({
                    .topologyClass = value_if(!sharedData.gpu.supportDynamicPrimitiveTopologyUnrestricted, [&]() { return getTopologyClass(getPrimitiveTopology(primitive.type)); }),
                    .positionComponentType = accessors.positionAccessor.componentType,
                    .positionMorphTargetWeightCount = static_cast<std::uint32_t>(accessors.positionMorphTargetAccessors.size()),
                    .skinAttributeCount = static_cast<std::uint32_t>(accessors.jointsAccessors.size()),
                });
            }
        }
        else {
            result.pipeline = sharedData.getMultiNodeMousePickingRenderer({
                .topologyClass = value_if(!sharedData.gpu.supportDynamicPrimitiveTopologyUnrestricted, [&]() { return getTopologyClass(getPrimitiveTopology(primitive.type)); }),
                .positionComponentType = accessors.positionAccessor.componentType,
                .positionMorphTargetWeightCount = static_cast<std::uint32_t>(accessors.positionMorphTargetAccessors.size()),
                .skinAttributeCount = static_cast<std::uint32_t>(accessors.jointsAccessors.size()),
            });
        }
        return result;
    };

    const auto jumpFloodSeedCriteriaGetter = [&](const fastgltf::Primitive &primitive) {
        CommandSeparationCriteriaNoShading result {
            .indexType = value_if(primitive.type == fastgltf::PrimitiveType::LineLoop || primitive.indicesAccessor.has_value(), [&]() {
                return sharedData.gltfAsset.value().combinedIndexBuffers.getIndexInfo(primitive).first;
            }),
            .primitiveTopology = getPrimitiveTopology(primitive.type),
            .cullMode = vk::CullModeFlagBits::eBack,
        };

        const auto &accessors = sharedData.gltfAsset->primitiveAttributes.getAccessors(primitive);
        if (primitive.materialIndex) {
            const fastgltf::Material &material = task.gltf->asset.materials[*primitive.materialIndex];
            if (material.alphaMode == fastgltf::AlphaMode::Mask) {
                result.pipeline = sharedData.getMaskJumpFloodSeedRenderer({
                    .topologyClass = value_if(!sharedData.gpu.supportDynamicPrimitiveTopologyUnrestricted, [&]() { return getTopologyClass(getPrimitiveTopology(primitive.type)); }),
                    .positionComponentType = accessors.positionAccessor.componentType,
                    .baseColorTexcoordComponentType = material.pbrData.baseColorTexture.transform([&](const fastgltf::TextureInfo &textureInfo) {
                        return accessors.texcoordAccessors.at(textureInfo.texCoordIndex).componentType;
                    }),
                    .colorAlphaComponentType = accessors.colorAccessor.and_then([](const auto &info) {
                        // Alpha value exists only if COLOR_0 is Vec4 type.
                        return value_if(info.componentCount == 4, info.componentType);
                    }),
                    .positionMorphTargetWeightCount = static_cast<std::uint32_t>(accessors.positionMorphTargetAccessors.size()),
                    .skinAttributeCount = static_cast<std::uint32_t>(accessors.jointsAccessors.size()),
                    .baseColorTextureTransform = material.pbrData.baseColorTexture && material.pbrData.baseColorTexture->transform,
                });
            }
            else {
                result.pipeline = sharedData.getJumpFloodSeedRenderer({
                    .topologyClass = value_if(!sharedData.gpu.supportDynamicPrimitiveTopologyUnrestricted, [&]() { return getTopologyClass(getPrimitiveTopology(primitive.type)); }),
                    .positionComponentType = accessors.positionAccessor.componentType,
                    .positionMorphTargetWeightCount = static_cast<std::uint32_t>(accessors.positionMorphTargetAccessors.size()),
                    .skinAttributeCount = static_cast<std::uint32_t>(accessors.jointsAccessors.size()),
                });
            }
            result.cullMode = material.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack;
        }
        else {
            result.pipeline = sharedData.getJumpFloodSeedRenderer({
                .topologyClass = value_if(!sharedData.gpu.supportDynamicPrimitiveTopologyUnrestricted, [&]() { return getTopologyClass(getPrimitiveTopology(primitive.type)); }),
                .positionComponentType = accessors.positionAccessor.componentType,
                .positionMorphTargetWeightCount = static_cast<std::uint32_t>(accessors.positionMorphTargetAccessors.size()),
                .skinAttributeCount = static_cast<std::uint32_t>(accessors.jointsAccessors.size()),
            });
        }
        return result;
    };

    const auto drawCommandGetter = [&](
        std::size_t nodeIndex,
        const fastgltf::Primitive &primitive
    ) -> std::variant<vk::DrawIndirectCommand, vk::DrawIndexedIndirectCommand> {
        // Get the accessor which determine the draw count.
        // - If the primitive has indices accessor, it will determine the draw count.
        // - Otherwise, the POSITION accessor will determine the draw count.
        const std::size_t drawCountDeterminingAccessorIndex
            = primitive.indicesAccessor.value_or(primitive.findAttribute("POSITION")->accessorIndex);
        std::uint32_t drawCount = task.gltf->asset.accessors[drawCountDeterminingAccessorIndex].count;

        // Since GL_LINE_LOOP primitive is emulated as LINE_STRIP draw, additional 1 index is used.
        if (primitive.type == fastgltf::PrimitiveType::LineLoop) {
            ++drawCount;
        }

        // EXT_mesh_gpu_instancing support.
        std::uint32_t instanceCount = 1;
        if (const fastgltf::Node &node = task.gltf->asset.nodes[nodeIndex]; !node.instancingAttributes.empty()) {
            instanceCount = task.gltf->asset.accessors[node.instancingAttributes[0].accessorIndex].count;
        }

        const std::size_t primitiveIndex = task.gltf->orderedPrimitives.getIndex(primitive);

        // To embed the node and primitive indices into 32-bit unsigned integer, both must be in range of 16-bit unsigned integer.
        if (!std::in_range<std::uint16_t>(nodeIndex) || !std::in_range<std::uint16_t>(primitiveIndex)) {
            throw std::runtime_error { "Requirement violation: nodeIndex <= 65535 && primitiveIndex <= 65535" };
        }

        const std::uint32_t firstInstance = (static_cast<std::uint32_t>(nodeIndex) << 16U) | static_cast<std::uint32_t>(primitiveIndex);
        if (primitive.type == fastgltf::PrimitiveType::LineLoop || primitive.indicesAccessor) {
            const auto [_, firstIndex] = sharedData.gltfAsset.value().combinedIndexBuffers.getIndexInfo(primitive);
            return vk::DrawIndexedIndirectCommand { drawCount, instanceCount, firstIndex, 0, firstInstance };
        }
        else {
            return vk::DrawIndirectCommand { drawCount, instanceCount, 0, firstInstance };
        }
    };

    if (task.gltf) {
        if (!renderingNodes || task.gltf->regenerateDrawCommands) {
            const std::vector<std::size_t> visibleNodeIndices
                = task.gltf->nodeVisibilities
                | ranges::views::enumerate
                | std::views::filter(LIFT(get<1>)) // Filter only if visibility bit is 1.
                | std::views::keys
                | std::views::transform([](auto index) { return static_cast<std::size_t>(index); })
                | std::ranges::to<std::vector>();
            renderingNodes.emplace(
                buffer::createIndirectDrawCommandBuffers(task.gltf->asset, sharedData.gpu.allocator, criteriaGetter, visibleNodeIndices, drawCommandGetter),
                buffer::createIndirectDrawCommandBuffers(task.gltf->asset, sharedData.gpu.allocator, mousePickingCriteriaGetter, visibleNodeIndices, drawCommandGetter),
                buffer::createIndirectDrawCommandBuffers(task.gltf->asset, sharedData.gpu.allocator, multiNodeMousePickingCriteriaGetter, visibleNodeIndices, drawCommandGetter));
        }

        if (task.frustum) {
            const auto commandBufferCullingFunc = [&](buffer::IndirectDrawCommands &indirectDrawCommands) -> void {
                indirectDrawCommands.partition([&](const auto &command) {
                    if (command.instanceCount > 1) {
                        // Do not perform frustum culling for instanced mesh.
                        return true;
                    }

                    const std::uint16_t nodeIndex = command.firstInstance >> 16U;
                    const fastgltf::Node &node = task.gltf->asset.nodes[nodeIndex];

                    if (node.skinIndex) {
                        // As primitive POSITION accessor's min/max values are not sufficient to determine the bounding
                        // volume of a skinned mesh, frustum culling which relies on this must be disabled.
                        return true;
                    }

                    const std::uint16_t primitiveIndex = command.firstInstance & 0xFFFFU;
                    const auto [min, max] = gltf::algorithm::getBoundingBoxMinMax<float>(
                        *task.gltf->orderedPrimitives[primitiveIndex], node, task.gltf->asset);

                    const fastgltf::math::fmat4x4 &nodeWorldTransform = task.gltf->nodeWorldTransforms[nodeIndex];
                    const fastgltf::math::fvec3 transformedMin { nodeWorldTransform * fastgltf::math::fvec4 { min.x(), min.y(), min.z(), 1.f } };
                    const fastgltf::math::fvec3 transformedMax { nodeWorldTransform * fastgltf::math::fvec4 { max.x(), max.y(), max.z(), 1.f } };

                    const fastgltf::math::fvec3 halfDisplacement = (transformedMax - transformedMin) / 2.f;
                    const fastgltf::math::fvec3 center = transformedMin + halfDisplacement;
                    const float radius = length(halfDisplacement);

                    return task.frustum->isOverlapApprox(glm::make_vec3(center.data()), radius);
                });
            };

            for (auto &buffer : renderingNodes->indirectDrawCommandBuffers | std::views::values) {
                commandBufferCullingFunc(buffer);
            }
            for (auto &buffer : renderingNodes->mousePickingIndirectDrawCommandBuffers | std::views::values) {
                commandBufferCullingFunc(buffer);
            }
            for (auto &buffer : renderingNodes->multiNodeMousePickingIndirectDrawCommandBuffers | std::views::values) {
                commandBufferCullingFunc(buffer);
            }
        }
        else {
            for (auto &buffer : renderingNodes->indirectDrawCommandBuffers | std::views::values) {
                buffer.resetDrawCount();
            }
            for (auto &buffer : renderingNodes->mousePickingIndirectDrawCommandBuffers | std::views::values) {
                buffer.resetDrawCount();
            }
            for (auto &buffer : renderingNodes->multiNodeMousePickingIndirectDrawCommandBuffers | std::views::values) {
                buffer.resetDrawCount();
            }
        }
    }
    else {
        renderingNodes.reset();
    }

    if (task.gltf && task.gltf->selectedNodes) {
        if (selectedNodes) {
            if (task.gltf->regenerateDrawCommands ||
                selectedNodes->indices != task.gltf->selectedNodes->indices) {
                selectedNodes->indices = task.gltf->selectedNodes->indices;
                selectedNodes->jumpFloodSeedIndirectDrawCommandBuffers = buffer::createIndirectDrawCommandBuffers(task.gltf->asset, sharedData.gpu.allocator, jumpFloodSeedCriteriaGetter, task.gltf->selectedNodes->indices, drawCommandGetter);
            }
            selectedNodes->outlineColor = task.gltf->selectedNodes->outlineColor;
            selectedNodes->outlineThickness = task.gltf->selectedNodes->outlineThickness;
        }
        else {
            selectedNodes.emplace(
                task.gltf->selectedNodes->indices,
                buffer::createIndirectDrawCommandBuffers(task.gltf->asset, sharedData.gpu.allocator, jumpFloodSeedCriteriaGetter, task.gltf->selectedNodes->indices, drawCommandGetter),
                task.gltf->selectedNodes->outlineColor,
                task.gltf->selectedNodes->outlineThickness);
        }
    }
    else {
        selectedNodes.reset();
    }

    if (task.gltf && task.gltf->hoveringNode &&
        // If selectedNodeIndices == hoveringNodeIndex, hovering node outline doesn't have to be drawn.
        !(task.gltf->selectedNodes && task.gltf->selectedNodes->indices.size() == 1 && *task.gltf->selectedNodes->indices.begin() == task.gltf->hoveringNode->index)) {
        if (hoveringNode) {
            if (task.gltf->regenerateDrawCommands ||
                hoveringNode->index != task.gltf->hoveringNode->index) {
                hoveringNode->index = task.gltf->hoveringNode->index;
                hoveringNode->jumpFloodSeedIndirectDrawCommandBuffers = buffer::createIndirectDrawCommandBuffers(task.gltf->asset, sharedData.gpu.allocator, jumpFloodSeedCriteriaGetter, std::views::single(task.gltf->hoveringNode->index), drawCommandGetter);
            }
            hoveringNode->outlineColor = task.gltf->hoveringNode->outlineColor;
            hoveringNode->outlineThickness = task.gltf->hoveringNode->outlineThickness;
        }
        else {
            hoveringNode.emplace(
                task.gltf->hoveringNode->index,
                buffer::createIndirectDrawCommandBuffers(task.gltf->asset, sharedData.gpu.allocator, jumpFloodSeedCriteriaGetter, std::views::single(task.gltf->hoveringNode->index), drawCommandGetter),
                task.gltf->hoveringNode->outlineColor,
                task.gltf->hoveringNode->outlineThickness);
        }
    }
    else {
        hoveringNode.reset();
    }

    if (task.solidBackground) {
        background.emplace<glm::vec3>(*task.solidBackground);
    }
    else {
        background.emplace<vku::DescriptorSet<dsl::Skybox>>(sharedData.skyboxDescriptorSet);
    }

    return result;
}

void vk_gltf_viewer::vulkan::Frame::recordCommandsAndSubmit(
    std::uint32_t swapchainImageIndex,
    vk::Semaphore swapchainImageAcquireSemaphore,
    vk::Semaphore swapchainImageReadySemaphore,
    vk::Fence inFlightFence
) const {
    // Record commands.
    graphicsCommandPool.reset();
    computeCommandPool.reset();

    // Jump flood image seeding & mouse picking pass.
    {
        scenePrepassCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
        recordScenePrepassCommands(scenePrepassCommandBuffer);
        scenePrepassCommandBuffer.end();

        sharedData.gpu.queues.graphicsPresent.submit(vk::SubmitInfo {
            {},
            {},
            scenePrepassCommandBuffer,
            *scenePrepassFinishSema,
        });
    }

    // Jump flood calculation pass.
    // TODO: If there are multiple compute queues, distribute the tasks to avoid the compute pipeline stalling.
    std::optional<bool> hoveringNodeJumpFloodForward{}, selectedNodeJumpFloodForward{};
    {
        jumpFloodCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
        if (hoveringNode) {
            hoveringNodeJumpFloodForward = recordJumpFloodComputeCommands(
                jumpFloodCommandBuffer,
                passthruResources->hoveringNodeOutlineJumpFloodResources.image,
                hoveringNodeJumpFloodSet,
                std::bit_ceil(static_cast<std::uint32_t>(hoveringNode->outlineThickness)));
            sharedData.gpu.device.updateDescriptorSets(
                hoveringNodeOutlineSet.getWriteOne<0>({
                    {},
                    *hoveringNodeJumpFloodForward
                        ? *passthruResources->hoveringNodeOutlineJumpFloodResources.pongImageView
                        : *passthruResources->hoveringNodeOutlineJumpFloodResources.pingImageView,
                    vk::ImageLayout::eShaderReadOnlyOptimal,
                }),
                {});
        }
        if (selectedNodes) {
            selectedNodeJumpFloodForward = recordJumpFloodComputeCommands(
                jumpFloodCommandBuffer,
                passthruResources->selectedNodeOutlineJumpFloodResources.image,
                selectedNodeJumpFloodSet,
                std::bit_ceil(static_cast<std::uint32_t>(selectedNodes->outlineThickness)));
            sharedData.gpu.device.updateDescriptorSets(
                selectedNodeOutlineSet.getWriteOne<0>({
                    {},
                    *selectedNodeJumpFloodForward
                        ? *passthruResources->selectedNodeOutlineJumpFloodResources.pongImageView
                        : *passthruResources->selectedNodeOutlineJumpFloodResources.pingImageView,
                    vk::ImageLayout::eShaderReadOnlyOptimal,
                }),
                {});
        }
        jumpFloodCommandBuffer.end();

        sharedData.gpu.queues.compute.submit(vk::SubmitInfo {
            *scenePrepassFinishSema,
            vku::unsafeProxy(vk::Flags { vk::PipelineStageFlagBits::eComputeShader }),
            jumpFloodCommandBuffer,
            *jumpFloodFinishSema,
        });
    }

    // glTF scene rendering pass.
    {
        sceneRenderingCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

        sceneRenderingCommandBuffer.setViewport(0, vku::toViewport(passthruResources->extent, true));
        sceneRenderingCommandBuffer.setScissor(0, vk::Rect2D { { 0, 0 }, passthruResources->extent });

        vk::ClearColorValue backgroundColor { 0.f, 0.f, 0.f, 0.f };
        if (auto *clearColor = get_if<glm::vec3>(&background)) {
            backgroundColor.setFloat32({ clearColor->x, clearColor->y, clearColor->z, 1.f });
        }
        sceneRenderingCommandBuffer.beginRenderPass({
            *sharedData.sceneRenderPass,
            *passthruResources->sceneFramebuffer,
            vk::Rect2D { { 0, 0 }, passthruResources->extent },
            vku::unsafeProxy<vk::ClearValue>({
                backgroundColor,
                vk::ClearColorValue{},
                vk::ClearDepthStencilValue { 0.f, 0 },
                vk::ClearDepthStencilValue{},
                vk::ClearColorValue { 0.f, 0.f, 0.f, 0.f },
                vk::ClearColorValue{},
                vk::ClearColorValue { 1.f, 0.f, 0.f, 0.f },
                vk::ClearColorValue{},
                vk::ClearColorValue { 0.f, 0.f, 0.f, 0.f },
            }),
        }, vk::SubpassContents::eInline);

        if (renderingNodes) {
            recordSceneOpaqueMeshDrawCommands(sceneRenderingCommandBuffer);
        }
        if (holds_alternative<vku::DescriptorSet<dsl::Skybox>>(background)) {
            recordSkyboxDrawCommands(sceneRenderingCommandBuffer);
        }

        // Render meshes whose AlphaMode=Blend.
        sceneRenderingCommandBuffer.nextSubpass(vk::SubpassContents::eInline);
        bool hasBlendMesh = false;
        if (renderingNodes) {
            hasBlendMesh = recordSceneBlendMeshDrawCommands(sceneRenderingCommandBuffer);
        }

        sceneRenderingCommandBuffer.nextSubpass(vk::SubpassContents::eInline);

        if (hasBlendMesh) {
            // Weighted blended composition.
            sceneRenderingCommandBuffer.bindPipeline(
                vk::PipelineBindPoint::eGraphics,
                sharedData.weightedBlendedCompositionRenderer.pipeline);
            sceneRenderingCommandBuffer.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                sharedData.weightedBlendedCompositionRenderer.pipelineLayout,
                0, weightedBlendedCompositionSet, {});
            sceneRenderingCommandBuffer.draw(3, 1, 0, 0);
        }

        sceneRenderingCommandBuffer.nextSubpass(vk::SubpassContents::eInline);

        // Inverse tone-map the result image to bloomImage[mipLevel=0] when bloom is enabled.
        if (global::bloom) {
            sceneRenderingCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *sharedData.inverseToneMappingRenderer.pipeline);
            sceneRenderingCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.inverseToneMappingRenderer.pipelineLayout, 0, inverseToneMappingSet, {});
            sceneRenderingCommandBuffer.draw(3, 1, 0, 0);
        }

        sceneRenderingCommandBuffer.endRenderPass();

        if (global::bloom) {
            sceneRenderingCommandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eComputeShader,
                {},
                vk::MemoryBarrier { vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eShaderRead },
                {}, {});

            sharedData.bloomComputer.compute(sceneRenderingCommandBuffer, bloomSet, passthruResources->extent, passthruResources->bloomImage.mipLevels);

            sceneRenderingCommandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eColorAttachmentOutput,
                {}, {}, {},
                vk::ImageMemoryBarrier {
                    vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eInputAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,
                    vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
                    vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                    passthruResources->bloomImage, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
                });

            sceneRenderingCommandBuffer.beginRenderPass({
                *sharedData.bloomApplyRenderPass,
                *passthruResources->bloomApplyFramebuffer,
                vk::Rect2D { { 0, 0 }, passthruResources->extent },
                vku::unsafeProxy<vk::ClearValue>({
                    vk::ClearColorValue{},
                    vk::ClearColorValue{},
                }),
            }, vk::SubpassContents::eInline);

            sceneRenderingCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *sharedData.bloomApplyRenderer.pipeline);
            sceneRenderingCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.bloomApplyRenderer.pipelineLayout, 0, bloomApplySet, {});
            sceneRenderingCommandBuffer.pushConstants<BloomApplyRenderer::PushConstant>(
                *sharedData.bloomApplyRenderer.pipelineLayout,
                vk::ShaderStageFlagBits::eFragment,
                0, BloomApplyRenderer::PushConstant { .factor = global::bloom->intensity });
            sceneRenderingCommandBuffer.draw(3, 1, 0, 0);

            sceneRenderingCommandBuffer.endRenderPass();
        }

        sceneRenderingCommandBuffer.end();
    }

    // Post-composition pass.
    {
        compositionCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

        if (selectedNodes || hoveringNode) {
            recordNodeOutlineCompositionCommands(compositionCommandBuffer, hoveringNodeJumpFloodForward, selectedNodeJumpFloodForward);

            // Make sure the outline composition is done before rendering ImGui.
            compositionCommandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput,
                {},
                vk::MemoryBarrier {
                    vk::AccessFlagBits::eColorAttachmentWrite,
                    vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,
                },
                {}, {});
        }

        compositionCommandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eTransfer,
            {}, {}, {},
            vku::unsafeProxy({
                // Change composited image layout from ColorAttachmentOptimal to TransferSrcOptimal.
                vk::ImageMemoryBarrier {
                    vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eTransferRead,
                    vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eTransferSrcOptimal,
                    vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                    passthruResources->sceneOpaqueAttachmentGroup.getColorAttachment(0).image, vku::fullSubresourceRange(),
                },
                // Change swapchain image layout from PresentSrcKHR to TransferDstOptimal.
                vk::ImageMemoryBarrier {
                    {}, vk::AccessFlagBits::eTransferWrite,
                    vk::ImageLayout::ePresentSrcKHR, vk::ImageLayout::eTransferDstOptimal,
                    vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                    sharedData.swapchainImages[swapchainImageIndex], vku::fullSubresourceRange(),
                },
            }));

        // Copy from composited image to swapchain image.
        compositionCommandBuffer.copyImage(
            passthruResources->sceneOpaqueAttachmentGroup.getColorAttachment(0).image, vk::ImageLayout::eTransferSrcOptimal,
            sharedData.swapchainImages[swapchainImageIndex], vk::ImageLayout::eTransferDstOptimal,
            vk::ImageCopy {
                { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
                { 0, 0, 0 },
                { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
                vk::Offset3D { passthruOffset, 0 },
                vk::Extent3D { passthruResources->extent, 1 },
            });

        // Change swapchain image layout from TransferDstOptimal to ColorAttachmentOptimal.
        compositionCommandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eColorAttachmentOutput,
            {}, {}, {},
            vk::ImageMemoryBarrier {
                vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,
                vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eColorAttachmentOptimal,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                sharedData.swapchainImages[swapchainImageIndex], vku::fullSubresourceRange(),
            });

        recordImGuiCompositionCommands(compositionCommandBuffer, swapchainImageIndex);

        // Change swapchain image layout from ColorAttachmentOptimal to PresentSrcKHR.
        compositionCommandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eBottomOfPipe,
            {}, {}, {},
            vk::ImageMemoryBarrier {
                vk::AccessFlagBits::eColorAttachmentWrite, {},
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                sharedData.swapchainImages[swapchainImageIndex], vku::fullSubresourceRange(),
            });

        compositionCommandBuffer.end();
    }

    sharedData.gpu.queues.graphicsPresent.submit({
        vk::SubmitInfo {
            {},
            {},
            sceneRenderingCommandBuffer,
            *sceneRenderingFinishSema,
        },
        vk::SubmitInfo {
            vku::unsafeProxy({ swapchainImageAcquireSemaphore, *sceneRenderingFinishSema, *jumpFloodFinishSema }),
            vku::unsafeProxy<vk::PipelineStageFlags>({
                vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eColorAttachmentOutput,
                vk::PipelineStageFlagBits::eFragmentShader,
            }),
            compositionCommandBuffer,
            swapchainImageReadySemaphore,
        },
    }, inFlightFence);
}

vk_gltf_viewer::vulkan::Frame::PassthruResources::JumpFloodResources::JumpFloodResources(
    const Gpu &gpu,
    const vk::Extent2D &extent
) : image { gpu.allocator, vk::ImageCreateInfo {
        {},
        vk::ImageType::e2D,
        vk::Format::eR16G16Uint,
        vk::Extent3D { extent, 1 },
        1, 2, // arrayLevels=0 for ping image, arrayLevels=1 for pong image.
        vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eColorAttachment /* write from JumpFloodSeedRenderer */
            | vk::ImageUsageFlagBits::eStorage /* used as ping pong image in JumpFloodComputer */
            | vk::ImageUsageFlagBits::eSampled /* read in OutlineRenderer */,
        gpu.queueFamilies.uniqueIndices.size() == 1 ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent,
        gpu.queueFamilies.uniqueIndices,
    } },
    imageView { gpu.device, image.getViewCreateInfo(vk::ImageViewType::e2DArray) },
    pingImageView { gpu.device, image.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }) },
    pongImageView { gpu.device, image.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 1, 1 }) } { }

vk_gltf_viewer::vulkan::Frame::PassthruResources::PassthruResources(
    const SharedData &sharedData,
    const vk::Extent2D &extent,
    vk::CommandBuffer graphicsCommandBuffer
) : extent { extent },
    mousePickingAttachmentGroup { sharedData.gpu, extent },
    mousePickingFramebuffer { sharedData.gpu.device, vk::FramebufferCreateInfo {
        {},
        *sharedData.mousePickingRenderPass,
        vku::unsafeProxy({
            *mousePickingAttachmentGroup.getColorAttachment(0).view,
            *mousePickingAttachmentGroup.depthStencilAttachment->view,
        }),
        extent.width, extent.height, 1,
    } },
    hoveringNodeOutlineJumpFloodResources { sharedData.gpu, extent },
    hoveringNodeJumpFloodSeedAttachmentGroup { sharedData.gpu, hoveringNodeOutlineJumpFloodResources.image },
    selectedNodeOutlineJumpFloodResources { sharedData.gpu, extent },
    selectedNodeJumpFloodSeedAttachmentGroup { sharedData.gpu, selectedNodeOutlineJumpFloodResources.image },
    sceneOpaqueAttachmentGroup { sharedData.gpu, extent },
    sceneWeightedBlendedAttachmentGroup { sharedData.gpu, extent, sceneOpaqueAttachmentGroup.depthStencilAttachment->image },
    bloomImage { sharedData.gpu.allocator, vk::ImageCreateInfo {
        {},
        vk::ImageType::e2D,
        vk::Format::eR16G16B16A16Sfloat,
        vk::Extent3D { extent, 1 },
        vku::Image::maxMipLevels(extent), 1,
        vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eColorAttachment // written in InverseToneMappingRenderer
            | bloom::BloomComputer::requiredImageUsageFlags
            | vk::ImageUsageFlagBits::eInputAttachment /* read in BloomApplyRenderer */,
    } },
    bloomImageView { sharedData.gpu.device, bloomImage.getViewCreateInfo() },
    bloomMipImageViews { [&]() {
        std::vector<vk::raii::ImageView> result;
        result.emplace_back(sharedData.gpu.device, bloomImage.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }));

        if (!sharedData.gpu.supportShaderImageLoadStoreLod) {
            result.append_range(
                bloomImage.getMipViewCreateInfos()
                | std::views::drop(1)
                | std::views::transform([&](const vk::ImageViewCreateInfo& createInfo) {
                    return vk::raii::ImageView{ sharedData.gpu.device, createInfo };
                }));
        }

        return result;
    }() },
    sceneFramebuffer { sharedData.gpu.device, vk::FramebufferCreateInfo {
        {},
        *sharedData.sceneRenderPass,
        vku::unsafeProxy({
            *sceneOpaqueAttachmentGroup.getColorAttachment(0).multisampleView,
            *sceneOpaqueAttachmentGroup.getColorAttachment(0).view,
            *sceneOpaqueAttachmentGroup.depthStencilAttachment->view,
            *sceneOpaqueAttachmentGroup.stencilResolveImageView,
            *sceneWeightedBlendedAttachmentGroup.getColorAttachment(0).multisampleView,
            *sceneWeightedBlendedAttachmentGroup.getColorAttachment(0).view,
            *sceneWeightedBlendedAttachmentGroup.getColorAttachment(1).multisampleView,
            *sceneWeightedBlendedAttachmentGroup.getColorAttachment(1).view,
            *bloomMipImageViews[0],
        }),
        extent.width, extent.height, 1,
    } },
    bloomApplyFramebuffer { sharedData.gpu.device, vk::FramebufferCreateInfo {
        {},
        *sharedData.bloomApplyRenderPass,
        vku::unsafeProxy({
            *sceneOpaqueAttachmentGroup.getColorAttachment(0).view,
            *bloomMipImageViews[0],
        }),
        extent.width, extent.height, 1,
    } } {
    constexpr auto layoutTransitionBarrier = [](
        vk::ImageLayout newLayout,
        vk::Image image,
        const vk::ImageSubresourceRange &subresourceRange = vku::fullSubresourceRange()
    ) {
        return vk::ImageMemoryBarrier {
            {}, {},
            {}, newLayout,
            vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
            image, subresourceRange
        };
    };
    graphicsCommandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe,
        {}, {}, {},
        {
            layoutTransitionBarrier(vk::ImageLayout::eDepthAttachmentOptimal, mousePickingAttachmentGroup.depthStencilAttachment->image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth)),
            layoutTransitionBarrier(vk::ImageLayout::eGeneral, hoveringNodeOutlineJumpFloodResources.image, { vk::ImageAspectFlagBits::eColor, 0, 1, 1, 1 } /* pong image */),
            layoutTransitionBarrier(vk::ImageLayout::eDepthAttachmentOptimal, hoveringNodeJumpFloodSeedAttachmentGroup.depthStencilAttachment->image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth)),
            layoutTransitionBarrier(vk::ImageLayout::eGeneral, selectedNodeOutlineJumpFloodResources.image, { vk::ImageAspectFlagBits::eColor, 0, 1, 1, 1 } /* pong image */),
            layoutTransitionBarrier(vk::ImageLayout::eDepthAttachmentOptimal, selectedNodeJumpFloodSeedAttachmentGroup.depthStencilAttachment->image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth)),
            layoutTransitionBarrier(vk::ImageLayout::eGeneral, bloomImage, { vk::ImageAspectFlagBits::eColor, 1, vk::RemainingArrayLayers, 0, 1 }),
        });
}

vk::raii::DescriptorPool vk_gltf_viewer::vulkan::Frame::createDescriptorPool() const {
    vku::PoolSizes poolSizes
        = sharedData.mousePickingRenderer.descriptorSetLayout.getPoolSize()
        + sharedData.multiNodeMousePickingDescriptorSetLayout.getPoolSize()
        + 2 * getPoolSizes(sharedData.jumpFloodComputer.descriptorSetLayout, sharedData.outlineRenderer.descriptorSetLayout)
        + sharedData.weightedBlendedCompositionRenderer.descriptorSetLayout.getPoolSize()
        + sharedData.inverseToneMappingRenderer.descriptorSetLayout.getPoolSize()
        + sharedData.bloomComputer.descriptorSetLayout.getPoolSize()
        + sharedData.bloomApplyRenderer.descriptorSetLayout.getPoolSize();
    vk::DescriptorPoolCreateFlags flags{};
    if (sharedData.gpu.supportVariableDescriptorCount) {
        poolSizes += sharedData.assetDescriptorSetLayout.getPoolSize();
        flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet | vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;
    }

    return { sharedData.gpu.device, poolSizes.getDescriptorPoolCreateInfo(flags) };
}

void vk_gltf_viewer::vulkan::Frame::recordScenePrepassCommands(vk::CommandBuffer cb) const {
    boost::container::static_vector<vk::ImageMemoryBarrier, 3> memoryBarriers;

    // If glTF Scene have to be rendered, prepare attachment layout transition for node index and depth rendering.
    if (renderingNodes) {
        memoryBarriers.push_back({
            {}, vk::AccessFlagBits::eColorAttachmentWrite,
            {}, vk::ImageLayout::eColorAttachmentOptimal,
            vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
            passthruResources->mousePickingAttachmentGroup.getColorAttachment(0).image, vku::fullSubresourceRange(),
        });
    }

    // If hovering node's outline have to be rendered, prepare attachment layout transition for jump flood seeding.
    const auto addJumpFloodSeedImageMemoryBarrier = [&](vk::Image image) {
        memoryBarriers.push_back({
            {}, vk::AccessFlagBits::eColorAttachmentWrite,
            {}, vk::ImageLayout::eColorAttachmentOptimal,
            vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
            image, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } /* ping image */,
        });
    };
    if (selectedNodes) {
        addJumpFloodSeedImageMemoryBarrier(passthruResources->selectedNodeOutlineJumpFloodResources.image);
    }
    // Same holds for hovering nodes' outline.
    if (hoveringNode) {
        addJumpFloodSeedImageMemoryBarrier(passthruResources->hoveringNodeOutlineJumpFloodResources.image);
    }

    // Attachment layout transitions.
    cb.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eColorAttachmentOutput,
        {}, {}, {}, memoryBarriers);

    struct ResourceBindingState {
        vk::Pipeline pipeline{};
        std::optional<vk::PrimitiveTopology> primitiveTopology{};
        std::optional<vk::CullModeFlagBits> cullMode{};
        std::optional<vk::IndexType> indexType;

        // (Mask){Depth|JumpFloodSeed}Renderer have compatible descriptor set layouts and push constant range,
        // therefore they only need to be bound once.
        bool descriptorSetBound = false;
        bool pushConstantBound = false;
    };

    auto drawPrimitives = [&, resourceBindingState = ResourceBindingState{}](const auto &indirectDrawCommandBuffers) mutable {
        for (const auto &[criteria, indirectDrawCommandBuffer] : indirectDrawCommandBuffers) {
            if (resourceBindingState.pipeline != criteria.pipeline) {
                cb.bindPipeline(vk::PipelineBindPoint::eGraphics, resourceBindingState.pipeline = criteria.pipeline);
            }

            if (!resourceBindingState.descriptorSetBound) {
                cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.primitiveNoShadingPipelineLayout,
                    0, assetDescriptorSet, {});
                resourceBindingState.descriptorSetBound = true;
            }

            if (!resourceBindingState.pushConstantBound) {
                sharedData.primitiveNoShadingPipelineLayout.pushConstants(cb, { projectionViewMatrix });
                resourceBindingState.pushConstantBound = true;
            }

            if (resourceBindingState.primitiveTopology != criteria.primitiveTopology) {
                cb.setPrimitiveTopologyEXT(resourceBindingState.primitiveTopology.emplace(criteria.primitiveTopology));
            }

            if (resourceBindingState.cullMode != criteria.cullMode) {
                cb.setCullModeEXT(resourceBindingState.cullMode.emplace(criteria.cullMode));
            }

            if (criteria.indexType && resourceBindingState.indexType != *criteria.indexType) {
                resourceBindingState.indexType = *criteria.indexType;
                cb.bindIndexBuffer(sharedData.gltfAsset.value().combinedIndexBuffers.getIndexBuffer(*resourceBindingState.indexType), 0, *resourceBindingState.indexType);
            }
            indirectDrawCommandBuffer.recordDrawCommand(cb, sharedData.gpu.supportDrawIndirectCount);;
        }
    };

    cb.setViewport(0, vku::toViewport(passthruResources->extent, true));
    cb.setScissor(0, vk::Rect2D{ { 0, 0 }, passthruResources->extent });

    // Seeding jump flood initial image for hovering node.
    if (hoveringNode) {
        cb.beginRenderingKHR(passthruResources->hoveringNodeJumpFloodSeedAttachmentGroup.getRenderingInfo(
            vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, { 0U, 0U, 0U, 0U } },
            vku::AttachmentGroup::DepthStencilAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, { 0.f, 0U } }));
        drawPrimitives(hoveringNode->jumpFloodSeedIndirectDrawCommandBuffers);
        cb.endRenderingKHR();
    }

    // Seeding jump flood initial image for selected node.
    if (selectedNodes) {
        cb.beginRenderingKHR(passthruResources->selectedNodeJumpFloodSeedAttachmentGroup.getRenderingInfo(
            vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, { 0U, 0U, 0U, 0U } },
            vku::AttachmentGroup::DepthStencilAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, { 0.f, 0U } }));
        drawPrimitives(selectedNodes->jumpFloodSeedIndirectDrawCommandBuffers);
        cb.endRenderingKHR();
    }

    // Mouse picking.
    const bool makeMousePickingResultBufferAvailableToHost = renderingNodes && visit(multilambda {
        [&](const vk::Offset2D &offset) {
            // Clear the first [0, 4] region of mousePickingResultBuffer as NO_INDEX.
            cb.fillBuffer(gltfAsset->mousePickingResultBuffer, 0, sizeof(std::uint32_t), NO_INDEX);

            cb.pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
                {}, vk::MemoryBarrier { vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderWrite }, {}, {});

            cb.setScissor(0, vk::Rect2D { offset, { 1, 1 } });

            cb.beginRenderPass(vk::RenderPassBeginInfo {
                *sharedData.mousePickingRenderPass,
                *passthruResources->mousePickingFramebuffer,
                { { 0, 0 }, passthruResources->extent },
                vku::unsafeProxy<vk::ClearValue>({
                    vk::ClearColorValue { static_cast<std::uint32_t>(NO_INDEX), 0U, 0U, 0U },
                    vk::ClearDepthStencilValue { 0.f, 0U },
                }),
            }, vk::SubpassContents::eInline);

            // Subpass 1: draw node index to the 1x1 pixel (which lies at the right below the cursor).
            drawPrimitives(renderingNodes->mousePickingIndirectDrawCommandBuffers);

            cb.nextSubpass(vk::SubpassContents::eInline);

            // Subpass 2: read it and copy to the mousePickingResultBuffer.
            cb.bindPipeline(vk::PipelineBindPoint::eGraphics, *sharedData.mousePickingRenderer.pipeline);
            cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.mousePickingRenderer.pipelineLayout, 0, mousePickingSet, {});
            cb.draw(3, 1, 0, 0);

            cb.endRenderPass();

            return true;
        },
        [&](const vk::Rect2D &rect) {
            // Clear mousePickingResultBuffer as zeros.
            cb.fillBuffer(gltfAsset->mousePickingResultBuffer, 0, gltfAsset->mousePickingResultBuffer.size, 0U);

            if (rect.extent.width == 0 || rect.extent.height == 0) {
                // Do nothing.
                return false;
            }

            cb.pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
                {}, vk::MemoryBarrier {
                    vk::AccessFlagBits::eTransferWrite,
                    vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
                }, {}, {});

            auto drawPrimitives = [&, resourceBindingState = ResourceBindingState{}](const auto &indirectDrawCommandBuffers) mutable {
                for (const auto &[criteria, indirectDrawCommandBuffer] : indirectDrawCommandBuffers) {
                    if (resourceBindingState.pipeline != criteria.pipeline) {
                        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, resourceBindingState.pipeline = criteria.pipeline);
                    }

                    if (!resourceBindingState.descriptorSetBound) {
                        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.multiNodeMousePickingPipelineLayout,
                            0, { assetDescriptorSet, multiNodeMousePickingSet }, {});
                        resourceBindingState.descriptorSetBound = true;
                    }

                    if (!resourceBindingState.pushConstantBound) {
                        cb.pushConstants<pl::MultiNodeMousePicking::PushConstant>(*sharedData.multiNodeMousePickingPipelineLayout, vk::ShaderStageFlagBits::eVertex,
                            0, pl::MultiNodeMousePicking::PushConstant {
                                .projectionView = projectionViewMatrix,
                            });
                        resourceBindingState.pushConstantBound = true;
                    }

                    if (resourceBindingState.primitiveTopology != criteria.primitiveTopology) {
                        cb.setPrimitiveTopologyEXT(resourceBindingState.primitiveTopology.emplace(criteria.primitiveTopology));
                    }

                    if (criteria.indexType && resourceBindingState.indexType != *criteria.indexType) {
                        resourceBindingState.indexType = *criteria.indexType;
                        cb.bindIndexBuffer(sharedData.gltfAsset.value().combinedIndexBuffers.getIndexBuffer(*resourceBindingState.indexType), 0, *resourceBindingState.indexType);
                    }
                    indirectDrawCommandBuffer.recordDrawCommand(cb, sharedData.gpu.supportDrawIndirectCount);;
                }
            };

            cb.setScissor(0, rect);
            cb.beginRenderingKHR(vk::RenderingInfo {
                {},
                rect,
                1,
                0,
                vk::ArrayProxyNoTemporaries<const vk::RenderingAttachmentInfo>{},
                sharedData.gpu.workaround.attachmentLessRenderPass ? vku::unsafeAddress(vk::RenderingAttachmentInfo {
                    *passthruResources->mousePickingAttachmentGroup.depthStencilAttachment->view, vk::ImageLayout::eDepthAttachmentOptimal,
                    {}, {}, {},
                    vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                }) : nullptr,
            });
            drawPrimitives(renderingNodes->multiNodeMousePickingIndirectDrawCommandBuffers);
            cb.endRenderingKHR();

            return true;
        },
        [](std::monostate) {
            return false;
        }
    }, mousePickingInput);

    if (makeMousePickingResultBufferAvailableToHost) {
        cb.pipelineBarrier(
            vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eHost,
            {}, vk::MemoryBarrier {
                vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eHostRead,
            }, {}, {});
    }
}

bool vk_gltf_viewer::vulkan::Frame::recordJumpFloodComputeCommands(
    vk::CommandBuffer cb,
    const vku::Image &image,
    vku::DescriptorSet<JumpFloodComputer::DescriptorSetLayout> descriptorSet,
    std::uint32_t initialSampleOffset
) const {
    cb.pipelineBarrier2KHR({
        {}, {}, {},
        vku::unsafeProxy({
            vk::ImageMemoryBarrier2 {
                // Dependency chain: this srcStageMask must match to the cb's submission waitDstStageMask.
                vk::PipelineStageFlagBits2::eComputeShader, {},
                vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageRead,
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eGeneral,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                image, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
            },
            vk::ImageMemoryBarrier2 {
                {}, {},
                vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite,
                {}, vk::ImageLayout::eGeneral,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                image, { vk::ImageAspectFlagBits::eColor, 0, 1, 1, 1 },
            }
        }),
    });

    // Compute jump flood and get the last execution direction.
    return sharedData.jumpFloodComputer.compute(cb, descriptorSet, initialSampleOffset, vku::toExtent2D(image.extent));
}

void vk_gltf_viewer::vulkan::Frame::recordSceneOpaqueMeshDrawCommands(vk::CommandBuffer cb) const {
    assert(renderingNodes && "No nodes have to be rendered.");

    struct {
        vk::Pipeline pipeline{};
        std::optional<vk::PrimitiveTopology> primitiveTopology{};
        std::optional<std::uint32_t> stencilReference{};
        std::optional<vk::CullModeFlagBits> cullMode{};
        std::optional<vk::IndexType> indexType;

        // (Mask)(Faceted)PrimitiveRenderer have compatible descriptor set layouts and push constant range,
        // therefore they only need to be bound once.
        bool descriptorBound = false;
        bool pushConstantBound = false;
    } resourceBindingState{};

    // Render alphaMode=Opaque | Mask meshes.
    for (const auto &[criteria, indirectDrawCommandBuffer] : ranges::make_subrange(renderingNodes->indirectDrawCommandBuffers.equal_range(0U))) {
        if (resourceBindingState.pipeline != criteria.pipeline) {
            cb.bindPipeline(vk::PipelineBindPoint::eGraphics, resourceBindingState.pipeline = criteria.pipeline);
        }
        if (!resourceBindingState.descriptorBound) {
            cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.primitivePipelineLayout, 0,
                { sharedData.imageBasedLightingDescriptorSet, assetDescriptorSet }, {});
            resourceBindingState.descriptorBound = true;
        }
        if (!resourceBindingState.pushConstantBound) {
            sharedData.primitivePipelineLayout.pushConstants(cb, { projectionViewMatrix, viewPosition });
            resourceBindingState.pushConstantBound = true;
        }

        if (resourceBindingState.primitiveTopology != criteria.primitiveTopology) {
            cb.setPrimitiveTopologyEXT(resourceBindingState.primitiveTopology.emplace(criteria.primitiveTopology));
        }

        if (criteria.stencilReference) {
            if (resourceBindingState.stencilReference != *criteria.stencilReference) {
                cb.setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack, resourceBindingState.stencilReference.emplace(*criteria.stencilReference));
            }
        }
        else {
            // If a pipeline which does not uses stencil reference dynamic state had been bound to the command buffer,
            // the recorded command buffer's stencil reference value is invalidated.
            resourceBindingState.stencilReference.reset();
        }

        if (resourceBindingState.cullMode != criteria.cullMode) {
            cb.setCullModeEXT(resourceBindingState.cullMode.emplace(criteria.cullMode));
        }

        if (criteria.indexType && resourceBindingState.indexType != *criteria.indexType) {
            resourceBindingState.indexType = *criteria.indexType;
            cb.bindIndexBuffer(sharedData.gltfAsset.value().combinedIndexBuffers.getIndexBuffer(*resourceBindingState.indexType), 0, *resourceBindingState.indexType);
        }
        indirectDrawCommandBuffer.recordDrawCommand(cb, sharedData.gpu.supportDrawIndirectCount);;
    }
}

bool vk_gltf_viewer::vulkan::Frame::recordSceneBlendMeshDrawCommands(vk::CommandBuffer cb) const {
    assert(renderingNodes && "No nodes have to be rendered.");

    struct {
        vk::Pipeline pipeline{};
        std::optional<vk::PrimitiveTopology> primitiveTopology{};
        std::optional<std::uint32_t> stencilReference{};
        std::optional<vk::IndexType> indexType;

        // Blend(Faceted)PrimitiveRenderer have compatible descriptor set layouts and push constant range,
        // therefore they only need to be bound once.
        bool descriptorBound = false;
        bool pushConstantBound = false;
    } resourceBindingState{};

    // Render alphaMode=Blend meshes.
    bool hasBlendMesh = false;
    for (const auto &[criteria, indirectDrawCommandBuffer] : ranges::make_subrange(renderingNodes->indirectDrawCommandBuffers.equal_range(1U))) {
        if (resourceBindingState.pipeline != criteria.pipeline) {
            resourceBindingState.pipeline = criteria.pipeline;
            cb.bindPipeline(vk::PipelineBindPoint::eGraphics, resourceBindingState.pipeline);
        }

        if (resourceBindingState.primitiveTopology != criteria.primitiveTopology) {
            cb.setPrimitiveTopologyEXT(resourceBindingState.primitiveTopology.emplace(criteria.primitiveTopology));
        }

        if (criteria.stencilReference) {
            if (resourceBindingState.stencilReference != *criteria.stencilReference) {
                cb.setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack, resourceBindingState.stencilReference.emplace(*criteria.stencilReference));
            }
        }
        else {
            // If a pipeline which does not uses stencil reference dynamic state had been bound to the command buffer,
            // the recorded command buffer's stencil reference value is invalidated.
            resourceBindingState.stencilReference.reset();
        }

        if (!resourceBindingState.descriptorBound) {
            cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.primitivePipelineLayout, 0,
                { sharedData.imageBasedLightingDescriptorSet, assetDescriptorSet }, {});
            resourceBindingState.descriptorBound = true;
        }
        if (!resourceBindingState.pushConstantBound) {
            sharedData.primitivePipelineLayout.pushConstants(cb, { projectionViewMatrix, viewPosition });
            resourceBindingState.pushConstantBound = true;
        }

        if (criteria.indexType && resourceBindingState.indexType != *criteria.indexType) {
            resourceBindingState.indexType = *criteria.indexType;
            cb.bindIndexBuffer(sharedData.gltfAsset.value().combinedIndexBuffers.getIndexBuffer(*resourceBindingState.indexType), 0, *resourceBindingState.indexType);
        }
        indirectDrawCommandBuffer.recordDrawCommand(cb, sharedData.gpu.supportDrawIndirectCount);;
        hasBlendMesh = true;
    }

    return hasBlendMesh;
}

void vk_gltf_viewer::vulkan::Frame::recordSkyboxDrawCommands(vk::CommandBuffer cb) const {
    assert(holds_alternative<vku::DescriptorSet<dsl::Skybox>>(background) && "recordSkyboxDrawCommand called, but background is not set to the proper skybox descriptor set.");
    sharedData.skyboxRenderer.draw(cb, get<vku::DescriptorSet<dsl::Skybox>>(background), { translationlessProjectionViewMatrix });
}

void vk_gltf_viewer::vulkan::Frame::recordNodeOutlineCompositionCommands(
    vk::CommandBuffer cb,
    std::optional<bool> hoveringNodeJumpFloodForward,
    std::optional<bool> selectedNodeJumpFloodForward
) const {
    boost::container::static_vector<vk::ImageMemoryBarrier, 2> memoryBarriers;
    // Change jump flood image layouts to ShaderReadOnlyOptimal.
    if (hoveringNodeJumpFloodForward) {
        memoryBarriers.push_back({
            {}, vk::AccessFlagBits::eShaderRead,
            vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
            passthruResources->hoveringNodeOutlineJumpFloodResources.image,
            { vk::ImageAspectFlagBits::eColor, 0, 1, *hoveringNodeJumpFloodForward, 1 },
        });
    }
    if (selectedNodeJumpFloodForward) {
        memoryBarriers.push_back({
            {}, vk::AccessFlagBits::eShaderRead,
            vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
            passthruResources->selectedNodeOutlineJumpFloodResources.image,
            { vk::ImageAspectFlagBits::eColor, 0, 1, *selectedNodeJumpFloodForward, 1 },
        });
    }
    if (!memoryBarriers.empty()) {
        cb.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eFragmentShader,
            {}, {}, {}, memoryBarriers);
    }

    // Set viewport and scissor.
    cb.setViewport(0, vku::toViewport(passthruResources->extent));
    cb.setScissor(0, vk::Rect2D { { 0, 0 }, passthruResources->extent });

    cb.beginRenderingKHR(vk::RenderingInfo {
        {},
        { { 0, 0 }, passthruResources->extent },
        1,
        0,
        vku::unsafeProxy(vk::RenderingAttachmentInfo {
            *passthruResources->sceneOpaqueAttachmentGroup.getColorAttachment(0).view,
            vk::ImageLayout::eColorAttachmentOptimal,
            {}, {}, {},
            vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore,
        }),
    });

    // Draw hovering/selected node outline if exists.
    if (selectedNodes) {
        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, *sharedData.outlineRenderer.pipeline);
        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.outlineRenderer.pipelineLayout, 0,
            selectedNodeOutlineSet, {});
        cb.pushConstants<OutlineRenderer::PushConstant>(
            *sharedData.outlineRenderer.pipelineLayout, vk::ShaderStageFlagBits::eFragment,
            0, OutlineRenderer::PushConstant {
                .outlineColor = selectedNodes->outlineColor,
                .outlineThickness = selectedNodes->outlineThickness,
            });
        cb.draw(3, 1, 0, 0);
    }
    if (hoveringNode) {
        if (selectedNodes) {
            // TODO: pipeline barrier required.
        }
        else {
            cb.bindPipeline(vk::PipelineBindPoint::eGraphics, *sharedData.outlineRenderer.pipeline);
        }

        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.outlineRenderer.pipelineLayout, 0,
            hoveringNodeOutlineSet, {});
        cb.pushConstants<OutlineRenderer::PushConstant>(
            *sharedData.outlineRenderer.pipelineLayout, vk::ShaderStageFlagBits::eFragment,
            0, OutlineRenderer::PushConstant {
                .outlineColor = hoveringNode->outlineColor,
                .outlineThickness = hoveringNode->outlineThickness,
            });
        cb.draw(3, 1, 0, 0);
    }

    cb.endRenderingKHR();
}

void vk_gltf_viewer::vulkan::Frame::recordImGuiCompositionCommands(
    vk::CommandBuffer cb,
    std::uint32_t swapchainImageIndex
) const {
    cb.beginRenderingKHR(sharedData.imGuiAttachmentGroup.getRenderingInfo(
        vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore },
        swapchainImageIndex));

    // Draw ImGui.
    if (ImDrawData *drawData = ImGui::GetDrawData()) {
        ImGui_ImplVulkan_RenderDrawData(drawData, cb);
    }

    cb.endRenderingKHR();
}