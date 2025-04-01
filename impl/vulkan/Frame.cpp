module;

#include <boost/container/static_vector.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.Frame;

import std;
import imgui.vulkan;
import :gltf.algorithm.bounding_box;
import :helpers.concepts;
import :helpers.fastgltf;
import :helpers.functional;
import :helpers.optional;
import :helpers.ranges;
import :vulkan.ag.DepthPrepass;
import :vulkan.buffer.IndirectDrawCommands;
import :vulkan.shader_type.Accessor;

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
    , hoveringNodeIndexBuffer { sharedData.gpu.allocator, NO_INDEX, vk::BufferUsageFlagBits::eTransferDst, vku::allocation::hostRead }
    , sceneOpaqueAttachmentGroup { sharedData.gpu, sharedData.swapchainExtent, sharedData.swapchainImages }
    , sceneWeightedBlendedAttachmentGroup { sharedData.gpu, sharedData.swapchainExtent, sceneOpaqueAttachmentGroup.depthStencilAttachment->image }
    , framebuffers { createFramebuffers() }
    , descriptorPool { createDescriptorPool() }
    , computeCommandPool { sharedData.gpu.device, vk::CommandPoolCreateInfo { {}, sharedData.gpu.queueFamilies.compute } }
    , graphicsCommandPool { sharedData.gpu.device, vk::CommandPoolCreateInfo { {}, sharedData.gpu.queueFamilies.graphicsPresent } }
    , scenePrepassFinishSema { sharedData.gpu.device, vk::SemaphoreCreateInfo{} }
    , swapchainImageAcquireSema { sharedData.gpu.device, vk::SemaphoreCreateInfo{} }
    , sceneRenderingFinishSema { sharedData.gpu.device, vk::SemaphoreCreateInfo{} }
    , compositionFinishSema { sharedData.gpu.device, vk::SemaphoreCreateInfo{} }
    , jumpFloodFinishSema { sharedData.gpu.device, vk::SemaphoreCreateInfo{} }
    , inFlightFence { sharedData.gpu.device, vk::FenceCreateInfo { vk::FenceCreateFlagBits::eSignaled } } {
    // Change initial attachment layouts.
    const vk::raii::Fence fence { sharedData.gpu.device, vk::FenceCreateInfo{} };
    vku::executeSingleCommand(*sharedData.gpu.device, *graphicsCommandPool, sharedData.gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
        recordSwapchainExtentDependentImageLayoutTransitionCommands(cb);
    }, *fence);
    std::ignore = sharedData.gpu.device.waitForFences(*fence, true, ~0ULL); // TODO: failure handling

    // Allocate descriptor sets.
    std::tie(hoveringNodeJumpFloodSet, selectedNodeJumpFloodSet, hoveringNodeOutlineSet, selectedNodeOutlineSet, weightedBlendedCompositionSet)
        = allocateDescriptorSets(*descriptorPool, std::tie(
            sharedData.jumpFloodComputer.descriptorSetLayout,
            sharedData.jumpFloodComputer.descriptorSetLayout,
            sharedData.outlineRenderer.descriptorSetLayout,
            sharedData.outlineRenderer.descriptorSetLayout,
            sharedData.weightedBlendedCompositionRenderer.descriptorSetLayout));

    // Update descriptor set.
    sharedData.gpu.device.updateDescriptorSets(
        weightedBlendedCompositionSet.getWrite<0>(vku::unsafeProxy({
            vk::DescriptorImageInfo { {}, *sceneWeightedBlendedAttachmentGroup.getColorAttachment(0).view, vk::ImageLayout::eShaderReadOnlyOptimal },
            vk::DescriptorImageInfo { {}, *sceneWeightedBlendedAttachmentGroup.getColorAttachment(1).view, vk::ImageLayout::eShaderReadOnlyOptimal },
        })),
        {});

    // Allocate per-frame command buffers.
    std::tie(jumpFloodCommandBuffer) = vku::allocateCommandBuffers<1>(*sharedData.gpu.device, *computeCommandPool);
    std::tie(scenePrepassCommandBuffer, sceneRenderingCommandBuffer, compositionCommandBuffer)
        = vku::allocateCommandBuffers<3>(*sharedData.gpu.device, *graphicsCommandPool);
}

