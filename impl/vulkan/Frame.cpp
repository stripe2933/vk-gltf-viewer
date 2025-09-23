module;

#include <boost/container_hash/hash.hpp>
#include <boost/container/small_vector.hpp>
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
import vk_gltf_viewer.math.bit;
import vk_gltf_viewer.math.extended_arithmetic;

#define INDEX_SEQ(Is, N, ...) [&]<auto ...Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

constexpr auto NO_INDEX = std::numeric_limits<std::uint16_t>::max();
constexpr auto emulatedPrimitiveTopologies = {
    fastgltf::PrimitiveType::LineLoop, // -> LineStrip
#if __APPLE__
    fastgltf::PrimitiveType::TriangleFan, // Triangles
#endif
};

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
    #if __APPLE__
        return vk::PrimitiveTopology::eTriangleList;
    #else
        return vk::PrimitiveTopology::eTriangleFan;
    #endif
    }
    std::unreachable();
}

vk_gltf_viewer::vulkan::Frame::GltfAsset::GltfAsset(const SharedData &sharedData)
    : assetExtended { sharedData.assetExtended }
    , nodeBuffer {
        assetExtended->asset,
        assetExtended->nodeWorldTransforms,
        sharedData.gpu.device,
        sharedData.gpu.allocator,
        vkgltf::NodeBuffer::Config {
            .adapter = assetExtended->externalBuffers,
            .skinBuffer = value_address(assetExtended->skinBuffer),
        },
    },
    mousePickingResultBuffer {
        sharedData.gpu.allocator,
        vk::BufferCreateInfo {
            {},
            std::max(sizeof(std::uint64_t), sizeof(std::uint32_t) * math::divCeil<std::uint32_t>(assetExtended->asset.nodes.size(), 32U)),
            vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
        },
        vma::AllocationCreateInfo {
            vma::AllocationCreateFlagBits::eHostAccessRandom | vma::AllocationCreateFlagBits::eMapped,
            vma::MemoryUsage::eAutoPreferDevice,
        },
    } { }

void vk_gltf_viewer::vulkan::Frame::GltfAsset::updateNodeWorldTransform(std::size_t nodeIndex) {
    nodeBuffer.update(nodeIndex, assetExtended->nodeWorldTransforms[nodeIndex], assetExtended->externalBuffers);
}

void vk_gltf_viewer::vulkan::Frame::GltfAsset::updateNodeWorldTransformHierarchical(std::size_t nodeIndex) {
    nodeBuffer.updateHierarchical(nodeIndex, assetExtended->nodeWorldTransforms, assetExtended->externalBuffers);
}

void vk_gltf_viewer::vulkan::Frame::GltfAsset::updateNodeWorldTransformScene(std::size_t sceneIndex) {
    nodeBuffer.update(assetExtended->asset.scenes[sceneIndex], assetExtended->nodeWorldTransforms, assetExtended->externalBuffers);
}

void vk_gltf_viewer::vulkan::Frame::GltfAsset::updateNodeTargetWeights(std::size_t nodeIndex, std::size_t startIndex, std::size_t count) {
    std::ranges::copy(
        getTargetWeights(assetExtended->asset.nodes[nodeIndex], assetExtended->asset).subspan(startIndex, count),
        nodeBuffer.getMorphTargetWeights(nodeIndex).subspan(startIndex, count).begin());
}

vk_gltf_viewer::vulkan::Frame::Frame(std::shared_ptr<const Renderer> _renderer, const SharedData &sharedData)
    : sharedData { sharedData }
    , renderer { std::move(_renderer) }
    , cameraBuffer {
        sharedData.gpu.allocator,
        vk::BufferCreateInfo {
            {},
            sizeof(glm::mat4) * 8 + sizeof(glm::vec4) * 4,
            vk::BufferUsageFlagBits::eUniformBuffer,
        },
        vma::AllocationCreateInfo {
            vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped,
            vma::MemoryUsage::eAutoPreferDevice,
        },
    }
    , descriptorPool { createDescriptorPool() }
    , computeCommandPool { sharedData.gpu.device, vk::CommandPoolCreateInfo { {}, sharedData.gpu.queueFamilies.compute } }
    , graphicsCommandPool { sharedData.gpu.device, vk::CommandPoolCreateInfo { {}, sharedData.gpu.queueFamilies.graphicsPresent } }
    , scenePrepassFinishSema { sharedData.gpu.device, vk::SemaphoreCreateInfo{} }
    , sceneRenderingFinishSema { sharedData.gpu.device, vk::SemaphoreCreateInfo{} }
    , jumpFloodFinishSema { sharedData.gpu.device, vk::SemaphoreCreateInfo{} }
    , swapchainImageAcquireSema { sharedData.gpu.device, vk::SemaphoreCreateInfo{} }
    , inFlightFence { sharedData.gpu.device, vk::FenceCreateInfo{} } {
    // Allocate descriptor sets.
    std::tie(rendererSet, mousePickingSet, hoveringNodeJumpFloodSet, selectedNodeJumpFloodSet, hoveringNodeOutlineSet, selectedNodeOutlineSet, weightedBlendedCompositionSet, inverseToneMappingSet, bloomSet, bloomApplySet)
        = allocateDescriptorSets(*descriptorPool, std::tie(
            sharedData.rendererDescriptorSetLayout,
            sharedData.mousePickingDescriptorSetLayout,
            sharedData.jumpFloodComputePipeline.descriptorSetLayout,
            sharedData.jumpFloodComputePipeline.descriptorSetLayout,
            sharedData.outlineDescriptorSetLayout,
            sharedData.outlineDescriptorSetLayout,
            sharedData.weightedBlendedCompositionDescriptorSetLayout,
            sharedData.inverseToneMappingDescriptorSetLayout,
            sharedData.bloomComputePipeline.descriptorSetLayout,
            sharedData.bloomApplyDescriptorSetLayout));

    // Update descriptor sets.
    sharedData.gpu.device.updateDescriptorSets(
        rendererSet.getWriteOne<0>({ cameraBuffer, 0, vk::WholeSize }),
        {});

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
        if (gltfAsset->mousePickingInput) {
            const auto &rect = gltfAsset->mousePickingInput->second;
            if (rect.extent.width == 1 && rect.extent.height == 1) {
                std::uint16_t nodeIndex;
                if (sharedData.gpu.supportShaderBufferInt64Atomics) {
                    nodeIndex = gltfAsset->mousePickingResultBuffer.asValue<const std::uint64_t>() & 0xFFFF;
                }
                else {
                    nodeIndex = gltfAsset->mousePickingResultBuffer.asValue<const std::uint32_t>() & 0xFFFF;
                }

                if (nodeIndex != NO_INDEX) {
                    result.mousePickingResult.emplace<std::size_t>(nodeIndex);
                }
            }
            else {
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
            }
        }
    }

    return result;
}

