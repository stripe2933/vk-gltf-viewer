module;

#include <boost/container_hash/hash.hpp>
#include <boost/container/static_vector.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer.vulkan.Frame;

import imgui.vulkan;

import vk_gltf_viewer.helpers.concepts;
import vk_gltf_viewer.helpers.fastgltf;
import vk_gltf_viewer.helpers.functional;
import vk_gltf_viewer.helpers.Lazy;
import vk_gltf_viewer.helpers.optional;
import vk_gltf_viewer.helpers.ranges;
import vk_gltf_viewer.math.extended_arithmetic;

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

vk_gltf_viewer::vulkan::Frame::GltfAsset::GltfAsset(const SharedData &sharedData)
    : nodeBuffer {
        sharedData.assetExtended->asset,
        sharedData.assetExtended->nodeWorldTransforms,
        sharedData.gpu.device,
        sharedData.gpu.allocator,
        vkgltf::NodeBuffer::Config {
            .adapter = sharedData.assetExtended->externalBuffers,
            .skinBuffer = value_address(sharedData.assetExtended->skinBuffer),
        },
    },
    mousePickingResultBuffer {
        sharedData.gpu.allocator,
        vk::BufferCreateInfo {
            {},
            sizeof(std::uint32_t) * math::divCeil<std::uint32_t>(sharedData.assetExtended->asset.nodes.size(), 32U),
            vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
        },
        vma::AllocationCreateInfo {
            vma::AllocationCreateFlagBits::eHostAccessRandom | vma::AllocationCreateFlagBits::eMapped,
            vma::MemoryUsage::eAutoPreferDevice,
        },
    },
    descriptorPool { value_if(!sharedData.gpu.supportVariableDescriptorCount, [&]() {
        return vk::raii::DescriptorPool {
            sharedData.gpu.device,
            sharedData.assetDescriptorSetLayout.getPoolSize()
                .getDescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind),
        };
    }) } { }