auto vk_gltf_viewer::vulkan::Frame::update(const ExecutionTask &task) -> UpdateResult {
    UpdateResult result{};

    // --------------------
    // Update CPU resources.
    // --------------------

    if (task.handleSwapchainResize) {
        // Attachment images that have to be matched to the swapchain extent must be recreated.
        sceneOpaqueAttachmentGroup = { sharedData.gpu, sharedData.swapchainExtent, sharedData.swapchainImages };
        sceneWeightedBlendedAttachmentGroup = { sharedData.gpu, sharedData.swapchainExtent, sceneOpaqueAttachmentGroup.depthStencilAttachment->image };
        framebuffers = createFramebuffers();

        sharedData.gpu.device.updateDescriptorSets(
            weightedBlendedCompositionSet.getWrite<0>(vku::unsafeProxy({
                vk::DescriptorImageInfo { {}, *sceneWeightedBlendedAttachmentGroup.getColorAttachment(0).view, vk::ImageLayout::eShaderReadOnlyOptimal },
                vk::DescriptorImageInfo { {}, *sceneWeightedBlendedAttachmentGroup.getColorAttachment(1).view, vk::ImageLayout::eShaderReadOnlyOptimal },
            })),
            {});

        // Change initial attachment layouts.
        // TODO: can this operation be non-blocking?
        const vk::raii::Fence fence { sharedData.gpu.device, vk::FenceCreateInfo{} };
        vku::executeSingleCommand(*sharedData.gpu.device, *graphicsCommandPool, sharedData.gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
            recordSwapchainExtentDependentImageLayoutTransitionCommands(cb);
        }, *fence);
        std::ignore = sharedData.gpu.device.waitForFences(*fence, true, ~0ULL); // TODO: failure handling
    }

    // Get node index under the cursor from hoveringNodeIndexBuffer.
    // If it is not NO_INDEX (i.e. node index is found), update hoveringNodeIndex.
    if (auto value = std::exchange(hoveringNodeIndexBuffer.asValue<std::uint16_t>(), NO_INDEX); value != NO_INDEX) {
        result.hoveringNodeIndex = value;
    }

    // If passthru extent is different from the current's, dependent images have to be recreated.
    if (!passthruResources || passthruResources->extent != task.passthruRect.extent) {
        // TODO: can this operation be non-blocking?
        const vk::raii::Fence fence { sharedData.gpu.device, vk::FenceCreateInfo{} };
        vku::executeSingleCommand(*sharedData.gpu.device, *graphicsCommandPool, sharedData.gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
            passthruResources.emplace(sharedData.gpu, task.passthruRect.extent, cb);
        }, *fence);
        std::ignore = sharedData.gpu.device.waitForFences(*fence, true, ~0ULL); // TODO: failure handling

        sharedData.gpu.device.updateDescriptorSets({
            hoveringNodeJumpFloodSet.getWriteOne<0>({ {}, *passthruResources->hoveringNodeOutlineJumpFloodResources.imageView, vk::ImageLayout::eGeneral }),
            selectedNodeJumpFloodSet.getWriteOne<0>({ {}, *passthruResources->selectedNodeOutlineJumpFloodResources.imageView, vk::ImageLayout::eGeneral }),
        }, {});
    }

    projectionViewMatrix = task.camera.projection * task.camera.view;
    viewPosition = inverse(task.camera.view)[3];
    translationlessProjectionViewMatrix = task.camera.projection * glm::mat4 { glm::mat3 { task.camera.view } };
    passthruRect = task.passthruRect;
    cursorPosFromPassthruRectTopLeft = task.cursorPosFromPassthruRectTopLeft;

    constexpr auto fetchTextureTransform = [](const fastgltf::TextureInfo &textureInfo) {
        if (textureInfo.transform) {
            return textureInfo.transform->rotation != 0.f
                ? shader_type::TextureTransform::All
                : shader_type::TextureTransform::ScaleAndOffset;
        }
        else {
            return shader_type::TextureTransform::None;
        }
    };

    const auto criteriaGetter = [&](const fastgltf::Primitive &primitive) {
        CommandSeparationCriteria result {
            .subpass = 0U,
            .indexType = value_if(primitive.type == fastgltf::PrimitiveType::LineLoop || primitive.indicesAccessor.has_value(), [&]() {
                return sharedData.gltfAsset.value().combinedIndexBuffers.getIndexInfo(primitive).first;
            }),
            .primitiveTopology = getPrimitiveTopology(primitive.type),
            .cullMode = vk::CullModeFlagBits::eBack,
        };

        const auto &accessors = sharedData.gltfAsset->primitiveAttributes.getAccessors(primitive);
        if (primitive.materialIndex) {
            const fastgltf::Material &material = task.gltf->asset.materials[*primitive.materialIndex];
            result.subpass = material.alphaMode == fastgltf::AlphaMode::Blend;

            if (material.unlit) {
                result.pipeline = sharedData.getUnlitPrimitiveRenderer({
                    .topologyClass = getTopologyClass(getPrimitiveTopology(primitive.type)),
                    .positionComponentType = accessors.positionAccessor.componentType,
                    .baseColorTexcoordComponentType = material.pbrData.baseColorTexture.transform([&](const fastgltf::TextureInfo &textureInfo) {
                        return accessors.texcoordAccessors.at(textureInfo.texCoordIndex).componentType;
                    }),
                    .colorComponentCountAndType = accessors.colorAccessor.transform([](const auto &info) {
                        return std::pair { info.componentCount, info.componentType };
                    }),
                    .positionMorphTargetWeightCount = static_cast<std::uint32_t>(accessors.positionMorphTargetAccessors.size()),
                    .skinAttributeCount = static_cast<std::uint32_t>(accessors.jointsAccessors.size()),
                    .baseColorTextureTransform = material.pbrData.baseColorTexture
                        .transform(fetchTextureTransform)
                        .value_or(shader_type::TextureTransform::None),
                    .alphaMode = material.alphaMode,
                });
            }
            else {
                result.pipeline = sharedData.getPrimitiveRenderer({
                    .topologyClass = getTopologyClass(getPrimitiveTopology(primitive.type)),
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
                    .baseColorTextureTransform = material.pbrData.baseColorTexture
                        .transform(fetchTextureTransform)
                        .value_or(shader_type::TextureTransform::None),
                    .metallicRoughnessTextureTransform = material.pbrData.metallicRoughnessTexture
                        .transform(fetchTextureTransform)
                        .value_or(shader_type::TextureTransform::None),
                    .normalTextureTransform = material.normalTexture
                        .transform(fetchTextureTransform)
                        .value_or(shader_type::TextureTransform::None),
                    .occlusionTextureTransform = material.occlusionTexture
                        .transform(fetchTextureTransform)
                        .value_or(shader_type::TextureTransform::None),
                    .emissiveTextureTransform = material.emissiveTexture
                        .transform(fetchTextureTransform)
                        .value_or(shader_type::TextureTransform::None),
                    .alphaMode = material.alphaMode,
                });
            }
            result.cullMode = material.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack;
        }
        else {
            result.pipeline = sharedData.getPrimitiveRenderer({
                .topologyClass = getTopologyClass(getPrimitiveTopology(primitive.type)),
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
            });
        }
        return result;
    };

    const auto depthPrepassCriteriaGetter = [&](const fastgltf::Primitive &primitive) {
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
                result.pipeline = sharedData.getMaskDepthRenderer({
                    .topologyClass = getTopologyClass(getPrimitiveTopology(primitive.type)),
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
                    .baseColorTextureTransform = material.pbrData.baseColorTexture
                        .transform(fetchTextureTransform)
                        .value_or(shader_type::TextureTransform::None),
                });
            }
            else {
                result.pipeline = sharedData.getDepthRenderer({
                    .topologyClass = getTopologyClass(getPrimitiveTopology(primitive.type)),
                    .positionComponentType = accessors.positionAccessor.componentType,
                    .positionMorphTargetWeightCount = static_cast<std::uint32_t>(accessors.positionMorphTargetAccessors.size()),
                    .skinAttributeCount = static_cast<std::uint32_t>(accessors.jointsAccessors.size()),
                });
            }
            result.cullMode = material.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack;
        }
        else {
            result.pipeline = sharedData.getDepthRenderer({
                .topologyClass = getTopologyClass(getPrimitiveTopology(primitive.type)),
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
                    .topologyClass = getTopologyClass(getPrimitiveTopology(primitive.type)),
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
                    .baseColorTextureTransform = material.pbrData.baseColorTexture
                        .transform(fetchTextureTransform)
                        .value_or(shader_type::TextureTransform::None),
                });
            }
            else {
                result.pipeline = sharedData.getJumpFloodSeedRenderer({
                    .topologyClass = getTopologyClass(getPrimitiveTopology(primitive.type)),
                    .positionComponentType = accessors.positionAccessor.componentType,
                    .positionMorphTargetWeightCount = static_cast<std::uint32_t>(accessors.positionMorphTargetAccessors.size()),
                    .skinAttributeCount = static_cast<std::uint32_t>(accessors.jointsAccessors.size()),
                });
            }
            result.cullMode = material.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack;
        }
        else {
            result.pipeline = sharedData.getJumpFloodSeedRenderer({
                .topologyClass = getTopologyClass(getPrimitiveTopology(primitive.type)),
                .positionComponentType = accessors.positionAccessor.componentType,
                .positionMorphTargetWeightCount = static_cast<std::uint32_t>(accessors.positionMorphTargetAccessors.size()),
                .skinAttributeCount = static_cast<std::uint32_t>(accessors.jointsAccessors.size()),
            });
        }
        return result;
    };

    const auto drawCommandGetter = [&](
        std::uint16_t nodeIndex,
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
        const std::uint32_t firstInstance = (static_cast<std::uint32_t>(nodeIndex) << 16U) | static_cast<std::uint32_t>(primitiveIndex);
        if (primitive.type == fastgltf::PrimitiveType::LineLoop || primitive.indicesAccessor) {
            const auto [_, firstIndex] = sharedData.gltfAsset.value().combinedIndexBuffers.getIndexInfo(primitive);
            return vk::DrawIndexedIndirectCommand { drawCount, instanceCount, firstIndex, 0, firstInstance };
        }
        else {
            return vk::DrawIndirectCommand { drawCount, instanceCount, 0, firstInstance };
        }
    };

    if (task.gltf && !task.gltf->renderingNodes.indices.empty()) {
        if (!renderingNodes ||
            task.gltf->regenerateDrawCommands ||
            renderingNodes->indices != task.gltf->renderingNodes.indices) {
            renderingNodes.emplace(
                task.gltf->renderingNodes.indices,
                buffer::createIndirectDrawCommandBuffers(task.gltf->asset, sharedData.gpu.allocator, criteriaGetter, task.gltf->renderingNodes.indices, drawCommandGetter),
                buffer::createIndirectDrawCommandBuffers(task.gltf->asset, sharedData.gpu.allocator, depthPrepassCriteriaGetter, task.gltf->renderingNodes.indices, drawCommandGetter));
        }

        if (task.frustum) {
            const auto commandBufferCullingFunc = [&](buffer::IndirectDrawCommands &indirectDrawCommands) -> void {
                indirectDrawCommands.partition([&](const auto &command) {
                    if (command.instanceCount > 1) {
                        // Do not perform frustum culling for instanced mesh.
                        return true;
                    }

                    const std::uint16_t nodeIndex = command.firstInstance >> 16U;
                    const std::uint16_t primitiveIndex = command.firstInstance & 0xFFFFU;
                    const auto [min, max] = gltf::algorithm::getBoundingBoxMinMax<float>(
                        *task.gltf->orderedPrimitives[primitiveIndex],
                        task.gltf->asset.nodes[nodeIndex],
                        task.gltf->asset);

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
            for (auto &buffer : renderingNodes->depthPrepassIndirectDrawCommandBuffers | std::views::values) {
                commandBufferCullingFunc(buffer);
            }
        }
        else {
            for (auto &buffer : renderingNodes->indirectDrawCommandBuffers | std::views::values) {
                buffer.resetDrawCount();
            }
            for (auto &buffer : renderingNodes->depthPrepassIndirectDrawCommandBuffers | std::views::values) {
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
                hoveringNode->jumpFloodSeedIndirectDrawCommandBuffers = buffer::createIndirectDrawCommandBuffers(task.gltf->asset, sharedData.gpu.allocator, jumpFloodSeedCriteriaGetter, { task.gltf->hoveringNode->index }, drawCommandGetter);
            }
            hoveringNode->outlineColor = task.gltf->hoveringNode->outlineColor;
            hoveringNode->outlineThickness = task.gltf->hoveringNode->outlineThickness;
        }
        else {
            hoveringNode.emplace(
                task.gltf->hoveringNode->index,
                buffer::createIndirectDrawCommandBuffers(task.gltf->asset, sharedData.gpu.allocator, jumpFloodSeedCriteriaGetter, { task.gltf->hoveringNode->index }, drawCommandGetter),
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

void vk_gltf_viewer::vulkan::Frame::recordCommandsAndSubmit(std::uint32_t swapchainImageIndex) const {
    // Record commands.
    graphicsCommandPool.reset();
    computeCommandPool.reset();

    // Depth prepass and jump flood seed image calculation pass.
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

        vk::ClearColorValue backgroundColor { 0.f, 0.f, 0.f, 0.f };
        if (auto *clearColor = get_if<glm::vec3>(&background)) {
            backgroundColor.setFloat32({ clearColor->x, clearColor->y, clearColor->z, 1.f });
        }
        sceneRenderingCommandBuffer.beginRenderPass({
            *sharedData.sceneRenderPass,
            *framebuffers[swapchainImageIndex],
            vk::Rect2D { { 0, 0 }, sharedData.swapchainExtent },
            vku::unsafeProxy<vk::ClearValue>({
                backgroundColor,
                vk::ClearColorValue{},
                vk::ClearDepthStencilValue { 0.f, 0 },
                vk::ClearColorValue { 0.f, 0.f, 0.f, 0.f },
                vk::ClearColorValue{},
                vk::ClearColorValue { 1.f, 0.f, 0.f, 0.f },
                vk::ClearColorValue{},
            }),
        }, vk::SubpassContents::eInline);

        const vk::Viewport passthruViewport {
            // Use negative viewport.
            static_cast<float>(passthruRect.offset.x), static_cast<float>(passthruRect.offset.y + passthruRect.extent.height),
            static_cast<float>(passthruRect.extent.width), -static_cast<float>(passthruRect.extent.height),
            0.f, 1.f,
        };
        sceneRenderingCommandBuffer.setViewport(0, passthruViewport);
        sceneRenderingCommandBuffer.setScissor(0, passthruRect);

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

        sceneRenderingCommandBuffer.endRenderPass();

        sceneRenderingCommandBuffer.end();
    }

    // Post-composition pass.
    {
        compositionCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

        if (selectedNodes || hoveringNode) {
            recordNodeOutlineCompositionCommands(compositionCommandBuffer, hoveringNodeJumpFloodForward, selectedNodeJumpFloodForward, swapchainImageIndex);

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
            *swapchainImageAcquireSema,
            vku::unsafeProxy(vk::Flags { vk::PipelineStageFlagBits::eColorAttachmentOutput }),
            sceneRenderingCommandBuffer,
            *sceneRenderingFinishSema,
        },
        vk::SubmitInfo {
            vku::unsafeProxy({ *sceneRenderingFinishSema, *jumpFloodFinishSema }),
            vku::unsafeProxy({
                vk::Flags { vk::PipelineStageFlagBits::eFragmentShader },
                vk::Flags { vk::PipelineStageFlagBits::eFragmentShader },
            }),
            compositionCommandBuffer,
            *compositionFinishSema,
        },
    }, *inFlightFence);
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
        vk::ImageUsageFlagBits::eColorAttachment /* write from DepthRenderer */
            | vk::ImageUsageFlagBits::eStorage /* used as ping pong image in JumpFloodComputer */
            | vk::ImageUsageFlagBits::eSampled /* read in OutlineRenderer */,
        gpu.queueFamilies.uniqueIndices.size() == 1 ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent,
        gpu.queueFamilies.uniqueIndices,
    } },
    imageView { gpu.device, image.getViewCreateInfo(vk::ImageViewType::e2DArray) },
    pingImageView { gpu.device, image.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }) },
    pongImageView { gpu.device, image.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 1, 1 }) } { }