void vk_gltf_viewer::vulkan::Frame::update(const ExecutionTask &task) {
    passthruOffset = task.passthruOffset;

    // Update camera buffer.
    std::byte* const cameraBufferMapped = static_cast<std::byte*>(sharedData.gpu.allocator.getAllocationInfo(cameraBuffer.allocation).pMappedData);
    std::ranges::transform(renderer->cameras, reinterpret_cast<glm::mat4*>(cameraBufferMapped), [this](const control::Camera &camera) {
        return camera.getProjectionViewMatrix(vku::aspect(viewport->extent));
    });
    std::ranges::transform(renderer->cameras, reinterpret_cast<glm::mat4*>(cameraBufferMapped + 4 * sizeof(glm::mat4)), [this](const control::Camera &camera) {
        // Skybox only works with perspective projection.
        return glm::perspectiveRH_ZO(camera.getEquivalentYFov(), vku::aspect(viewport->extent), camera.zMax, camera.zMin)
            * glm::mat4 { glm::mat3 { camera.getViewMatrix() } };
    });
    std::ranges::transform(renderer->cameras, reinterpret_cast<glm::vec4*>(cameraBufferMapped + 8 * sizeof(glm::mat4)), [](const control::Camera &camera) {
        return glm::vec4 { camera.position, 0.f };
    });

    if (!vku::contains(sharedData.gpu.allocator.getAllocationMemoryProperties(cameraBuffer.allocation), vk::MemoryPropertyFlagBits::eHostCoherent)) {
        sharedData.gpu.allocator.flushAllocation(cameraBuffer.allocation, 0, vk::WholeSize);
    }

    const auto criteriaGetter = [&](const fastgltf::Primitive &primitive) {
        const bool usePerFragmentEmissiveStencilExport = renderer->bloom.raw().mode == Renderer::Bloom::PerFragment;
        CommandSeparationCriteria result {
            .subpass = 0U,
            .indexType = value_if(ranges::contains(emulatedPrimitiveTopologies, primitive.type) || primitive.indicesAccessor.has_value(), [&]() {
                return gltfAsset->assetExtended->combinedIndexBuffer.getIndexTypeAndFirstIndex(primitive).first;
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
            const fastgltf::Material &material = gltfAsset->assetExtended->asset.materials[*primitive.materialIndex];
            result.subpass = material.alphaMode == fastgltf::AlphaMode::Blend;

            if (material.unlit || isPrimitivePointsOrLineWithoutNormal) {
                result.pipeline = *viewport->shared->getUnlitPrimitiveRenderPipeline(gltfAsset->assetExtended->getUnlitPrimitivePipelineConfig(primitive));
                // Disable stencil reference dynamic state when using unlit rendering pipeline.
                result.stencilReference.reset();
            }
            else {
                result.pipeline = *viewport->shared->getPrimitiveRenderPipeline(gltfAsset->assetExtended->getPrimitivePipelineConfig(primitive, usePerFragmentEmissiveStencilExport));
                if (!usePerFragmentEmissiveStencilExport) {
                    result.stencilReference.emplace(material.emissiveStrength > 1.f ? 1U : 0U);
                }
            }
            result.cullMode = material.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack;
        }
        else if (isPrimitivePointsOrLineWithoutNormal) {
            result.pipeline = *viewport->shared->getUnlitPrimitiveRenderPipeline(gltfAsset->assetExtended->getUnlitPrimitivePipelineConfig(primitive));
            // Disable stencil reference dynamic state when using unlit rendering pipeline.
            result.stencilReference.reset();
        }
        else {
            result.pipeline = *viewport->shared->getPrimitiveRenderPipeline(gltfAsset->assetExtended->getPrimitivePipelineConfig(primitive, usePerFragmentEmissiveStencilExport));
        }
        return result;
    };

    const auto mousePickingCriteriaGetter = [&](const fastgltf::Primitive &primitive) {
        CommandSeparationCriteriaNoShading result{
            .indexType = value_if(ranges::contains(emulatedPrimitiveTopologies, primitive.type) || primitive.indicesAccessor.has_value(), [&]() {
                return gltfAsset->assetExtended->combinedIndexBuffer.getIndexTypeAndFirstIndex(primitive).first;
            }),
            .primitiveTopology = getPrimitiveTopology(primitive.type),
            .cullMode = vk::CullModeFlagBits::eBack,
        };

        if (primitive.materialIndex) {
            const fastgltf::Material& material = gltfAsset->assetExtended->asset.materials[*primitive.materialIndex];
            if (material.alphaMode == fastgltf::AlphaMode::Mask) {
                result.pipeline = *sharedData.getMaskNodeMousePickingRenderPipeline(gltfAsset->assetExtended->getPrepassPipelineConfig<true>(primitive));
            }
            else {
                result.pipeline = *sharedData.getNodeMousePickingRenderPipeline(gltfAsset->assetExtended->getPrepassPipelineConfig<false>(primitive));
            }
            result.cullMode = material.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack;
        }
        else {
            result.pipeline = *sharedData.getNodeMousePickingRenderPipeline(gltfAsset->assetExtended->getPrepassPipelineConfig<false>(primitive));
        }
        return result;
    };

    const auto multiNodeMousePickingCriteriaGetter = [&](const fastgltf::Primitive &primitive) {
        CommandSeparationCriteriaNoShading result{
            .indexType = value_if(ranges::contains(emulatedPrimitiveTopologies, primitive.type) || primitive.indicesAccessor.has_value(), [&]() {
                return gltfAsset->assetExtended->combinedIndexBuffer.getIndexTypeAndFirstIndex(primitive).first;
            }),
            .primitiveTopology = getPrimitiveTopology(primitive.type),
            .cullMode = vk::CullModeFlagBits::eNone,
        };

        if (primitive.materialIndex) {
            const fastgltf::Material& material = gltfAsset->assetExtended->asset.materials[*primitive.materialIndex];
            if (material.alphaMode == fastgltf::AlphaMode::Mask) {
                result.pipeline = *sharedData.getMaskMultiNodeMousePickingRenderPipeline(gltfAsset->assetExtended->getPrepassPipelineConfig<true>(primitive));
            }
            else {
                result.pipeline = *sharedData.getMultiNodeMousePickingRenderPipeline(gltfAsset->assetExtended->getPrepassPipelineConfig<false>(primitive));
            }
        }
        else {
            result.pipeline = *sharedData.getMultiNodeMousePickingRenderPipeline(gltfAsset->assetExtended->getPrepassPipelineConfig<false>(primitive));
        }
        return result;
    };

    const auto jumpFloodSeedCriteriaGetter = [&](const fastgltf::Primitive &primitive) {
        CommandSeparationCriteriaNoShading result {
            .indexType = value_if(ranges::contains(emulatedPrimitiveTopologies, primitive.type) || primitive.indicesAccessor.has_value(), [&]() {
                return gltfAsset->assetExtended->combinedIndexBuffer.getIndexTypeAndFirstIndex(primitive).first;
            }),
            .primitiveTopology = getPrimitiveTopology(primitive.type),
            .cullMode = vk::CullModeFlagBits::eBack,
        };

        if (primitive.materialIndex) {
            const fastgltf::Material &material = gltfAsset->assetExtended->asset.materials[*primitive.materialIndex];
            if (material.alphaMode == fastgltf::AlphaMode::Mask) {
                result.pipeline = *viewport->shared->getMaskJumpFloodSeedRenderPipeline(gltfAsset->assetExtended->getPrepassPipelineConfig<true>(primitive));
            }
            else {
                result.pipeline = *viewport->shared->getJumpFloodSeedRenderPipeline(gltfAsset->assetExtended->getPrepassPipelineConfig<false>(primitive));
            }
            result.cullMode = material.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack;
        }
        else {
            result.pipeline = *viewport->shared->getJumpFloodSeedRenderPipeline(gltfAsset->assetExtended->getPrepassPipelineConfig<false>(primitive));
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
        std::uint32_t drawCount = gltfAsset->assetExtended->asset.accessors[drawCountDeterminingAccessorIndex].count;

        if (primitive.type == fastgltf::PrimitiveType::LineLoop) {
            // Since GL_LINE_LOOP primitive is emulated as LINE_STRIP draw, additional 1 index is used.
            ++drawCount;
        }
    #if __APPLE__
        else if (primitive.type == fastgltf::PrimitiveType::TriangleFan) {
            drawCount = 3 * (drawCount - 2);
        }
    #endif

        // EXT_mesh_gpu_instancing support.
        std::uint32_t instanceCount = 1;
        if (const fastgltf::Node &node = gltfAsset->assetExtended->asset.nodes[nodeIndex]; !node.instancingAttributes.empty()) {
            instanceCount = gltfAsset->assetExtended->asset.accessors[node.instancingAttributes[0].accessorIndex].count;
        }

        const std::size_t primitiveIndex = gltfAsset->assetExtended->primitiveBuffer.getPrimitiveIndex(primitive);

        // To embed the node and primitive indices into 32-bit unsigned integer, both must be in range of 16-bit unsigned integer.
        if (!std::in_range<std::uint16_t>(nodeIndex) || !std::in_range<std::uint16_t>(primitiveIndex)) {
            throw std::runtime_error { "Requirement violation: nodeIndex <= 65535 && primitiveIndex <= 65535" };
        }

        const std::uint32_t firstInstance = (static_cast<std::uint32_t>(nodeIndex) << 16U) | static_cast<std::uint32_t>(primitiveIndex);
        if (ranges::contains(emulatedPrimitiveTopologies, primitive.type) || primitive.indicesAccessor) {
            const std::uint32_t firstIndex = gltfAsset->assetExtended->combinedIndexBuffer.getIndexTypeAndFirstIndex(primitive).second;
            return vk::DrawIndexedIndirectCommand { drawCount, instanceCount, firstIndex, 0, firstInstance };
        }
        else {
            return vk::DrawIndirectCommand { drawCount, instanceCount, 0, firstInstance };
        }
    };

    if (task.gltf) {
        const auto isPrimitiveWithinFrustum = [&](std::size_t nodeIndex, std::size_t primitiveIndex, const math::Frustum &frustum) -> bool {
            const fastgltf::Node &node = gltfAsset->assetExtended->asset.nodes[nodeIndex];
            const auto [min, max] = getBoundingBoxMinMax(gltfAsset->assetExtended->primitiveBuffer.getPrimitive(primitiveIndex), node, gltfAsset->assetExtended->asset);

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
                        const fastgltf::Node &node = gltfAsset->assetExtended->asset.nodes[nodeIndex];

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
                                    ? gltfAsset->assetExtended->asset.accessors[node.instancingAttributes.front().accessorIndex].count : 0U;
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
            for (const auto &[nodeIndex, visible] : gltfAsset->assetExtended->sceneNodeVisibilities.getVisibilities() | ranges::views::enumerate) {
                if (visible) {
                    visibleNodeIndices.push_back(nodeIndex);
                }
            }

            renderingNodes.emplace(
                buffer::createIndirectDrawCommandBuffers(gltfAsset->assetExtended->asset, sharedData.gpu.allocator, criteriaGetter, visibleNodeIndices, drawCommandGetter),
                buffer::createIndirectDrawCommandBuffers(gltfAsset->assetExtended->asset, sharedData.gpu.allocator, mousePickingCriteriaGetter, visibleNodeIndices, drawCommandGetter),
                buffer::createIndirectDrawCommandBuffers(gltfAsset->assetExtended->asset, sharedData.gpu.allocator, multiNodeMousePickingCriteriaGetter, visibleNodeIndices, drawCommandGetter));
        }


        if (renderer->frustumCullingMode != Renderer::FrustumCullingMode::Off) {
            assert(renderer->cameras.size() == 1 && "Multiview frustum culling is not supported yet");

            const math::Frustum frustum = renderer->cameras[0].getFrustum(vku::aspect(viewport->extent));
            for (buffer::IndirectDrawCommands &buffer : renderingNodes->indirectDrawCommandBuffers | std::views::values) {
                commandBufferCullingFunc(buffer, frustum);
            }

            // Do frustum culling and do mouse picking only if there's any mesh primitive inside the frustum.
            renderingNodes->startMousePickingRenderPass = false;
            if (task.gltf->mousePickingInput) {
                const auto &rect = task.gltf->mousePickingInput->second;
                // TODO: use ray-sphere intersection test instead of frustum overlap test when extent is 1x1.
                const float xmin = static_cast<float>(rect.offset.x) / viewport->extent.width;
                const float xmax = static_cast<float>(rect.offset.x + rect.extent.width) / viewport->extent.width;
                const float ymin = 1.f - static_cast<float>(rect.offset.y + rect.extent.height) / viewport->extent.height;
                const float ymax = 1.f - static_cast<float>(rect.offset.y) / viewport->extent.height;
                const math::Frustum frustum = renderer->cameras[0].getFrustum(vku::aspect(viewport->extent), xmin, xmax, ymin, ymax);

                auto &map = (rect.extent.width == 1 && rect.extent.height == 1)
                    ? renderingNodes->mousePickingIndirectDrawCommandBuffers
                    : renderingNodes->multiNodeMousePickingIndirectDrawCommandBuffers;

                renderingNodes->startMousePickingRenderPass = false;
                for (buffer::IndirectDrawCommands &buffer : map | std::views::values) {
                    renderingNodes->startMousePickingRenderPass |= commandBufferCullingFunc(buffer, frustum);
                }
            }
        }
        else {
            for (buffer::IndirectDrawCommands &buffer : renderingNodes->indirectDrawCommandBuffers | std::views::values) {
                buffer.resetDrawCount();
            }

            if (task.gltf->mousePickingInput) {
                const auto &rect = task.gltf->mousePickingInput->second;
                auto &map = (rect.extent.width == 1 && rect.extent.height == 1)
                    ? renderingNodes->mousePickingIndirectDrawCommandBuffers
                    : renderingNodes->multiNodeMousePickingIndirectDrawCommandBuffers;
                for (buffer::IndirectDrawCommands &buffer : map | std::views::values) {
                    buffer.resetDrawCount();
                }
            }
        }

        if (!gltfAsset->assetExtended->selectedNodes.empty() && renderer->selectedNodeOutline) {
            const auto getSelectionHash = [&] {
                return boost::hash_unordered_range(gltfAsset->assetExtended->selectedNodes.begin(), gltfAsset->assetExtended->selectedNodes.end());
            };

            std::size_t indexHash;
            if (!selectedNodes /* asset has selected nodes but frame doesn't */ ||
                task.gltf->regenerateDrawCommands /* draw call regeneration explicitly requested */ ||
                (indexHash = getSelectionHash()) != selectedNodes->indexHash /* asset node selection has been changed */ ) {
                selectedNodes.emplace(
                    indexHash,
                    buffer::createIndirectDrawCommandBuffers(gltfAsset->assetExtended->asset, sharedData.gpu.allocator, jumpFloodSeedCriteriaGetter, gltfAsset->assetExtended->selectedNodes, drawCommandGetter));
            }

            if (renderer->frustumCullingMode != Renderer::FrustumCullingMode::Off) {
                assert(renderer->cameras.size() == 1 && "Multiview frustum culling is not supported yet");

                const math::Frustum frustum = renderer->cameras[0].getFrustum(vku::aspect(viewport->extent));
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
            const auto &selectedNodes = gltfAsset->assetExtended->selectedNodes;
            return selectedNodes.size() == 1 && *gltfAsset->assetExtended->hoveringNode == *selectedNodes.begin();
        };

        if (gltfAsset->assetExtended->hoveringNode &&
            renderer->hoveringNodeOutline &&
            !isHoveringNodeAndSelectedNodeEqual() /* in this case, hovering node outline doesn't have to be drawn */) {
            if (!hoveringNode /* asset has hovering node but frame doesn't */ ||
                task.gltf->regenerateDrawCommands /* draw call regeneration explicitly requested */ ||
                *gltfAsset->assetExtended->hoveringNode != hoveringNode->index /* asset hovering node has been changed */) {
                hoveringNode.emplace(
                    *gltfAsset->assetExtended->hoveringNode,
                    buffer::createIndirectDrawCommandBuffers(gltfAsset->assetExtended->asset, sharedData.gpu.allocator, jumpFloodSeedCriteriaGetter, std::views::single(*gltfAsset->assetExtended->hoveringNode), drawCommandGetter));
            }

            if (renderer->frustumCullingMode != Renderer::FrustumCullingMode::Off) {
                assert(renderer->cameras.size() == 1 && "Multiview frustum culling is not supported yet");

                const math::Frustum frustum = renderer->cameras[0].getFrustum(vku::aspect(viewport->extent));
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
                viewport->hoveringNodeOutlineJumpFloodResources.image,
                hoveringNodeJumpFloodSet,
                std::bit_ceil(static_cast<std::uint32_t>(renderer->hoveringNodeOutline->thickness)));
            sharedData.gpu.device.updateDescriptorSets(
                hoveringNodeOutlineSet.getWriteOne<0>({
                    {},
                    *viewport->hoveringNodeOutlineJumpFloodResources.pingPongImageViews[*hoveringNodeJumpFloodForward],
                    vk::ImageLayout::eShaderReadOnlyOptimal,
                }),
                {});
        }
        if (selectedNodes) {
            selectedNodeJumpFloodForward = recordJumpFloodComputeCommands(
                jumpFloodCommandBuffer,
                viewport->selectedNodeOutlineJumpFloodResources.image,
                selectedNodeJumpFloodSet,
                std::bit_ceil(static_cast<std::uint32_t>(renderer->selectedNodeOutline->thickness)));
            sharedData.gpu.device.updateDescriptorSets(
                selectedNodeOutlineSet.getWriteOne<0>({
                    {},
                    *viewport->selectedNodeOutlineJumpFloodResources.pingPongImageViews[*selectedNodeJumpFloodForward],
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

        sceneRenderingCommandBuffer.setViewport(0, vku::toViewport(viewport->extent, true));
        sceneRenderingCommandBuffer.setScissor(0, vk::Rect2D { { 0, 0 }, viewport->extent });

        vk::ClearColorValue backgroundColor { 0.f, 0.f, 0.f, 0.f };
        if (renderer->solidBackground) {
            backgroundColor.setFloat32({ renderer->solidBackground->x, renderer->solidBackground->y, renderer->solidBackground->z, 1.f });
        }
        sceneRenderingCommandBuffer.beginRenderPass({
            *viewport->shared->sceneRenderPass,
            *viewport->sceneFramebuffer,
            vk::Rect2D { { 0, 0 }, viewport->extent },
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
                *viewport->shared->weightedBlendedCompositionRenderPipeline);
            sceneRenderingCommandBuffer.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                *sharedData.weightedBlendedCompositionPipelineLayout,
                0, weightedBlendedCompositionSet, {});
            sceneRenderingCommandBuffer.draw(3, 1, 0, 0);
        }

        sceneRenderingCommandBuffer.nextSubpass(vk::SubpassContents::eInline);

        // Inverse tone-map the result image to bloomImage[mipLevel=0] when bloom is enabled.
        if (renderer->bloom) {
            sceneRenderingCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *viewport->shared->inverseToneMappingRenderPipeline);
            sceneRenderingCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.inverseToneMappingPipelineLayout, 0, inverseToneMappingSet, {});
            sceneRenderingCommandBuffer.draw(3, 1, 0, 0);
        }

        sceneRenderingCommandBuffer.endRenderPass();

        if (renderer->bloom) {
            sceneRenderingCommandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eComputeShader,
                {},
                vk::MemoryBarrier { vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eShaderRead },
                {}, {});

            sharedData.bloomComputePipeline.compute(sceneRenderingCommandBuffer, bloomSet, viewport->extent, viewport->bloomImage.mipLevels, renderer->cameras.size());

            sceneRenderingCommandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eColorAttachmentOutput,
                {}, {}, {},
                vk::ImageMemoryBarrier {
                    vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eInputAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,
                    vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
                    vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                    viewport->bloomImage, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, vk::RemainingArrayLayers },
                });

            sceneRenderingCommandBuffer.beginRenderPass({
                *viewport->shared->bloomApplyRenderPass,
                *viewport->bloomApplyFramebuffer,
                vk::Rect2D { { 0, 0 }, viewport->extent },
                vku::unsafeProxy<vk::ClearValue>({
                    vk::ClearColorValue{},
                    vk::ClearColorValue{},
                }),
            }, vk::SubpassContents::eInline);

            sceneRenderingCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *viewport->shared->bloomApplyRenderPipeline);
            sceneRenderingCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.bloomApplyPipelineLayout, 0, bloomApplySet, {});
            sceneRenderingCommandBuffer.pushConstants<pl::BloomApply::PushConstant>(
                *sharedData.bloomApplyPipelineLayout,
                vk::ShaderStageFlagBits::eFragment,
                0, pl::BloomApply::PushConstant { .factor = renderer->bloom->intensity });
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
                    viewport->sceneAttachmentGroup.colorImage, vku::fullSubresourceRange(),
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
        boost::container::static_vector<vk::Offset2D, 4> copyRegionOffsets;
        switch (renderer->cameras.size()) {
            case 1:
                copyRegionOffsets.push_back(passthruOffset);
                break;
            case 2:
                copyRegionOffsets.push_back(passthruOffset);
                copyRegionOffsets.emplace_back(passthruOffset.x + viewport->extent.width, passthruOffset.y);
                break;
            case 4:
                copyRegionOffsets.push_back(passthruOffset);
                copyRegionOffsets.emplace_back(passthruOffset.x + viewport->extent.width, passthruOffset.y);
                copyRegionOffsets.emplace_back(passthruOffset.x, passthruOffset.y + viewport->extent.height);
                copyRegionOffsets.emplace_back(passthruOffset.x + viewport->extent.width, passthruOffset.y + viewport->extent.height);
                break;
            default:
                std::unreachable(); // Only 1, 2, and 4 views are supported.
        }
        compositionCommandBuffer.copyImage(
            viewport->sceneAttachmentGroup.colorImage, vk::ImageLayout::eTransferSrcOptimal,
            swapchain.images[swapchainImageIndex], vk::ImageLayout::eTransferDstOptimal,
            copyRegionOffsets
                | ranges::views::enumerate
                | std::views::transform(decomposer([this](std::uint32_t imageLayer, const vk::Offset2D &offset) {
                    return vk::ImageCopy {
                        { vk::ImageAspectFlagBits::eColor, 0, imageLayer, 1 },
                        { 0, 0, 0 },
                        { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
                        vk::Offset3D { offset, 0 }, vk::Extent3D { viewport->extent, 1 },
                    };
                }))
                | std::ranges::to<boost::container::static_vector<vk::ImageCopy, 4>>());

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

void vk_gltf_viewer::vulkan::Frame::setViewportExtent(const vk::Extent2D &extent) {
    vk::raii::Fence fence { sharedData.gpu.device, vk::FenceCreateInfo{} };
    vku::executeSingleCommand(*sharedData.gpu.device, *graphicsCommandPool, sharedData.gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
        const std::uint32_t viewCount = static_cast<std::uint32_t>(renderer->cameras.size());
        const std::uint32_t viewMask = math::bit::ones(viewCount);
        viewport.emplace(sharedData.gpu, extent, viewCount, sharedData.viewMaskDependentResources.at(viewMask), cb);
    }, *fence);

    std::vector<vk::DescriptorImageInfo> bloomSetDescriptorInfos;
    if (sharedData.gpu.supportShaderImageLoadStoreLod) {
        bloomSetDescriptorInfos.push_back({ {}, *viewport->bloomImageView, vk::ImageLayout::eGeneral });
    }
    else {
        bloomSetDescriptorInfos.append_range(viewport->bloomMipImageViews | std::views::transform([this](vk::ImageView imageView) {
            return vk::DescriptorImageInfo{ {}, imageView, vk::ImageLayout::eGeneral };
        }));
    }

    sharedData.gpu.device.updateDescriptorSets({
        hoveringNodeJumpFloodSet.getWriteOne<0>({ {}, *viewport->hoveringNodeOutlineJumpFloodResources.imageView, vk::ImageLayout::eGeneral }),
        selectedNodeJumpFloodSet.getWriteOne<0>({ {}, *viewport->selectedNodeOutlineJumpFloodResources.imageView, vk::ImageLayout::eGeneral }),
        weightedBlendedCompositionSet.getWrite<0>(vku::unsafeProxy({
            vk::DescriptorImageInfo { {}, *viewport->sceneAttachmentGroup.accumulationImageView, vk::ImageLayout::eShaderReadOnlyOptimal },
            vk::DescriptorImageInfo { {}, *viewport->sceneAttachmentGroup.revealageImageView, vk::ImageLayout::eShaderReadOnlyOptimal },
        })),
        inverseToneMappingSet.getWriteOne<0>({ {}, *viewport->sceneAttachmentGroup.colorImageView, vk::ImageLayout::eShaderReadOnlyOptimal }),
        bloomSet.getWriteOne<0>({ {}, *viewport->bloomImageView, vk::ImageLayout::eGeneral }),
        bloomSet.getWrite<1>(bloomSetDescriptorInfos),
        bloomApplySet.getWriteOne<0>({ {}, *viewport->sceneAttachmentGroup.colorImageView, vk::ImageLayout::eGeneral }),
        bloomApplySet.getWriteOne<1>({ {}, *viewport->bloomMipImageViews[0], vk::ImageLayout::eShaderReadOnlyOptimal }),
    }, {});

    // TODO: can this operation be non-blocking?
    std::ignore = sharedData.gpu.device.waitForFences(*fence, true, ~0ULL);
}

void vk_gltf_viewer::vulkan::Frame::updateAsset() {
    constexpr auto getVariableDescriptorCount = [](const fastgltf::Asset &asset) noexcept {
    #if __APPLE__
        return static_cast<std::uint32_t>(1 + asset.images.size());
    #else
        return static_cast<std::uint32_t>(1 + asset.textures.size());
    #endif
    };

    const std::optional<std::uint32_t> oldVariableDescriptorCount = gltfAsset.transform([&](const GltfAsset &vkAsset) {
        return getVariableDescriptorCount(vkAsset.assetExtended->asset);
    });
    const auto &inner = gltfAsset.emplace(sharedData);

    bool assetDescriptorSetReallocated = false;
    if (std::uint32_t variableDescriptorCount = getVariableDescriptorCount(inner.assetExtended->asset);
        !oldVariableDescriptorCount || variableDescriptorCount > *oldVariableDescriptorCount) {
        // If variable descriptor count has greater than the previous, descriptor set must be reallocated with the new
        // increased count.
        (*sharedData.gpu.device).freeDescriptorSets(*descriptorPool, assetDescriptorSet);
        assetDescriptorSet = decltype(assetDescriptorSet) {
            vku::unsafe,
            (*sharedData.gpu.device).allocateDescriptorSets(vk::StructureChain {
                vk::DescriptorSetAllocateInfo {
                    *descriptorPool,
                    *sharedData.assetDescriptorSetLayout,
                },
                vk::DescriptorSetVariableDescriptorCountAllocateInfo { vk::ArrayProxyNoTemporaries<const std::uint32_t> { variableDescriptorCount } },
             }.get())[0],
        };
        assetDescriptorSetReallocated = true;
    }

    // Update the descriptors that are unrelated to the asset textures.
    sharedData.gpu.device.updateDescriptorSets({
        mousePickingSet.getWriteOne<0>({ inner.mousePickingResultBuffer, 0, vk::WholeSize }),
        assetDescriptorSet.getWrite<0>(inner.assetExtended->primitiveBuffer.descriptorInfo),
        assetDescriptorSet.getWrite<1>(inner.nodeBuffer.descriptorInfo),
        assetDescriptorSet.getWrite<2>(inner.assetExtended->materialBuffer.descriptorInfo),
    }, {});

#if __APPLE__
    std::vector<vk::WriteDescriptorSet> descriptorWrites;

    // Usually, an asset does not have a sampler (it relies on the default sampler) or has only one sampler.
    // Therefore, capacity of 2 is enough for storing the fallback texture sampler and the asset sampler.
    boost::container::small_vector<vk::DescriptorImageInfo, 2> samplerInfos;
    samplerInfos.reserve(1 + inner.assetExtended->asset.samplers.size());

    const vk::DescriptorImageInfo fallbackImageInfo { {}, *sharedData.fallbackTexture.imageView, vk::ImageLayout::eShaderReadOnlyOptimal };
    if (assetDescriptorSetReallocated) {
        // Write fallback texture sampler and image.
        samplerInfos.emplace_back(*sharedData.fallbackTexture.sampler);
        descriptorWrites.push_back(assetDescriptorSet.getWrite<4>(fallbackImageInfo));
    }

    for (vk::Sampler sampler : inner.assetExtended->textures.samplers) {
        samplerInfos.emplace_back(sampler);
    }

    if (!samplerInfos.empty()) {
        descriptorWrites.push_back(assetDescriptorSet.getWrite<3>(samplerInfos).setDstArrayElement(assetDescriptorSetReallocated ? 0 : 1));
    }

    std::vector<std::vector<vk::DescriptorImageInfo>> chunkedImageInfos;
    if (!inner.assetExtended->asset.textures.empty()) {
        // Get chunks of contiguous image indices.
        // For example, if asset has 16 images, and only 2, 3, 5, 6, 7, 10 are used, then total 3 vk::DescriptorImageInfo
        // structs are needed and their corresponding dstArrayElement and descriptorCount are: (3, 2), (6, 3), (11, 1).
        // Remind that the 0-th array element is reserved for the fallback texture, so 3, 6 and 11 are obtained by adding 1
        // to the 2, 5, 10.

        std::vector<std::size_t> usedImageIndices;
        for (const fastgltf::Texture &texture : inner.assetExtended->asset.textures) {
            usedImageIndices.push_back(getPreferredImageIndex(texture));
        }
        std::ranges::sort(usedImageIndices);
        const auto [begin, end] = std::ranges::unique(usedImageIndices);
        usedImageIndices.erase(begin, end);

        for (const auto &chunk : usedImageIndices | std::views::chunk_by([](auto a, auto b) { return b - a == 1; })) {
            std::span infos = chunkedImageInfos.emplace_back(
                std::from_range,
                chunk | std::views::transform([&](std::size_t imageIndex) {
                    return vk::DescriptorImageInfo { {}, *inner.assetExtended->textures.images.at(imageIndex).view, vk::ImageLayout::eShaderReadOnlyOptimal };
                }));
            descriptorWrites.push_back(assetDescriptorSet.getWrite<4>(infos).setDstArrayElement(1 + chunk.front()));
        }
    }

    if (!descriptorWrites.empty()) {
        sharedData.gpu.device.updateDescriptorSets(descriptorWrites, {});
    }
#else
    // If asset descriptor is not reallocated (therefore the previously written fallback texture info is still valid)
    // then no need to write fallback texture info. Therefore, we should call vkUpdateDescriptorSets only if
    // assetDescriptorSetReallocated == true || asset.textures.size() > 0.
    if (std::uint32_t infoCount = inner.assetExtended->asset.textures.size() + assetDescriptorSetReallocated; infoCount > 0) {
        std::vector<vk::DescriptorImageInfo> textureInfos;
        textureInfos.reserve(infoCount);

        std::uint32_t dstArrayElement = 1;
        if (assetDescriptorSetReallocated) {
            textureInfos.emplace_back(*sharedData.fallbackTexture.sampler, *sharedData.fallbackTexture.imageView, vk::ImageLayout::eShaderReadOnlyOptimal);
            dstArrayElement = 0;
        }
        textureInfos.append_range(inner.assetExtended->textures.descriptorInfos);

        sharedData.gpu.device.updateDescriptorSets(assetDescriptorSet.getWrite<3>(textureInfos).setDstArrayElement(dstArrayElement), {});
    }
#endif
}

vk_gltf_viewer::vulkan::Frame::Viewport::JumpFloodResources::JumpFloodResources(
    const Gpu &gpu,
    const vk::Extent2D &extent,
    std::uint32_t viewCount
) : image { gpu.allocator, vk::ImageCreateInfo {
        {},
        vk::ImageType::e2D,
        vk::Format::eR16G16Uint,
        vk::Extent3D { extent, 1 },
        1, 2 * viewCount, // arrayLevels=0..viewCount for ping image, arrayLevels=viewCount.. for pong image.
        vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eColorAttachment /* write from JumpFloodSeedRenderPipeline */
            | vk::ImageUsageFlagBits::eStorage /* used as ping pong image in JumpFloodComputePipeline */
            | vk::ImageUsageFlagBits::eSampled /* read in OutlineRenderPipeline */,
        gpu.queueFamilies.uniqueIndices.size() == 1 ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent,
        gpu.queueFamilies.uniqueIndices,
    } },
    imageView { gpu.device, image.getViewCreateInfo(vk::ImageViewType::e2DArray) },
    pingPongImageViews { INDEX_SEQ(Is, 2, {
        return std::array { vk::raii::ImageView { gpu.device, image.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, static_cast<std::uint32_t>(Is * viewCount), viewCount }, vk::ImageViewType::e2DArray) }... };
    }) } { }

vk_gltf_viewer::vulkan::Frame::Viewport::Viewport(
    const Gpu &gpu,
    const vk::Extent2D &extent,
    std::uint32_t viewCount,
    const SharedData::ViewMaskDependentResources &shared,
    vk::CommandBuffer graphicsCommandBuffer
) : extent { extent },
    shared { &shared },
    mousePickingAttachmentGroup { value_if(gpu.workaround.attachmentLessRenderPass, [&] { return ag::MousePicking { gpu, extent }; }) },
    hoveringNodeOutlineJumpFloodResources { gpu, extent, viewCount },
    hoveringNodeJumpFloodSeedAttachmentGroup { gpu, hoveringNodeOutlineJumpFloodResources.image, viewCount },
    selectedNodeOutlineJumpFloodResources { gpu, extent, viewCount },
    selectedNodeJumpFloodSeedAttachmentGroup { gpu, selectedNodeOutlineJumpFloodResources.image, viewCount },
    sceneAttachmentGroup { gpu, extent, viewCount },
    bloomImage { gpu.allocator, vk::ImageCreateInfo {
        {},
        vk::ImageType::e2D,
        vk::Format::eR16G16B16A16Sfloat,
        vk::Extent3D { extent, 1 },
        vku::Image::maxMipLevels(extent), viewCount,
        vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eColorAttachment // written in InverseToneMappingRenderPipeline
            | bloom::BloomComputePipeline::requiredImageUsageFlags
            | vk::ImageUsageFlagBits::eInputAttachment /* read in BloomApplyRenderPipeline */,
    } },
    bloomImageView { gpu.device, bloomImage.getViewCreateInfo(vk::ImageViewType::e2DArray) },
    bloomMipImageViews { [&] {
        std::vector<vk::raii::ImageView> result;
        result.emplace_back(gpu.device, bloomImage.getViewCreateInfo(
            { vk::ImageAspectFlagBits::eColor, 0, 1, 0, vk::RemainingArrayLayers }, vk::ImageViewType::e2DArray));

        if (!gpu.supportShaderImageLoadStoreLod) {
            result.append_range(
                bloomImage.getMipViewCreateInfos(vk::ImageViewType::e2DArray)
                | std::views::drop(1)
                | std::views::transform([&](const vk::ImageViewCreateInfo& createInfo) {
                    return vk::raii::ImageView{ gpu.device, createInfo };
                }));
        }

        return result;
    }() },
    sceneFramebuffer { gpu.device, vk::FramebufferCreateInfo {
        {},
        *shared.sceneRenderPass,
        vku::unsafeProxy({
            *sceneAttachmentGroup.multisampleColorImageView,
            *sceneAttachmentGroup.colorImageView,
            *sceneAttachmentGroup.depthStencilImageView,
            *sceneAttachmentGroup.stencilResolveImageView,
            *sceneAttachmentGroup.multisampleAccumulationImageView,
            *sceneAttachmentGroup.accumulationImageView,
            *sceneAttachmentGroup.multisampleRevealageImageView,
            *sceneAttachmentGroup.revealageImageView,
            *bloomMipImageViews[0],
        }),
        extent.width, extent.height, 1,
    } },
    bloomApplyFramebuffer { gpu.device, vk::FramebufferCreateInfo {
        {},
        *shared.bloomApplyRenderPass,
        vku::unsafeProxy({
            *sceneAttachmentGroup.colorImageView,
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

    boost::container::static_vector<vk::ImageMemoryBarrier, 6> imageMemoryBarriers {
        layoutTransitionBarrier(vk::ImageLayout::eGeneral, hoveringNodeOutlineJumpFloodResources.image, { vk::ImageAspectFlagBits::eColor, 0, 1, viewCount, vk::RemainingArrayLayers } /* pong image */),
        layoutTransitionBarrier(vk::ImageLayout::eDepthAttachmentOptimal, hoveringNodeJumpFloodSeedAttachmentGroup.depthImage, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth)),
        layoutTransitionBarrier(vk::ImageLayout::eGeneral, selectedNodeOutlineJumpFloodResources.image, { vk::ImageAspectFlagBits::eColor, 0, 1, viewCount, vk::RemainingArrayLayers } /* pong image */),
        layoutTransitionBarrier(vk::ImageLayout::eDepthAttachmentOptimal, selectedNodeJumpFloodSeedAttachmentGroup.depthImage, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth)),
        layoutTransitionBarrier(vk::ImageLayout::eGeneral, bloomImage, { vk::ImageAspectFlagBits::eColor, 1, vk::RemainingMipLevels, 0, vk::RemainingArrayLayers }),
    };
    if (mousePickingAttachmentGroup) {
        imageMemoryBarriers.push_back(layoutTransitionBarrier(vk::ImageLayout::eDepthAttachmentOptimal, mousePickingAttachmentGroup->depthStencilAttachment->image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth)));
    }

    graphicsCommandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe,
        {}, {}, {}, imageMemoryBarriers);
}