vk_gltf_viewer::vulkan::Frame::Frame(std::shared_ptr<const Renderer> _renderer, const SharedData &sharedData)
    : sharedData { sharedData }
    , renderer { std::move(_renderer) }
    , descriptorPool { createDescriptorPool() }
    , computeCommandPool { sharedData.gpu.device, vk::CommandPoolCreateInfo { {}, sharedData.gpu.queueFamilies.compute } }
    , graphicsCommandPool { sharedData.gpu.device, vk::CommandPoolCreateInfo { {}, sharedData.gpu.queueFamilies.graphicsPresent } }
    , scenePrepassFinishSema { sharedData.gpu.device, vk::SemaphoreCreateInfo{} }
    , sceneRenderingFinishSema { sharedData.gpu.device, vk::SemaphoreCreateInfo{} }
    , jumpFloodFinishSema { sharedData.gpu.device, vk::SemaphoreCreateInfo{} }
    , swapchainImageAcquireSema { sharedData.gpu.device, vk::SemaphoreCreateInfo{} }
    , inFlightFence { sharedData.gpu.device, vk::FenceCreateInfo{} } {
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

vk_gltf_viewer::vulkan::Frame::ExecutionResult vk_gltf_viewer::vulkan::Frame::getExecutionResult() {
    std::ignore = sharedData.gpu.device.waitForFences(*inFlightFence, true, ~0ULL);
    ExecutionResult result{};

    if (gltfAsset) {
        // Retrieve the mouse picking result from the buffer.
        visit(multilambda {
            [&](const vk::Rect2D&) {
                const std::span packedBits = gltfAsset->mousePickingResultBuffer.asRange<const std::uint32_t>();
                std::vector<std::size_t> &indices = result.mousePickingResult.emplace<std::vector<std::size_t>>();

                std::size_t nodeIndex = 0;
                for (std::uint32_t accessBlock : packedBits) {
                    for (std::uint32_t bitmask = 1; bitmask != 0; bitmask <<= 1) {
                        if (accessBlock & bitmask) {
                            indices.push_back(nodeIndex);
                        }
                        ++nodeIndex;
                    }
                }
            },
            [&](const vk::Offset2D&) {
                const std::uint16_t hoveringNodeIndex = gltfAsset->mousePickingResultBuffer.asValue<const std::uint32_t>();
                if (hoveringNodeIndex != NO_INDEX) {
                    result.mousePickingResult.emplace<std::size_t>(hoveringNodeIndex);
                }
            },
            [](std::monostate) { }
        }, gltfAsset->mousePickingInput);
    }

    return result;
}

void vk_gltf_viewer::vulkan::Frame::update(const ExecutionTask &task) {
    // If passthru extent is different from the current's, dependent images have to be recreated.
    if (!passthruResources || passthruResources->extent != task.passthruRect.extent) {
        // TODO: can this operation be non-blocking?
        const vk::raii::Fence fence { sharedData.gpu.device, vk::FenceCreateInfo{} };
        vku::executeSingleCommand(*sharedData.gpu.device, *graphicsCommandPool, sharedData.gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
            passthruResources.emplace(sharedData, task.passthruRect.extent, cb);
        }, *fence);

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

        std::ignore = sharedData.gpu.device.waitForFences(*fence, true, ~0ULL);
    }

    projectionViewMatrix = renderer->camera.getProjectionViewMatrix();
    translationlessProjectionViewMatrix = renderer->camera.getProjectionMatrix() * glm::mat4 { glm::mat3 { renderer->camera.getViewMatrix() } };
    passthruOffset = task.passthruRect.offset;

    // Should be calculated on-demand (only if pipeline recreation is requested).
    Lazy<AssetSpecialization> assetSpecialization { [&]() { return AssetSpecialization { sharedData.assetExtended->asset }; } };

    const auto criteriaGetter = [&](const fastgltf::Primitive &primitive) {
        const bool usePerFragmentEmissiveStencilExport = renderer->bloom.raw().mode == Renderer::Bloom::PerFragment;
        CommandSeparationCriteria result {
            .subpass = 0U,
            .indexType = value_if(primitive.type == fastgltf::PrimitiveType::LineLoop || primitive.indicesAccessor.has_value(), [&]() {
                return sharedData.assetExtended->combinedIndexBuffer.getIndexTypeAndFirstIndex(primitive).first;
            }),
            .primitiveTopology = getPrimitiveTopology(primitive.type),
            // By default, the default primitive doesn't have a material and therefore isn't unlit.
            // If per-fragment stencil export is disabled, dynamic stencil reference state has to be used, and its
            // reference value is 0.
            .stencilReference = value_if(!usePerFragmentEmissiveStencilExport, 0U),
            .cullMode = vk::CullModeFlagBits::eBack,
        };

        // glTF 2.0 specification:
        //   Points or Lines with no NORMAL attribute SHOULD be rendered without lighting and instead use the sum of the
        //   base color value (as defined above, multiplied by COLOR_0 when present) and the emissive value.
        const bool isPrimitivePointsOrLineWithoutNormal
            = ranges::one_of(primitive.type, { fastgltf::PrimitiveType::Points, fastgltf::PrimitiveType::Lines, fastgltf::PrimitiveType::LineLoop, fastgltf::PrimitiveType::LineStrip })
            && (primitive.findAttribute("NORMAL") == primitive.attributes.end());

        if (primitive.materialIndex) {
            const fastgltf::Material &material = sharedData.assetExtended->asset.materials[*primitive.materialIndex];
            result.subpass = material.alphaMode == fastgltf::AlphaMode::Blend;

            if (material.unlit || isPrimitivePointsOrLineWithoutNormal) {
                result.pipeline = sharedData.getUnlitPrimitiveRenderer(assetSpecialization.get(), primitive);
                // Disable stencil reference dynamic state when using unlit rendering pipeline.
                result.stencilReference.reset();
            }
            else {
                result.pipeline = sharedData.getPrimitiveRenderer(assetSpecialization.get(), primitive, usePerFragmentEmissiveStencilExport);
                if (!usePerFragmentEmissiveStencilExport) {
                    result.stencilReference.emplace(material.emissiveStrength > 1.f ? 1U : 0U);
                }
            }
            result.cullMode = material.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack;
        }
        else if (isPrimitivePointsOrLineWithoutNormal) {
            result.pipeline = sharedData.getUnlitPrimitiveRenderer(assetSpecialization.get(), primitive);
            // Disable stencil reference dynamic state when using unlit rendering pipeline.
            result.stencilReference.reset();
        }
        else {
            result.pipeline = sharedData.getPrimitiveRenderer(assetSpecialization.get(), primitive, usePerFragmentEmissiveStencilExport);
        }
        return result;
    };

    const auto mousePickingCriteriaGetter = [&](const fastgltf::Primitive &primitive) {
        CommandSeparationCriteriaNoShading result{
            .indexType = value_if(primitive.type == fastgltf::PrimitiveType::LineLoop || primitive.indicesAccessor.has_value(), [&]() {
                return sharedData.assetExtended->combinedIndexBuffer.getIndexTypeAndFirstIndex(primitive).first;
            }),
            .primitiveTopology = getPrimitiveTopology(primitive.type),
            .cullMode = vk::CullModeFlagBits::eBack,
        };

        if (primitive.materialIndex) {
            const fastgltf::Material& material = sharedData.assetExtended->asset.materials[*primitive.materialIndex];
            if (material.alphaMode == fastgltf::AlphaMode::Mask) {
                result.pipeline = sharedData.getMaskNodeIndexRenderer(assetSpecialization.get(), primitive);
            }
            else {
                result.pipeline = sharedData.getNodeIndexRenderer(primitive);
            }
            result.cullMode = material.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack;
        }
        else {
            result.pipeline = sharedData.getNodeIndexRenderer(primitive);
        }
        return result;
    };

    const auto multiNodeMousePickingCriteriaGetter = [&](const fastgltf::Primitive &primitive) {
        CommandSeparationCriteriaNoShading result{
            .indexType = value_if(primitive.type == fastgltf::PrimitiveType::LineLoop || primitive.indicesAccessor.has_value(), [&]() {
                return sharedData.assetExtended->combinedIndexBuffer.getIndexTypeAndFirstIndex(primitive).first;
            }),
            .primitiveTopology = getPrimitiveTopology(primitive.type),
            .cullMode = vk::CullModeFlagBits::eNone,
        };

        if (primitive.materialIndex) {
            const fastgltf::Material& material = sharedData.assetExtended->asset.materials[*primitive.materialIndex];
            if (material.alphaMode == fastgltf::AlphaMode::Mask) {
                result.pipeline = sharedData.getMaskMultiNodeMousePickingRenderer(assetSpecialization.get(), primitive);
            }
            else {
                result.pipeline = sharedData.getMultiNodeMousePickingRenderer(primitive);
            }
        }
        else {
            result.pipeline = sharedData.getMultiNodeMousePickingRenderer(primitive);
        }
        return result;
    };

    const auto jumpFloodSeedCriteriaGetter = [&](const fastgltf::Primitive &primitive) {
        CommandSeparationCriteriaNoShading result {
            .indexType = value_if(primitive.type == fastgltf::PrimitiveType::LineLoop || primitive.indicesAccessor.has_value(), [&]() {
                return sharedData.assetExtended->combinedIndexBuffer.getIndexTypeAndFirstIndex(primitive).first;
            }),
            .primitiveTopology = getPrimitiveTopology(primitive.type),
            .cullMode = vk::CullModeFlagBits::eBack,
        };

        if (primitive.materialIndex) {
            const fastgltf::Material &material = sharedData.assetExtended->asset.materials[*primitive.materialIndex];
            if (material.alphaMode == fastgltf::AlphaMode::Mask) {
                result.pipeline = sharedData.getMaskJumpFloodSeedRenderer(assetSpecialization.get(), primitive);
            }
            else {
                result.pipeline = sharedData.getJumpFloodSeedRenderer(primitive);
            }
            result.cullMode = material.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack;
        }
        else {
            result.pipeline = sharedData.getJumpFloodSeedRenderer(primitive);
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
        std::uint32_t drawCount = sharedData.assetExtended->asset.accessors[drawCountDeterminingAccessorIndex].count;

        // Since GL_LINE_LOOP primitive is emulated as LINE_STRIP draw, additional 1 index is used.
        if (primitive.type == fastgltf::PrimitiveType::LineLoop) {
            ++drawCount;
        }

        // EXT_mesh_gpu_instancing support.
        std::uint32_t instanceCount = 1;
        if (const fastgltf::Node &node = sharedData.assetExtended->asset.nodes[nodeIndex]; !node.instancingAttributes.empty()) {
            instanceCount = sharedData.assetExtended->asset.accessors[node.instancingAttributes[0].accessorIndex].count;
        }

        const std::size_t primitiveIndex = sharedData.assetExtended->primitiveBuffer.getPrimitiveIndex(primitive);

        // To embed the node and primitive indices into 32-bit unsigned integer, both must be in range of 16-bit unsigned integer.
        if (!std::in_range<std::uint16_t>(nodeIndex) || !std::in_range<std::uint16_t>(primitiveIndex)) {
            throw std::runtime_error { "Requirement violation: nodeIndex <= 65535 && primitiveIndex <= 65535" };
        }

        const std::uint32_t firstInstance = (static_cast<std::uint32_t>(nodeIndex) << 16U) | static_cast<std::uint32_t>(primitiveIndex);
        if (primitive.type == fastgltf::PrimitiveType::LineLoop || primitive.indicesAccessor) {
            const std::uint32_t firstIndex = sharedData.assetExtended->combinedIndexBuffer.getIndexTypeAndFirstIndex(primitive).second;
            return vk::DrawIndexedIndirectCommand { drawCount, instanceCount, firstIndex, 0, firstInstance };
        }
        else {
            return vk::DrawIndirectCommand { drawCount, instanceCount, 0, firstInstance };
        }
    };

    if (task.gltf) {
        const auto isPrimitiveWithinFrustum = [&](std::size_t nodeIndex, std::size_t primitiveIndex, const math::Frustum &frustum) -> bool {
            const fastgltf::Node &node = sharedData.assetExtended->asset.nodes[nodeIndex];
            const auto [min, max] = getBoundingBoxMinMax(sharedData.assetExtended->primitiveBuffer.getPrimitive(primitiveIndex), node, sharedData.assetExtended->asset);

            const auto pred = [&](const fastgltf::math::fmat4x4 &worldTransform) -> bool {
                const fastgltf::math::fvec3 transformedMin { worldTransform * fastgltf::math::fvec4 { min.x(), min.y(), min.z(), 1.f } };
                const fastgltf::math::fvec3 transformedMax { worldTransform * fastgltf::math::fvec4 { max.x(), max.y(), max.z(), 1.f } };

                const fastgltf::math::fvec3 halfDisplacement = (transformedMax - transformedMin) / 2.f;
                const fastgltf::math::fvec3 center = transformedMin + halfDisplacement;
                const float radius = length(halfDisplacement);

                return frustum.isOverlapApprox(glm::make_vec3(center.data()), radius);
            };

            if (node.instancingAttributes.empty()) {
                return pred(sharedData.assetExtended->nodeWorldTransforms[nodeIndex]);
            }
            else {
                // If node is instanced, the node primitive is regarded to be within the frustum if any of its instance
                // is within the frustum.
                return std::ranges::any_of(gltfAsset->nodeBuffer.getInstanceWorldTransforms(nodeIndex), pred);
            }
        };

        std::unordered_map<std::uint32_t /* firstInstance */, std::uint32_t /* instanceCount */> cachedInstanceCounts;
        const auto commandBufferCullingFunc = [&](buffer::IndirectDrawCommands &indirectDrawCommands, const math::Frustum &frustum) -> bool {
            // Partition the commands based on whether the bounding sphere of the primitive is within the frustum.
            // - If the bounding sphere is overlapping with the frustum, partitioned left.
            // - Otherwise, partitioned right.
            // Then, draw count is set to the size of the left partition.
            const std::uint32_t drawCount = visit([&]<concepts::one_of<vk::DrawIndirectCommand, vk::DrawIndexedIndirectCommand> T>(std::span<T> commands) -> std::size_t {
                return std::distance(
                    commands.begin(),
                    std::ranges::partition(commands, [&](T &command) {
                        const std::size_t nodeIndex = command.firstInstance >> 16U;
                        const fastgltf::Node &node = sharedData.assetExtended->asset.nodes[nodeIndex];

                        // Node is instanced and frustum culling is disabled for instanced nodes.
                        if (!node.instancingAttributes.empty() && renderer->frustumCullingMode != Renderer::FrustumCullingMode::OnWithInstancing) {
                            return true;
                        }

                        if (node.skinIndex) {
                            // As primitive POSITION accessor's min/max values are not sufficient to determine the bounding
                            // volume of a skinned mesh, frustum culling which relies on this must be disabled.
                            return true;
                        }

                        // First find the pre-calculated instance count.
                        if (auto it = cachedInstanceCounts.find(command.firstInstance); it == cachedInstanceCounts.end()) {
                            // No pre-calculated instance count, calculate and store it.
                            const std::size_t primitiveIndex = command.firstInstance & 0xFFFFU;
                            if (node.instancingAttributes.empty()) {
                                command.instanceCount = isPrimitiveWithinFrustum(nodeIndex, primitiveIndex, frustum);
                            }
                            else {
                                command.instanceCount = isPrimitiveWithinFrustum(nodeIndex, primitiveIndex, frustum)
                                    ? sharedData.assetExtended->asset.accessors[node.instancingAttributes.front().accessorIndex].count : 0U;
                            }
                            cachedInstanceCounts.emplace_hint(it, command.firstInstance, command.instanceCount);
                        }
                        else {
                            command.instanceCount = it->second;
                        }

                        return command.instanceCount > 0U;
                    }).begin());
            }, indirectDrawCommands.drawIndirectCommands());
            indirectDrawCommands.setDrawCount(drawCount);
            return drawCount > 0U;
        };

        if (!renderingNodes || task.gltf->regenerateDrawCommands) {
            std::vector<std::size_t> visibleNodeIndices;
            for (const auto &[nodeIndex, visible] : sharedData.assetExtended->sceneNodeVisibilities.getVisibilities() | ranges::views::enumerate) {
                if (visible) {
                    visibleNodeIndices.push_back(nodeIndex);
                }
            }

            renderingNodes.emplace(
                buffer::createIndirectDrawCommandBuffers(sharedData.assetExtended->asset, sharedData.gpu.allocator, criteriaGetter, visibleNodeIndices, drawCommandGetter),
                buffer::createIndirectDrawCommandBuffers(sharedData.assetExtended->asset, sharedData.gpu.allocator, mousePickingCriteriaGetter, visibleNodeIndices, drawCommandGetter),
                buffer::createIndirectDrawCommandBuffers(sharedData.assetExtended->asset, sharedData.gpu.allocator, multiNodeMousePickingCriteriaGetter, visibleNodeIndices, drawCommandGetter));
        }

        const math::Frustum frustum = renderer->camera.getFrustum();

        if (renderer->frustumCullingMode != Renderer::FrustumCullingMode::Off) {
            for (buffer::IndirectDrawCommands &buffer : renderingNodes->indirectDrawCommandBuffers | std::views::values) {
                commandBufferCullingFunc(buffer, frustum);
            }

            // Do frustum culling and do mouse picking only if there's any mesh primitive inside the frustum.
            renderingNodes->startMousePickingRenderPass = false;
            visit(multilambda {
                [](std::monostate) noexcept { },
                [&](const vk::Offset2D &offset) {
                    // TODO: use ray-sphere intersection test instead of frustum overlap test.
                    const float xmin = static_cast<float>(offset.x) / task.passthruRect.extent.width;
                    const float xmax = static_cast<float>(offset.x + 1) / task.passthruRect.extent.width;
                    const float ymin = 1.f - static_cast<float>(offset.y + 1) / task.passthruRect.extent.height;
                    const float ymax = 1.f - static_cast<float>(offset.y) / task.passthruRect.extent.height;
                    const math::Frustum frustum = renderer->camera.getFrustum(xmin, xmax, ymin, ymax);

                    for (buffer::IndirectDrawCommands &buffer : renderingNodes->mousePickingIndirectDrawCommandBuffers | std::views::values) {
                        renderingNodes->startMousePickingRenderPass |= commandBufferCullingFunc(buffer, frustum);
                    }
                },
                [&](const vk::Rect2D &rect) {
                    const float xmin = static_cast<float>(rect.offset.x) / task.passthruRect.extent.width;
                    const float xmax = static_cast<float>(rect.offset.x + rect.extent.width) / task.passthruRect.extent.width;
                    const float ymin = 1.f - static_cast<float>(rect.offset.y + rect.extent.height) / task.passthruRect.extent.height;
                    const float ymax = 1.f - static_cast<float>(rect.offset.y) / task.passthruRect.extent.height;
                    const math::Frustum frustum = renderer->camera.getFrustum(xmin, xmax, ymin, ymax);

                    renderingNodes->startMousePickingRenderPass = false;
                    for (buffer::IndirectDrawCommands &buffer : renderingNodes->multiNodeMousePickingIndirectDrawCommandBuffers | std::views::values) {
                        renderingNodes->startMousePickingRenderPass |= commandBufferCullingFunc(buffer, frustum);
                    }
                },
            }, task.gltf->mousePickingInput);
        }
        else {
            for (buffer::IndirectDrawCommands &buffer : renderingNodes->indirectDrawCommandBuffers | std::views::values) {
                buffer.resetDrawCount();
            }
            visit(multilambda {
                [](std::monostate) noexcept { },
                [this](const vk::Offset2D&) {
                    for (buffer::IndirectDrawCommands &buffer : renderingNodes->mousePickingIndirectDrawCommandBuffers | std::views::values) {
                        buffer.resetDrawCount();
                    }
                },
                [this](const vk::Rect2D&) {
                    for (buffer::IndirectDrawCommands &buffer : renderingNodes->multiNodeMousePickingIndirectDrawCommandBuffers | std::views::values) {
                        buffer.resetDrawCount();
                    }
                },
            }, task.gltf->mousePickingInput);
        }

        if (!sharedData.assetExtended->selectedNodes.empty() && renderer->selectedNodeOutline) {
            const auto getSelectionHash = [&] {
                return boost::hash_unordered_range(sharedData.assetExtended->selectedNodes.begin(), sharedData.assetExtended->selectedNodes.end());
            };

            std::size_t indexHash;
            if (!selectedNodes /* asset has selected nodes but frame doesn't */ ||
                task.gltf->regenerateDrawCommands /* draw call regeneration explicitly requested */ ||
                (indexHash = getSelectionHash()) != selectedNodes->indexHash /* asset node selection has been changed */ ) {
                selectedNodes.emplace(
                    indexHash,
                    buffer::createIndirectDrawCommandBuffers(sharedData.assetExtended->asset, sharedData.gpu.allocator, jumpFloodSeedCriteriaGetter, sharedData.assetExtended->selectedNodes, drawCommandGetter));
            }

            if (renderer->frustumCullingMode != Renderer::FrustumCullingMode::Off) {
                for (buffer::IndirectDrawCommands &buffer : selectedNodes->jumpFloodSeedIndirectDrawCommandBuffers | std::views::values) {
                    commandBufferCullingFunc(buffer, frustum);
                }
            }
            else {
                for (auto &buffer : selectedNodes->jumpFloodSeedIndirectDrawCommandBuffers | std::views::values) {
                    buffer.resetDrawCount();
                }
            }
        }
        else {
            selectedNodes.reset();
        }

        const auto isHoveringNodeAndSelectedNodeEqual = [&] {
            const auto &selectedNodes = sharedData.assetExtended->selectedNodes;
            return selectedNodes.size() == 1 && *sharedData.assetExtended->hoveringNode == *selectedNodes.begin();
        };

        if (sharedData.assetExtended->hoveringNode &&
            renderer->hoveringNodeOutline &&
            !isHoveringNodeAndSelectedNodeEqual() /* in this case, hovering node outline doesn't have to be drawn */) {
            if (!hoveringNode /* asset has hovering node but frame doesn't */ ||
                task.gltf->regenerateDrawCommands /* draw call regeneration explicitly requested */ ||
                *sharedData.assetExtended->hoveringNode != hoveringNode->index /* asset hovering node has been changed */) {
                hoveringNode.emplace(
                    *sharedData.assetExtended->hoveringNode,
                    buffer::createIndirectDrawCommandBuffers(sharedData.assetExtended->asset, sharedData.gpu.allocator, jumpFloodSeedCriteriaGetter, std::views::single(*sharedData.assetExtended->hoveringNode), drawCommandGetter));
            }

            if (renderer->frustumCullingMode != Renderer::FrustumCullingMode::Off) {
                for (buffer::IndirectDrawCommands &buffer : hoveringNode->jumpFloodSeedIndirectDrawCommandBuffers | std::views::values) {
                    commandBufferCullingFunc(buffer, frustum);
                }
            }
            else {
                for (buffer::IndirectDrawCommands &buffer : hoveringNode->jumpFloodSeedIndirectDrawCommandBuffers | std::views::values) {
                    buffer.resetDrawCount();
                }
            }
        }
        else {
            hoveringNode.reset();
        }

        gltfAsset->mousePickingInput = task.gltf->mousePickingInput;
    }
    else {
        renderingNodes.reset();
        selectedNodes.reset();
        hoveringNode.reset();
    }
}

void vk_gltf_viewer::vulkan::Frame::recordCommandsAndSubmit(Swapchain &swapchain) const {
    // Acquire the next swapchain image.
    std::uint32_t swapchainImageIndex;
    try {
        vk::Result result [[maybe_unused]];
        std::tie(result, swapchainImageIndex) = (*sharedData.gpu.device).acquireNextImageKHR(
            *swapchain.swapchain, ~0ULL, *swapchainImageAcquireSema);

    #if __APPLE__
        // MoltenVK does not allow presenting suboptimal swapchain image.
        // Issue tracked: https://github.com/KhronosGroup/MoltenVK/issues/2542
        if (result == vk::Result::eSuboptimalKHR) {
            return;
        }
    #endif
    }
    catch (const vk::OutOfDateKHRError&) {
        return;
    }

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
                std::bit_ceil(static_cast<std::uint32_t>(renderer->hoveringNodeOutline->thickness)));
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
                std::bit_ceil(static_cast<std::uint32_t>(renderer->selectedNodeOutline->thickness)));
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
        if (renderer->solidBackground) {
            backgroundColor.setFloat32({ renderer->solidBackground->x, renderer->solidBackground->y, renderer->solidBackground->z, 1.f });
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
        if (!renderer->solidBackground) {
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
        if (renderer->bloom) {
            sceneRenderingCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *sharedData.inverseToneMappingRenderer.pipeline);
            sceneRenderingCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.inverseToneMappingRenderer.pipelineLayout, 0, inverseToneMappingSet, {});
            sceneRenderingCommandBuffer.draw(3, 1, 0, 0);
        }

        sceneRenderingCommandBuffer.endRenderPass();

        if (renderer->bloom) {
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
                0, BloomApplyRenderer::PushConstant { .factor = renderer->bloom->intensity });
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
                    swapchain.images[swapchainImageIndex], vku::fullSubresourceRange(),
                },
            }));

        // Copy from composited image to swapchain image.
        compositionCommandBuffer.copyImage(
            passthruResources->sceneOpaqueAttachmentGroup.getColorAttachment(0).image, vk::ImageLayout::eTransferSrcOptimal,
            swapchain.images[swapchainImageIndex], vk::ImageLayout::eTransferDstOptimal,
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
                swapchain.images[swapchainImageIndex], vku::fullSubresourceRange(),
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
                swapchain.images[swapchainImageIndex], vku::fullSubresourceRange(),
            });

        compositionCommandBuffer.end();
    }

    sharedData.gpu.device.resetFences(*inFlightFence);
    sharedData.gpu.queues.graphicsPresent.submit({
        vk::SubmitInfo {
            {},
            {},
            sceneRenderingCommandBuffer,
            *sceneRenderingFinishSema,
        },
        vk::SubmitInfo {
            vku::unsafeProxy({ *swapchainImageAcquireSema, *sceneRenderingFinishSema, *jumpFloodFinishSema }),
            vku::unsafeProxy<vk::PipelineStageFlags>({
                vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eColorAttachmentOutput,
                vk::PipelineStageFlagBits::eFragmentShader,
            }),
            compositionCommandBuffer,
            *swapchain.imageReadySemaphores[swapchainImageIndex],
        },
    }, *inFlightFence);

    // Present the rendered swapchain image to swapchain.
    try {
        std::ignore = sharedData.gpu.queues.graphicsPresent.presentKHR({
            *swapchain.imageReadySemaphores[swapchainImageIndex],
            *swapchain.swapchain,
            swapchainImageIndex,
        });
    }
    catch (const vk::OutOfDateKHRError&) { }
}

