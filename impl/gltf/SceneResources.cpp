module;

#include <fastgltf/core.hpp>

module vk_gltf_viewer;
import :gltf.SceneResources;

import std;
import :helpers.ranges;

using namespace std::views;

vk_gltf_viewer::gltf::SceneResources::SceneResources(
    const AssetResources &assetResources,
    const fastgltf::Scene &scene,
    const vulkan::Gpu &gpu
) : assetResources { assetResources },
    scene { scene },
    nodeTransformBuffer { createNodeTransformBuffer(gpu.allocator) },
    primitiveBuffer { createPrimitiveBuffer(gpu) } { }

auto vk_gltf_viewer::gltf::SceneResources::getParentNodeIndices() const -> std::vector<std::size_t> {
    const fastgltf::Asset &asset = assetResources.asset;

    // If node is root node, its parent index is itself.
    std::vector parentNodeIndices { std::from_range, std::views::iota(0UZ, asset.nodes.size()) };
    for (const auto &[i, node] : asset.nodes | ranges::views::enumerate) {
        for (std::size_t childIndex : node.children) {
            parentNodeIndices[childIndex] = i;
        }
    }
    return parentNodeIndices;
}

auto vk_gltf_viewer::gltf::SceneResources::createOrderedNodePrimitiveInfoPtrs() const -> decltype(orderedNodePrimitiveInfoPtrs) {
    const fastgltf::Asset &asset = assetResources.asset;

    // Collect glTF mesh primitives.
    std::vector<std::pair<std::uint32_t /* nodeIndex */, const AssetResources::PrimitiveInfo*>> nodePrimitiveInfoPtrs;
    for (std::stack dfs { std::from_range, scene.nodeIndices | reverse }; !dfs.empty(); ) {
        const std::size_t nodeIndex = dfs.top();

        const fastgltf::Node &node = asset.nodes[nodeIndex];
        if (node.meshIndex) {
            const fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
            for (const fastgltf::Primitive &primitive : mesh.primitives){
                const AssetResources::PrimitiveInfo &primitiveInfo = assetResources.primitiveInfos.at(&primitive);
                nodePrimitiveInfoPtrs.emplace_back(nodeIndex, &primitiveInfo);
            }
        }

        dfs.pop();
        dfs.push_range(node.children | reverse);
    }

    return nodePrimitiveInfoPtrs;
}

auto vk_gltf_viewer::gltf::SceneResources::createNodeTransformBuffer(
    vma::Allocator allocator
) const -> decltype(nodeTransformBuffer) {
    std::vector<glm::mat4> nodeTransforms(assetResources.asset.nodes.size());
    const auto calculateNodeTransformsRecursive = [&](this const auto &self, std::size_t nodeIndex, glm::mat4 transform) -> void {
        const fastgltf::Node &node = assetResources.asset.nodes[nodeIndex];
        transform *= visit(fastgltf::visitor {
            [](const fastgltf::TRS &trs) {
                return translate(glm::mat4 { 1.f }, glm::make_vec3(trs.translation.data()))
                    * mat4_cast(glm::make_quat(trs.rotation.data()))
                    * scale(glm::mat4 { 1.f }, glm::make_vec3(trs.scale.data()));
            },
            [](const fastgltf::Node::TransformMatrix &mat) {
                return glm::make_mat4(mat.data());
            },
        }, node.transform);
        nodeTransforms[nodeIndex] = transform;

        for (std::size_t childIndex : node.children) {
            self(childIndex, transform);
        }
    };
    for (std::size_t nodeIndex : scene.nodeIndices) {
        calculateNodeTransformsRecursive(nodeIndex, { 1.f });
    }

    return { allocator, std::from_range, nodeTransforms, vk::BufferUsageFlagBits::eStorageBuffer, vku::allocation::hostRead };
}

auto vk_gltf_viewer::gltf::SceneResources::createPrimitiveBuffer(
    const vulkan::Gpu &gpu
) -> decltype(primitiveBuffer) {
    return {
        gpu.allocator,
        std::from_range, orderedNodePrimitiveInfoPtrs | transform([](const auto &pair){
            const auto [nodeIndex, pPrimitiveInfo] = pair;

            // If normal and tangent not presented (nullopt), it will use a faceted mesh renderer, and they will does not
            // dereference those buffers. Therefore, it is okay to pass nullptr into shaders
            const auto normalInfo = pPrimitiveInfo->normalInfo.value_or(AssetResources::PrimitiveInfo::AttributeBufferInfo{});
            const auto tangentInfo = pPrimitiveInfo->tangentInfo.value_or(AssetResources::PrimitiveInfo::AttributeBufferInfo{});

            return GpuPrimitive {
                .pPositionBuffer = pPrimitiveInfo->positionInfo.address,
                .pNormalBuffer = normalInfo.address,
                .pTangentBuffer = tangentInfo.address,
                .pTexcoordBufferPtrsBuffer = ranges::value_or(pPrimitiveInfo->indexedAttributeMappingInfos, AssetResources::IndexedAttribute::Texcoord, {}).pBufferPtrBuffer,
                .pColorBufferPtrsBuffer = ranges::value_or(pPrimitiveInfo->indexedAttributeMappingInfos, AssetResources::IndexedAttribute::Color, {}).pBufferPtrBuffer,
                .positionByteStride = pPrimitiveInfo->positionInfo.byteStride,
                .normalByteStride = normalInfo.byteStride,
                .tangentByteStride = tangentInfo.byteStride,
                .pTexcoordByteStridesBuffer = ranges::value_or(pPrimitiveInfo->indexedAttributeMappingInfos, AssetResources::IndexedAttribute::Texcoord, {}).pByteStridesBuffer,
                .pColorByteStridesBuffer = ranges::value_or(pPrimitiveInfo->indexedAttributeMappingInfos, AssetResources::IndexedAttribute::Color, {}).pByteStridesBuffer,
                .nodeIndex = nodeIndex,
                .materialIndex = pPrimitiveInfo->materialIndex.transform([](std::uint32_t index) { return static_cast<std::int32_t>(index); }).value_or(-1 /*will use fallback material*/),
            };
        }),
        vk::BufferUsageFlagBits::eStorageBuffer,
    };
}