vk::raii::DescriptorPool vk_gltf_viewer::vulkan::Frame::createDescriptorPool() const {
    const vku::PoolSizes poolSizes
        = sharedData.rendererDescriptorSetLayout.getPoolSize()
        + sharedData.mousePickingDescriptorSetLayout.getPoolSize()
        + 2 * getPoolSizes(sharedData.jumpFloodComputePipeline.descriptorSetLayout, sharedData.outlineDescriptorSetLayout)
        + sharedData.weightedBlendedCompositionDescriptorSetLayout.getPoolSize()
        + sharedData.inverseToneMappingDescriptorSetLayout.getPoolSize()
        + sharedData.bloomComputePipeline.descriptorSetLayout.getPoolSize()
        + sharedData.bloomApplyDescriptorSetLayout.getPoolSize()
        + sharedData.assetDescriptorSetLayout.getPoolSize();
    return { sharedData.gpu.device, poolSizes.getDescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet | vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind) };
}

void vk_gltf_viewer::vulkan::Frame::recordScenePrepassCommands(vk::CommandBuffer cb) const {
    // If hovering/selected node's outline have to be rendered, prepare attachment layout transition for jump flood seeding.
    const auto getJumpFloodSeedImageMemoryBarrier = [this](vk::Image image) -> vk::ImageMemoryBarrier {
        return {
            {}, vk::AccessFlagBits::eColorAttachmentWrite,
            {}, vk::ImageLayout::eColorAttachmentOptimal,
            vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
            image, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, static_cast<std::uint32_t>(renderer->cameras.size()) } /* ping image */,
        };
    };

    boost::container::static_vector<vk::ImageMemoryBarrier, 2> memoryBarriers;
    if (selectedNodes) {
        memoryBarriers.push_back(getJumpFloodSeedImageMemoryBarrier(viewport->selectedNodeOutlineJumpFloodResources.image));
    }
    if (hoveringNode) {
        memoryBarriers.push_back(getJumpFloodSeedImageMemoryBarrier(viewport->hoveringNodeOutlineJumpFloodResources.image));
    }

    if (!memoryBarriers.empty()) {
        // Attachment layout transitions.
        cb.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eColorAttachmentOutput,
            {}, {}, {}, memoryBarriers);

        cb.setViewport(0, vku::toViewport(viewport->extent, true));
        cb.setScissor(0, vk::Rect2D{ { 0, 0 }, viewport->extent });

        struct ResourceBindingState {
            vk::Pipeline pipeline;
            std::optional<vk::PrimitiveTopology> primitiveTopology;
            std::optional<vk::CullModeFlagBits> cullMode;
            std::optional<vk::IndexType> indexType;

            // Every variant of (Mask)JumpFloodSeedRenderer shares the same pipeline layout,
            // therefore their descriptor sets and push constants should be bound only once.
            bool descriptorSetBound = false;
        } resourceBindingState{};

        auto drawPrimitives = [&](const auto &indirectDrawCommandBuffers) {
            for (const auto &[criteria, indirectDrawCommandBuffer] : indirectDrawCommandBuffers) {
                if (resourceBindingState.pipeline != criteria.pipeline) {
                    cb.bindPipeline(vk::PipelineBindPoint::eGraphics, resourceBindingState.pipeline = criteria.pipeline);
                }

                if (!resourceBindingState.descriptorSetBound) {
                    cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.primitiveNoShadingPipelineLayout,
                        0, { rendererSet, assetDescriptorSet }, {});
                    resourceBindingState.descriptorSetBound = true;
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
                        gltfAsset->assetExtended->combinedIndexBuffer,
                        gltfAsset->assetExtended->combinedIndexBuffer.getIndexOffsetAndSize(*resourceBindingState.indexType).first,
                        *resourceBindingState.indexType);
                }
                indirectDrawCommandBuffer.recordDrawCommand(cb, sharedData.gpu.supportDrawIndirectCount);
            }
        };

        // Seeding jump flood initial image for hovering node.
        if (hoveringNode) {
            cb.beginRenderingKHR({
                {},
                { {}, viewport->extent },
                static_cast<std::uint32_t>(renderer->cameras.size()),
                math::bit::ones(renderer->cameras.size()),
                vku::unsafeProxy(vk::RenderingAttachmentInfo {
                    *viewport->hoveringNodeJumpFloodSeedAttachmentGroup.seedImageView, vk::ImageLayout::eColorAttachmentOptimal,
                    {}, {}, {},
                    vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                }),
                vku::unsafeAddress(vk::RenderingAttachmentInfo {
                    *viewport->hoveringNodeJumpFloodSeedAttachmentGroup.depthImageView, vk::ImageLayout::eDepthStencilAttachmentOptimal,
                    {}, {}, {},
                    vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
                })
            });
            drawPrimitives(hoveringNode->jumpFloodSeedIndirectDrawCommandBuffers);
            cb.endRenderingKHR();
        }

        // Seeding jump flood initial image for selected node.
        if (selectedNodes) {
            cb.beginRenderingKHR({
                {},
                { {}, viewport->extent },
                static_cast<std::uint32_t>(renderer->cameras.size()),
                math::bit::ones(renderer->cameras.size()),
                vku::unsafeProxy(vk::RenderingAttachmentInfo {
                    *viewport->selectedNodeJumpFloodSeedAttachmentGroup.seedImageView, vk::ImageLayout::eColorAttachmentOptimal,
                    {}, {}, {},
                    vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                }),
                vku::unsafeAddress(vk::RenderingAttachmentInfo {
                    *viewport->selectedNodeJumpFloodSeedAttachmentGroup.depthImageView, vk::ImageLayout::eDepthStencilAttachmentOptimal,
                    {}, {}, {},
                    vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
                })
            });
            drawPrimitives(selectedNodes->jumpFloodSeedIndirectDrawCommandBuffers);
            cb.endRenderingKHR();
        }
    }

    // Mouse picking.
    if (renderingNodes && gltfAsset->mousePickingInput) {
        const auto &[viewIndex, rect] = *gltfAsset->mousePickingInput;
        const bool singlePixel = rect.extent.width == 1 && rect.extent.height == 1;
        if (singlePixel) {
            if (sharedData.gpu.supportShaderBufferInt64Atomics) {
                constexpr std::uint64_t initialValue = NO_INDEX;
                sharedData.gpu.allocator.copyMemoryToAllocation(&initialValue, gltfAsset->mousePickingResultBuffer.allocation, 0, sizeof(initialValue));
            }
            else {
                constexpr std::uint32_t initialValue = NO_INDEX;
                sharedData.gpu.allocator.copyMemoryToAllocation(&initialValue, gltfAsset->mousePickingResultBuffer.allocation, 0, sizeof(initialValue));
            }
        }
        else {
            // Clear mousePickingResultBuffer as zeros.
        #if __APPLE__
            // Filling buffer with a value needs MTLBlitCommandEncoder in Metal, and it breaks the render pass.
            // It is better to use host memset for this purpose.
            std::memset(gltfAsset->mousePickingResultBuffer.data, 0, gltfAsset->mousePickingResultBuffer.size);
        #else
            cb.fillBuffer(gltfAsset->mousePickingResultBuffer, 0, gltfAsset->mousePickingResultBuffer.size, 0U);
            cb.pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
                {}, vk::MemoryBarrier {
                    vk::AccessFlagBits::eTransferWrite,
                    vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
                }, {}, {});
        #endif
        }

        if (renderingNodes->startMousePickingRenderPass) {
            cb.beginRenderingKHR(vk::RenderingInfo {
                {},
                rect,
                1,
                // See doc about Gpu::Workaround::attachmentLessRenderPass.
                0,
                vk::ArrayProxyNoTemporaries<const vk::RenderingAttachmentInfo>{},
                value_address(viewport->mousePickingAttachmentGroup.transform([](const ag::MousePicking &ag) {
                    return vk::RenderingAttachmentInfo {
                        *ag.depthStencilAttachment->view, vk::ImageLayout::eDepthAttachmentOptimal,
                        {}, {}, {},
                        vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                    };
                })),
            });

            cb.setViewport(0, vku::toViewport(viewport->extent, true));
            cb.setScissor(0, rect);

            cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.mousePickingPipelineLayout,
                0, { rendererSet, assetDescriptorSet, mousePickingSet }, {});
            cb.pushConstants<pl::MousePicking::PushConstant>(*sharedData.mousePickingPipelineLayout, vk::ShaderStageFlagBits::eVertex,
                0, pl::MousePicking::PushConstant { viewIndex });

            struct ResourceBindingState {
                vk::Pipeline pipeline;
                std::optional<vk::PrimitiveTopology> primitiveTopology;
                std::optional<vk::CullModeFlagBits> cullMode;
                std::optional<vk::IndexType> indexType;
            } resourceBindingState{};

            const auto &map = singlePixel
                ? renderingNodes->mousePickingIndirectDrawCommandBuffers
                : renderingNodes->multiNodeMousePickingIndirectDrawCommandBuffers;
            for (const auto &[criteria, indirectDrawCommandBuffer] : map) {
                if (resourceBindingState.pipeline != criteria.pipeline) {
                    cb.bindPipeline(vk::PipelineBindPoint::eGraphics, resourceBindingState.pipeline = criteria.pipeline);
                }

                if (resourceBindingState.primitiveTopology != criteria.primitiveTopology) {
                    cb.setPrimitiveTopologyEXT(resourceBindingState.primitiveTopology.emplace(criteria.primitiveTopology));
                }

                if (singlePixel && resourceBindingState.cullMode != criteria.cullMode) {
                    cb.setCullModeEXT(resourceBindingState.cullMode.emplace(criteria.cullMode));
                }

                if (criteria.indexType && resourceBindingState.indexType != *criteria.indexType) {
                    resourceBindingState.indexType.emplace(*criteria.indexType);
                    cb.bindIndexBuffer(
                        gltfAsset->assetExtended->combinedIndexBuffer,
                        gltfAsset->assetExtended->combinedIndexBuffer.getIndexOffsetAndSize(*resourceBindingState.indexType).first,
                        *resourceBindingState.indexType);
                }
                indirectDrawCommandBuffer.recordDrawCommand(cb, sharedData.gpu.supportDrawIndirectCount);
            }

            cb.endRenderingKHR();

            // The collected node indices in mousePickingResultBuffer must be visible to the host.
            cb.pipelineBarrier(
                vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eHost,
                {}, vk::MemoryBarrier {
                    vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eHostRead,
                }, {}, {});
        }
    }
}