vk_gltf_viewer::vulkan::Frame::PassthruResources::PassthruResources(
    const Gpu &gpu,
    const vk::Extent2D &extent,
    vk::CommandBuffer graphicsCommandBuffer
) : extent { extent },
    hoveringNodeOutlineJumpFloodResources { gpu, extent },
    selectedNodeOutlineJumpFloodResources { gpu, extent },
    depthPrepassAttachmentGroup { gpu, extent },
    hoveringNodeJumpFloodSeedAttachmentGroup { gpu, hoveringNodeOutlineJumpFloodResources.image },
    selectedNodeJumpFloodSeedAttachmentGroup { gpu, selectedNodeOutlineJumpFloodResources.image } {
    recordInitialImageLayoutTransitionCommands(graphicsCommandBuffer);
}

auto vk_gltf_viewer::vulkan::Frame::PassthruResources::recordInitialImageLayoutTransitionCommands(
    vk::CommandBuffer graphicsCommandBuffer
) const -> void {
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
            layoutTransitionBarrier(vk::ImageLayout::eDepthAttachmentOptimal, depthPrepassAttachmentGroup.depthStencilAttachment->image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth)),
            layoutTransitionBarrier(vk::ImageLayout::eGeneral, hoveringNodeOutlineJumpFloodResources.image, { vk::ImageAspectFlagBits::eColor, 0, 1, 1, 1 } /* pong image */),
            layoutTransitionBarrier(vk::ImageLayout::eDepthAttachmentOptimal, hoveringNodeJumpFloodSeedAttachmentGroup.depthStencilAttachment->image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth)),
            layoutTransitionBarrier(vk::ImageLayout::eGeneral, selectedNodeOutlineJumpFloodResources.image, { vk::ImageAspectFlagBits::eColor, 0, 1, 1, 1 } /* pong image */),
            layoutTransitionBarrier(vk::ImageLayout::eDepthAttachmentOptimal, selectedNodeJumpFloodSeedAttachmentGroup.depthStencilAttachment->image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth)),
        });
}