void vk_gltf_viewer::vulkan::Frame::updateAsset() {
    const auto &inner = gltfAsset.emplace(sharedData);
    if (sharedData.gpu.supportVariableDescriptorCount) {
        (*sharedData.gpu.device).freeDescriptorSets(*descriptorPool, assetDescriptorSet);
        assetDescriptorSet = decltype(assetDescriptorSet) {
            vku::unsafe,
            (*sharedData.gpu.device).allocateDescriptorSets(vk::StructureChain {
                 vk::DescriptorSetAllocateInfo {
                     *descriptorPool,
                     *sharedData.assetDescriptorSetLayout,
                 },
                 vk::DescriptorSetVariableDescriptorCountAllocateInfo {
                     vku::unsafeProxy<std::uint32_t>(sharedData.assetExtended->asset.textures.size() + 1),
                 },
             }.get())[0],
        };
    }
    else {
        std::tie(assetDescriptorSet) = vku::allocateDescriptorSets(*inner.descriptorPool.value(), std::tie(sharedData.assetDescriptorSetLayout));
    }

    std::vector<vk::DescriptorImageInfo> imageInfos;
    imageInfos.reserve(sharedData.assetExtended->asset.textures.size() + 1);
    imageInfos.emplace_back(*sharedData.fallbackTexture.sampler, *sharedData.fallbackTexture.imageView, vk::ImageLayout::eShaderReadOnlyOptimal);
    imageInfos.append_range(sharedData.assetExtended->textures.descriptorInfos);

    sharedData.gpu.device.updateDescriptorSets({
        mousePickingSet.getWriteOne<1>({ inner.mousePickingResultBuffer, 0, sizeof(std::uint32_t) }),
        multiNodeMousePickingSet.getWriteOne<0>({ inner.mousePickingResultBuffer, 0, vk::WholeSize }),
        assetDescriptorSet.getWrite<0>(sharedData.assetExtended->primitiveBuffer.descriptorInfo),
        assetDescriptorSet.getWrite<1>(inner.nodeBuffer.descriptorInfo),
        assetDescriptorSet.getWrite<2>(sharedData.assetExtended->materialBuffer.descriptorInfo),
        assetDescriptorSet.getWrite<3>(imageInfos),
    }, {});
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
    const auto getJumpFloodSeedImageMemoryBarrier = [](vk::Image image) -> vk::ImageMemoryBarrier {
        return {
            {}, vk::AccessFlagBits::eColorAttachmentWrite,
            {}, vk::ImageLayout::eColorAttachmentOptimal,
            vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
            image, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } /* ping image */,
        };
    };
    if (selectedNodes) {
        memoryBarriers.push_back(getJumpFloodSeedImageMemoryBarrier(passthruResources->selectedNodeOutlineJumpFloodResources.image));
    }
    // Same holds for hovering nodes' outline.
    if (hoveringNode) {
        memoryBarriers.push_back(getJumpFloodSeedImageMemoryBarrier(passthruResources->hoveringNodeOutlineJumpFloodResources.image));
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
                resourceBindingState.indexType.emplace(*criteria.indexType);
                cb.bindIndexBuffer(
                    sharedData.assetExtended->combinedIndexBuffer,
                    sharedData.assetExtended->combinedIndexBuffer.getIndexOffsetAndSize(*resourceBindingState.indexType).first,
                    *resourceBindingState.indexType);
            }
            indirectDrawCommandBuffer.recordDrawCommand(cb, sharedData.gpu.supportDrawIndirectCount);
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

            if (renderingNodes->startMousePickingRenderPass) {
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
            }
            else {
                return false;
            }
        },
        [&](const vk::Rect2D &rect) {
            // Clear mousePickingResultBuffer as zeros.
            cb.fillBuffer(gltfAsset->mousePickingResultBuffer, 0, gltfAsset->mousePickingResultBuffer.size, 0U);

            if (rect.extent.width == 0 || rect.extent.height == 0) {
                // Do nothing.
                return false;
            }

            if (renderingNodes->startMousePickingRenderPass) {
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
                            resourceBindingState.indexType.emplace(*criteria.indexType);
                            cb.bindIndexBuffer(
                                sharedData.assetExtended->combinedIndexBuffer,
                                sharedData.assetExtended->combinedIndexBuffer.getIndexOffsetAndSize(*resourceBindingState.indexType).first,
                                *resourceBindingState.indexType);
                        }
                        indirectDrawCommandBuffer.recordDrawCommand(cb, sharedData.gpu.supportDrawIndirectCount);
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
            }
            else {
                return false;
            }
        },
        [](std::monostate) {
            return false;
        }
    }, gltfAsset->mousePickingInput);

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
            sharedData.primitivePipelineLayout.pushConstants(cb, { projectionViewMatrix, renderer->camera.position });
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
            resourceBindingState.indexType.emplace(*criteria.indexType);
            cb.bindIndexBuffer(
                sharedData.assetExtended->combinedIndexBuffer,
                sharedData.assetExtended->combinedIndexBuffer.getIndexOffsetAndSize(*resourceBindingState.indexType).first,
                *resourceBindingState.indexType);
        }
        indirectDrawCommandBuffer.recordDrawCommand(cb, sharedData.gpu.supportDrawIndirectCount);
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
            sharedData.primitivePipelineLayout.pushConstants(cb, { projectionViewMatrix, renderer->camera.position });
            resourceBindingState.pushConstantBound = true;
        }

        if (criteria.indexType && resourceBindingState.indexType != *criteria.indexType) {
            resourceBindingState.indexType.emplace(*criteria.indexType);
            cb.bindIndexBuffer(
                sharedData.assetExtended->combinedIndexBuffer,
                sharedData.assetExtended->combinedIndexBuffer.getIndexOffsetAndSize(*resourceBindingState.indexType).first,
                *resourceBindingState.indexType);
        }
        indirectDrawCommandBuffer.recordDrawCommand(cb, sharedData.gpu.supportDrawIndirectCount);
        hasBlendMesh = true;
    }

    return hasBlendMesh;
}

void vk_gltf_viewer::vulkan::Frame::recordSkyboxDrawCommands(vk::CommandBuffer cb) const {
    sharedData.skyboxRenderer.draw(cb, sharedData.skyboxDescriptorSet, { translationlessProjectionViewMatrix });
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
                .outlineColor = renderer->selectedNodeOutline->color,
                .outlineThickness = renderer->selectedNodeOutline->thickness,
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
                .outlineColor = renderer->hoveringNodeOutline->color,
                .outlineThickness = renderer->hoveringNodeOutline->thickness,
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