bool vk_gltf_viewer::vulkan::Frame::recordJumpFloodComputeCommands(
    vk::CommandBuffer cb,
    const vku::Image &image,
    vku::DescriptorSet<JumpFloodComputePipeline::DescriptorSetLayout> descriptorSet,
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
                image, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, static_cast<std::uint32_t>(renderer->cameras.size()) },
            },
            vk::ImageMemoryBarrier2 {
                {}, {},
                vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite,
                {}, vk::ImageLayout::eGeneral,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                image, { vk::ImageAspectFlagBits::eColor, 0, 1, static_cast<std::uint32_t>(renderer->cameras.size()), vk::RemainingArrayLayers },
            }
        }),
    });

    // Compute jump flood and get the last execution direction.
    return sharedData.jumpFloodComputePipeline.compute(cb, descriptorSet, initialSampleOffset, vku::toExtent2D(image.extent), renderer->cameras.size());
}

void vk_gltf_viewer::vulkan::Frame::recordSceneOpaqueMeshDrawCommands(vk::CommandBuffer cb) const {
    assert(renderingNodes && "No nodes have to be rendered.");

    struct {
        vk::Pipeline pipeline{};
        std::optional<vk::PrimitiveTopology> primitiveTopology{};
        std::optional<std::uint32_t> stencilReference{};
        std::optional<vk::CullModeFlagBits> cullMode{};
        std::optional<vk::IndexType> indexType;

        // (Unlit)PrimitiveRenderPipeline variants have the same pipeline layout, therefore the descriptor sets should
        // be bound only once.
        bool descriptorBound = false;
    } resourceBindingState{};

    // Render alphaMode=Opaque | Mask meshes.
    for (const auto &[criteria, indirectDrawCommandBuffer] : ranges::make_subrange(renderingNodes->indirectDrawCommandBuffers.equal_range(0U))) {
        if (resourceBindingState.pipeline != criteria.pipeline) {
            cb.bindPipeline(vk::PipelineBindPoint::eGraphics, resourceBindingState.pipeline = criteria.pipeline);
        }
        if (!resourceBindingState.descriptorBound) {
            cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.primitivePipelineLayout, 0,
                { rendererSet, sharedData.imageBasedLightingDescriptorSet, assetDescriptorSet }, {});
            resourceBindingState.descriptorBound = true;
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
                gltfAsset->assetExtended->combinedIndexBuffer,
                gltfAsset->assetExtended->combinedIndexBuffer.getIndexOffsetAndSize(*resourceBindingState.indexType).first,
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

        // (Unlit)PrimitiveRenderPipeline variants have the same pipeline layout, therefore the descriptor sets should
        // be bound only once.
        bool descriptorBound = false;
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
                { rendererSet, sharedData.imageBasedLightingDescriptorSet, assetDescriptorSet }, {});
            resourceBindingState.descriptorBound = true;
        }

        if (criteria.indexType && resourceBindingState.indexType != *criteria.indexType) {
            resourceBindingState.indexType.emplace(*criteria.indexType);
            cb.bindIndexBuffer(
                gltfAsset->assetExtended->combinedIndexBuffer,
                gltfAsset->assetExtended->combinedIndexBuffer.getIndexOffsetAndSize(*resourceBindingState.indexType).first,
                *resourceBindingState.indexType);
        }
        indirectDrawCommandBuffer.recordDrawCommand(cb, sharedData.gpu.supportDrawIndirectCount);
        hasBlendMesh = true;
    }

    return hasBlendMesh;
}