auto vk_gltf_viewer::vulkan::Frame::createFramebuffers() const -> std::vector<vk::raii::Framebuffer> {
    return sceneOpaqueAttachmentGroup.getSwapchainAttachment(0).views
        | std::views::transform([this](vk::ImageView swapchainImageView) {
            return vk::raii::Framebuffer { sharedData.gpu.device, vk::FramebufferCreateInfo {
                {},
                *sharedData.sceneRenderPass,
                vku::unsafeProxy({
                    *sceneOpaqueAttachmentGroup.getSwapchainAttachment(0).multisampleView,
                    swapchainImageView,
                    *sceneOpaqueAttachmentGroup.depthStencilAttachment->view,
                    *sceneWeightedBlendedAttachmentGroup.getColorAttachment(0).multisampleView,
                    *sceneWeightedBlendedAttachmentGroup.getColorAttachment(0).view,
                    *sceneWeightedBlendedAttachmentGroup.getColorAttachment(1).multisampleView,
                    *sceneWeightedBlendedAttachmentGroup.getColorAttachment(1).view,
                }),
                sharedData.swapchainExtent.width,
                sharedData.swapchainExtent.height,
                1,
            } };
        })
        | std::ranges::to<std::vector>();
}

vk::raii::DescriptorPool vk_gltf_viewer::vulkan::Frame::createDescriptorPool() const {
    vku::PoolSizes poolSizes
        = 2 * getPoolSizes(sharedData.jumpFloodComputer.descriptorSetLayout, sharedData.outlineRenderer.descriptorSetLayout)
        + sharedData.weightedBlendedCompositionRenderer.descriptorSetLayout.getPoolSize();
    vk::DescriptorPoolCreateFlags flags{};
    if (sharedData.gpu.supportVariableDescriptorCount) {
        poolSizes += sharedData.assetDescriptorSetLayout.getPoolSize();
        flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet | vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;
    }

    return { sharedData.gpu.device, poolSizes.getDescriptorPoolCreateInfo(flags) };
}

