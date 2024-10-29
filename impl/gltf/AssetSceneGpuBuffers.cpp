module vk_gltf_viewer;
import :gltf.AssetSceneGpuBuffers;

import std;
import :helpers.fastgltf;
import :helpers.ranges;

#define FWD(...) static_cast<decltype(__VA_ARGS__) &&>(__VA_ARGS__)
#define LIFT(...) [](auto &&x) { return __VA_ARGS__(FWD(x)); }

vk_gltf_viewer::gltf::AssetSceneGpuBuffers::AssetSceneGpuBuffers(
    const fastgltf::Asset &asset [[clang::lifetimebound]],
    const AssetGpuBuffers &assetGpuBuffers,
    const fastgltf::Scene &scene,
    const vulkan::Gpu &gpu
) : pAsset { &asset },
    pGpu { &gpu },
    pAssetGpuBuffers { &assetGpuBuffers },
    orderedNodePrimitiveInfoPtrs { createOrderedNodePrimitiveInfoPtrs(scene) },
    nodeWorldTransformBuffer { createNodeWorldTransformBuffer(scene) } { }

auto vk_gltf_viewer::gltf::AssetSceneGpuBuffers::createOrderedNodePrimitiveInfoPtrs(
    const fastgltf::Scene &scene
) const -> std::vector<std::pair<std::size_t, const AssetGpuBuffers::PrimitiveInfo*>> {
    std::vector<std::pair<std::size_t /* nodeIndex */, const AssetGpuBuffers::PrimitiveInfo*>> nodePrimitiveInfoPtrs;

    // Traverse the scene nodes and collect the glTF mesh primitives with their node indices.
    const auto traverseMeshPrimitivesRecursive = [&](this auto self, std::size_t nodeIndex) -> void {
        const fastgltf::Node &node = pAsset->nodes[nodeIndex];
        if (node.meshIndex) {
            const fastgltf::Mesh &mesh = pAsset->meshes[*node.meshIndex];
            for (const fastgltf::Primitive &primitive : mesh.primitives) {
                const AssetGpuBuffers::PrimitiveInfo &primitiveInfo = pAssetGpuBuffers->primitiveInfos.at(&primitive);
                nodePrimitiveInfoPtrs.emplace_back(nodeIndex, &primitiveInfo);
            }
        }

        for (std::size_t childNodeIndex : node.children) {
            self(childNodeIndex);
        }
    };

    for (std::size_t nodeIndex : scene.nodeIndices) {
        traverseMeshPrimitivesRecursive(nodeIndex);
    }

    return nodePrimitiveInfoPtrs;
}

auto vk_gltf_viewer::gltf::AssetSceneGpuBuffers::createNodeWorldTransformBuffer(const fastgltf::Scene &scene) const -> vku::MappedBuffer {
    std::vector<glm::mat4> nodeWorldTransforms(pAsset->nodes.size());

    // Traverse the scene nodes and calculate the world transform of each node (by multiplying their local transform to
    // their parent's world transform).
    const auto calculateNodeWorldTransformsRecursive
        // TODO: since the multiplication of parent node's world transform and node's local transform will be assigned
        //  to nodeWorldTransforms[nodeIndex], parentNodeWorldTransform parameter should be const-ref qualified. However,
        //  Clang ≤ 18 does not accept this signature (according to explicit object parameter bug). Change when it fixed.
        = [&](this const auto &self, std::size_t nodeIndex, glm::mat4 parentNodeWorldTransform = { 1.f }) -> void {
            const fastgltf::Node &node = pAsset->nodes[nodeIndex];
            parentNodeWorldTransform *= visit(LIFT(fastgltf::toMatrix), node.transform);
            nodeWorldTransforms[nodeIndex] = parentNodeWorldTransform;

            for (std::size_t childNodeIndex : node.children) {
                self(childNodeIndex, parentNodeWorldTransform);
            }
        };
    for (std::size_t nodeIndex : scene.nodeIndices) {
        calculateNodeWorldTransformsRecursive(nodeIndex);
    }

    return { pGpu->allocator, std::from_range, nodeWorldTransforms, vk::BufferUsageFlagBits::eStorageBuffer, vku::allocation::hostRead };
}

auto vk_gltf_viewer::gltf::AssetSceneGpuBuffers::createPrimitiveBuffer() const -> vku::AllocatedBuffer {
    return vku::MappedBuffer {
        pGpu->allocator,
        std::from_range, orderedNodePrimitiveInfoPtrs
            | ranges::views::decompose_transform([](std::size_t nodeIndex, const AssetGpuBuffers::PrimitiveInfo *pPrimitiveInfo) {
                // If normal and tangent not presented (nullopt), it will use a faceted mesh renderer, and they will does not
                // dereference those buffers. Therefore, it is okay to pass nullptr into shaders
                const auto normalInfo = pPrimitiveInfo->normalInfo.value_or(AssetGpuBuffers::PrimitiveInfo::AttributeBufferInfo{});
                const auto tangentInfo = pPrimitiveInfo->tangentInfo.value_or(AssetGpuBuffers::PrimitiveInfo::AttributeBufferInfo{});

                return GpuPrimitive {
                    .pPositionBuffer = pPrimitiveInfo->positionInfo.address,
                    .pNormalBuffer = normalInfo.address,
                    .pTangentBuffer = tangentInfo.address,
                    .pTexcoordAttributeMappingInfoBuffer
                        = ranges::value_or(
                            pPrimitiveInfo->indexedAttributeMappingInfos,
                            AssetGpuBuffers::IndexedAttribute::Texcoord, {}).pMappingBuffer,
                    .pColorAttributeMappingInfoBuffer
                        = ranges::value_or(
                            pPrimitiveInfo->indexedAttributeMappingInfos,
                            AssetGpuBuffers::IndexedAttribute::Color, {}).pMappingBuffer,
                    .positionByteStride = pPrimitiveInfo->positionInfo.byteStride,
                    .normalByteStride = normalInfo.byteStride,
                    .tangentByteStride = tangentInfo.byteStride,
                    .nodeIndex = static_cast<std::uint32_t>(nodeIndex),
                    .materialIndex
                        = pPrimitiveInfo->materialIndex.transform([](std::size_t index) {
                            return static_cast<std::int32_t>(index);
                        })
                        .value_or(-1 /* will use the fallback material */),
                };
            }),
        vk::BufferUsageFlagBits::eStorageBuffer,
    }.unmap();
}