void vk_gltf_viewer::vulkan::Frame::recordSkyboxDrawCommands(vk::CommandBuffer cb) const {
    cb.bindPipeline(vk::PipelineBindPoint::eGraphics, *viewport->shared->skyboxRenderPipeline);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.skyboxPipelineLayout, 0, { rendererSet, sharedData.skyboxDescriptorSet }, {});
    cb.bindIndexBuffer(sharedData.cubeIndexBuffer, 0, vk::IndexType::eUint16);
    cb.drawIndexed(36, 1, 0, 0, 0);
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
            viewport->hoveringNodeOutlineJumpFloodResources.image,
            { vk::ImageAspectFlagBits::eColor, 0, 1, static_cast<std::uint32_t>(*hoveringNodeJumpFloodForward ? renderer->cameras.size() : 0U), static_cast<std::uint32_t>(renderer->cameras.size()) },
        });
    }
    if (selectedNodeJumpFloodForward) {
        memoryBarriers.push_back({
            {}, vk::AccessFlagBits::eShaderRead,
            vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
            viewport->selectedNodeOutlineJumpFloodResources.image,
            { vk::ImageAspectFlagBits::eColor, 0, 1, static_cast<std::uint32_t>(*selectedNodeJumpFloodForward ? renderer->cameras.size() : 0U), static_cast<std::uint32_t>(renderer->cameras.size()) },
        });
    }
    if (!memoryBarriers.empty()) {
        cb.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eFragmentShader,
            {}, {}, {}, memoryBarriers);
    }

    // Set viewport and scissor.
    cb.setViewport(0, vku::toViewport(viewport->extent));
    cb.setScissor(0, vk::Rect2D { { 0, 0 }, viewport->extent });

    cb.beginRenderingKHR(vk::RenderingInfo {
        {},
        { { 0, 0 }, viewport->extent },
        1,
        math::bit::ones(renderer->cameras.size()),
        vku::unsafeProxy(vk::RenderingAttachmentInfo {
            *viewport->sceneAttachmentGroup.colorImageView,
            vk::ImageLayout::eColorAttachmentOptimal,
            {}, {}, {},
            vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore,
        }),
    });

    // Draw hovering/selected node outline if exists.
    if (selectedNodes) {
        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, *viewport->shared->outlineRenderPipeline);
        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.outlinePipelineLayout, 0,
            selectedNodeOutlineSet, {});
        cb.pushConstants<pl::Outline::PushConstant>(
            *sharedData.outlinePipelineLayout, vk::ShaderStageFlagBits::eFragment,
            0, pl::Outline::PushConstant {
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
            cb.bindPipeline(vk::PipelineBindPoint::eGraphics, *viewport->shared->outlineRenderPipeline);
        }

        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.outlinePipelineLayout, 0,
            hoveringNodeOutlineSet, {});
        cb.pushConstants<pl::Outline::PushConstant>(
            *sharedData.outlinePipelineLayout, vk::ShaderStageFlagBits::eFragment,
            0, pl::Outline::PushConstant {
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