auto vk_gltf_viewer::vulkan::Frame::recordScenePrepassCommands(vk::CommandBuffer cb) const -> void {
    boost::container::static_vector<vk::ImageMemoryBarrier, 3> memoryBarriers;

    // If glTF Scene have to be rendered, prepare attachment layout transition for node index and depth rendering.
    if (renderingNodes) {
        memoryBarriers.push_back({
            {}, vk::AccessFlagBits::eColorAttachmentWrite,
            {}, vk::ImageLayout::eColorAttachmentOptimal,
            vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
            passthruResources->depthPrepassAttachmentGroup.getColorAttachment(0).image, vku::fullSubresourceRange(),
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

    struct {
        vk::Pipeline pipeline{};
        std::optional<vk::PrimitiveTopology> primitiveTopology{};
        std::optional<vk::CullModeFlagBits> cullMode{};
        std::optional<vk::IndexType> indexType;

        // (Mask){Depth|JumpFloodSeed}Renderer have compatible descriptor set layouts and push constant range,
        // therefore they only need to be bound once.
        bool descriptorSetBound = false;
        bool pushConstantBound = false;
    } resourceBindingState{};

    const auto drawPrimitives = [&](const auto &indirectDrawCommandBuffers) {
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
                cb.setCullMode(resourceBindingState.cullMode.emplace(criteria.cullMode));
            }

            if (criteria.indexType && resourceBindingState.indexType != *criteria.indexType) {
                resourceBindingState.indexType = *criteria.indexType;
                cb.bindIndexBuffer(sharedData.gltfAsset.value().combinedIndexBuffers.getIndexBuffer(*resourceBindingState.indexType), 0, *resourceBindingState.indexType);
            }
            indirectDrawCommandBuffer.recordDrawCommand(cb, sharedData.gpu.supportDrawIndirectCount);;
        }
    };

    cb.setViewport(0, vku::toViewport(passthruResources->extent, true));

    if (renderingNodes && cursorPosFromPassthruRectTopLeft) {
        cb.beginRenderingKHR(passthruResources->depthPrepassAttachmentGroup.getRenderingInfo(
            vku::AttachmentGroup::ColorAttachmentInfo {
                vk::AttachmentLoadOp::eClear,
                vk::AttachmentStoreOp::eStore,
                { static_cast<std::uint32_t>(NO_INDEX), 0U, 0U, 0U },
            },
            vku::AttachmentGroup::DepthStencilAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, { 0.f, 0U } }));
        cb.setScissor(0, vk::Rect2D{ *cursorPosFromPassthruRectTopLeft, { 1, 1 } });
        drawPrimitives(renderingNodes->depthPrepassIndirectDrawCommandBuffers);
        cb.endRenderingKHR();
    }

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

    // If there are rendered nodes and the cursor is inside the passthru rect, do mouse picking.
    if (renderingNodes && cursorPosFromPassthruRectTopLeft) {
        cb.pipelineBarrier(
            vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eTransfer,
            {}, {}, {},
            // For copying to hoveringNodeIndexBuffer.
            vk::ImageMemoryBarrier {
                vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eTransferRead,
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eTransferSrcOptimal,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                passthruResources->depthPrepassAttachmentGroup.getColorAttachment(0).image, vku::fullSubresourceRange(),
            });

        cb.copyImageToBuffer(
            passthruResources->depthPrepassAttachmentGroup.getColorAttachment(0).image, vk::ImageLayout::eTransferSrcOptimal,
            hoveringNodeIndexBuffer,
            vk::BufferImageCopy {
                0, {}, {},
                { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
                vk::Offset3D { *cursorPosFromPassthruRectTopLeft, 0 },
                { 1, 1, 1 },
            });

        // hoveringNodeIndexBuffer data have to be available to the host.
        cb.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost,
            {}, {},
            vk::BufferMemoryBarrier {
                vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eHostRead,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                hoveringNodeIndexBuffer, 0, vk::WholeSize,
            },
            {});
    }
}

auto vk_gltf_viewer::vulkan::Frame::recordJumpFloodComputeCommands(
    vk::CommandBuffer cb,
    const vku::Image &image,
    vku::DescriptorSet<JumpFloodComputer::DescriptorSetLayout> descriptorSet,
    std::uint32_t initialSampleOffset
) const -> bool {
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

auto vk_gltf_viewer::vulkan::Frame::recordSceneOpaqueMeshDrawCommands(vk::CommandBuffer cb) const -> void {
    assert(renderingNodes && "No nodes have to be rendered.");

    struct {
        vk::Pipeline pipeline{};
        std::optional<vk::PrimitiveTopology> primitiveTopology{};
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

        if (resourceBindingState.cullMode != criteria.cullMode) {
            cb.setCullMode(resourceBindingState.cullMode.emplace(criteria.cullMode));
        }

        if (criteria.indexType && resourceBindingState.indexType != *criteria.indexType) {
            resourceBindingState.indexType = *criteria.indexType;
            cb.bindIndexBuffer(sharedData.gltfAsset.value().combinedIndexBuffers.getIndexBuffer(*resourceBindingState.indexType), 0, *resourceBindingState.indexType);
        }
        indirectDrawCommandBuffer.recordDrawCommand(cb, sharedData.gpu.supportDrawIndirectCount);;
    }
}

auto vk_gltf_viewer::vulkan::Frame::recordSceneBlendMeshDrawCommands(vk::CommandBuffer cb) const -> bool {
    assert(renderingNodes && "No nodes have to be rendered.");

    struct {
        vk::Pipeline pipeline{};
        std::optional<vk::PrimitiveTopology> primitiveTopology{};
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

auto vk_gltf_viewer::vulkan::Frame::recordSkyboxDrawCommands(vk::CommandBuffer cb) const -> void {
    assert(holds_alternative<vku::DescriptorSet<dsl::Skybox>>(background) && "recordSkyboxDrawCommand called, but background is not set to the proper skybox descriptor set.");
    sharedData.skyboxRenderer.draw(cb, get<vku::DescriptorSet<dsl::Skybox>>(background), { translationlessProjectionViewMatrix });
}

auto vk_gltf_viewer::vulkan::Frame::recordNodeOutlineCompositionCommands(
    vk::CommandBuffer cb,
    std::optional<bool> hoveringNodeJumpFloodForward,
    std::optional<bool> selectedNodeJumpFloodForward,
    std::uint32_t swapchainImageIndex
) const -> void {
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
    const vk::Viewport passthruViewport {
        // Use negative viewport.
        static_cast<float>(passthruRect.offset.x), static_cast<float>(passthruRect.offset.y + passthruRect.extent.height),
        static_cast<float>(passthruRect.extent.width), -static_cast<float>(passthruRect.extent.height),
        0.f, 1.f,
    };
    cb.setViewport(0, passthruViewport);
    cb.setScissor(0, passthruRect);

    cb.beginRenderingKHR(sharedData.swapchainAttachmentGroup.getRenderingInfo(
        vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore },
        swapchainImageIndex));

    // Draw hovering/selected node outline if exists.
    bool pipelineBound = false;
    if (selectedNodes) {
        if (!pipelineBound) {
            cb.bindPipeline(vk::PipelineBindPoint::eGraphics, *sharedData.outlineRenderer.pipeline);
            pipelineBound = true;
        }
        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.outlineRenderer.pipelineLayout, 0,
            selectedNodeOutlineSet, {});
        cb.pushConstants<OutlineRenderer::PushConstant>(
            *sharedData.outlineRenderer.pipelineLayout, vk::ShaderStageFlagBits::eFragment,
            0, OutlineRenderer::PushConstant {
                .outlineColor = selectedNodes->outlineColor,
                .passthruOffset = { passthruRect.offset.x, passthruRect.offset.y },
                .outlineThickness = selectedNodes->outlineThickness,
            });
        cb.draw(3, 1, 0, 0);
    }
    if (hoveringNode) {
        if (selectedNodes) {
            // TODO: pipeline barrier required.
        }

        if (!pipelineBound) {
            cb.bindPipeline(vk::PipelineBindPoint::eGraphics, *sharedData.outlineRenderer.pipeline);
            pipelineBound = true;
        }

        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.outlineRenderer.pipelineLayout, 0,
            hoveringNodeOutlineSet, {});
        cb.pushConstants<OutlineRenderer::PushConstant>(
            *sharedData.outlineRenderer.pipelineLayout, vk::ShaderStageFlagBits::eFragment,
            0, OutlineRenderer::PushConstant {
                .outlineColor = hoveringNode->outlineColor,
                .passthruOffset = { passthruRect.offset.x, passthruRect.offset.y },
                .outlineThickness = hoveringNode->outlineThickness,
            });
        cb.draw(3, 1, 0, 0);
    }

    cb.endRenderingKHR();
}

auto vk_gltf_viewer::vulkan::Frame::recordImGuiCompositionCommands(
    vk::CommandBuffer cb,
    std::uint32_t swapchainImageIndex
) const -> void {
    // Start dynamic rendering with B8G8R8A8_UNORM format.
    cb.beginRenderingKHR(visit_as<const ag::Swapchain&>(sharedData.imGuiSwapchainAttachmentGroup).getRenderingInfo(
        vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore },
        swapchainImageIndex));

    // Draw ImGui.
    if (ImDrawData *drawData = ImGui::GetDrawData()) {
        ImGui_ImplVulkan_RenderDrawData(drawData, cb);
    }

    cb.endRenderingKHR();
}

auto vk_gltf_viewer::vulkan::Frame::recordSwapchainExtentDependentImageLayoutTransitionCommands(
    vk::CommandBuffer graphicsCommandBuffer
) const -> void {
    graphicsCommandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe,
        {}, {}, {},
        {
            vk::ImageMemoryBarrier {
                {}, {},
                {}, vk::ImageLayout::eColorAttachmentOptimal,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                sceneOpaqueAttachmentGroup.getSwapchainAttachment(0).multisampleImage, vku::fullSubresourceRange(),
            },
            vk::ImageMemoryBarrier {
                {}, {},
                {}, vk::ImageLayout::eDepthAttachmentOptimal,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                sceneOpaqueAttachmentGroup.depthStencilAttachment->image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth),
            },
            vk::ImageMemoryBarrier {
                {}, {},
                {}, vk::ImageLayout::eColorAttachmentOptimal,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                sceneWeightedBlendedAttachmentGroup.getColorAttachment(0).multisampleImage, vku::fullSubresourceRange(),
            },
            vk::ImageMemoryBarrier {
                {}, {},
                {}, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                sceneWeightedBlendedAttachmentGroup.getColorAttachment(0).image, vku::fullSubresourceRange(),
            },
            vk::ImageMemoryBarrier {
                {}, {},
                {}, vk::ImageLayout::eColorAttachmentOptimal,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                sceneWeightedBlendedAttachmentGroup.getColorAttachment(1).multisampleImage, vku::fullSubresourceRange(),
            },
            vk::ImageMemoryBarrier {
                {}, {},
                {}, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                sceneWeightedBlendedAttachmentGroup.getColorAttachment(1).image, vku::fullSubresourceRange(),
            },
        });
}