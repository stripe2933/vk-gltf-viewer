module;

#include <algorithm>
#include <ranges>
#include <tuple>
#include <vector>

#include <fastgltf/core.hpp>

module vk_gltf_viewer;
import :gltf.SceneResources;
import :helpers.ranges;

using namespace std::views;

vk_gltf_viewer::gltf::SceneResources::SceneResources(
    const AssetResources &assetResources,
    const fastgltf::Scene &scene,
    const vulkan::Gpu &gpu
) : assetResources { assetResources },
    scene { scene },
    nodeTransformBuffer { createNodeTransformBuffer(gpu) },
    primitiveBuffer { createPrimitiveBuffer(gpu) } { }

auto vk_gltf_viewer::gltf::SceneResources::createNodeTransformBuffer(
    const vulkan::Gpu &gpu
) const -> decltype(nodeTransformBuffer) {
    std::vector<glm::mat4> nodeTransforms(assetResources.asset.nodes.size());
    const auto calculateNodeTransformsRecursive
        = [&](this const auto &self, std::size_t nodeIndex, glm::mat4 transform) -> void {
            const fastgltf::Node &node = assetResources.asset.nodes[nodeIndex];
            transform *= visit(fastgltf::visitor {
                [](const fastgltf::TRS &trs) {
                    return glm::gtc::translate(glm::mat4 { 1.f }, glm::gtc::make_vec3(trs.translation.data()))
                        * glm::gtc::mat4_cast(glm::gtc::make_quat(trs.rotation.data()))
                        * glm::gtc::scale(glm::mat4 { 1.f }, glm::gtc::make_vec3(trs.scale.data()));
                },
                [](const fastgltf::Node::TransformMatrix &mat) {
                    return glm::gtc::make_mat4(mat.data());
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

    return {
        gpu.allocator,
        std::from_range, nodeTransforms,
        vk::BufferUsageFlagBits::eStorageBuffer,
        vma::AllocationCreateInfo {
            vma::AllocationCreateFlagBits::eHostAccessRandom | vma::AllocationCreateFlagBits::eMapped,
            vma::MemoryUsage::eAuto,
        },
    };
}

auto vk_gltf_viewer::gltf::SceneResources::createPrimitiveBuffer(
    const vulkan::Gpu &gpu
) -> decltype(primitiveBuffer) {
    const fastgltf::Asset &asset = assetResources.asset;

    // Collect glTF mesh primitives.
    std::vector<const AssetResources::PrimitiveInfo*> primitiveInfoPtrs;
    for (std::stack dfs { std::from_range, asset.scenes[asset.defaultScene.value_or(0)].nodeIndices | reverse }; !dfs.empty(); ) {
        const std::size_t nodeIndex = dfs.top();
        const fastgltf::Node &node = asset.nodes[nodeIndex];
        if (node.meshIndex) {
            const fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
            primitiveInfoPtrs.append_range(
                mesh.primitives | transform([&](const fastgltf::Primitive &primitive) {
                    const AssetResources::PrimitiveInfo &primitiveInfo = assetResources.primitiveInfos.at(&primitive);
                    return &primitiveInfo;
                }));
        }
        dfs.pop();
        dfs.push_range(node.children | reverse);
    }

    // Sort primitive by index type.
    constexpr auto projection = [](const AssetResources::PrimitiveInfo *pPrimitiveInfo) {
        return pPrimitiveInfo->indexInfo.transform([](const auto &info) { return info.type; });
    };
    std::ranges::sort(primitiveInfoPtrs, {}, projection);

    const auto primitiveInfos
        = primitiveInfoPtrs
        | transform([](const AssetResources::PrimitiveInfo *pPrimitiveInfo){
            return GpuPrimitive {
                .pPositionBuffer = pPrimitiveInfo->positionInfo.address,
                .pNormalBuffer = pPrimitiveInfo->normalInfo.value().address,
                .pTangentBuffer = pPrimitiveInfo->tangentInfo.value().address,
                .pTexcoordBufferPtrsBuffer = ranges::value_or(
                    pPrimitiveInfo->indexedAttributeMappingInfos,
                    AssetResources::IndexedAttribute::Texcoord, {}).pBufferPtrBuffer,
                .pColorBufferPtrsBuffer = ranges::value_or(
                    pPrimitiveInfo->indexedAttributeMappingInfos,
                    AssetResources::IndexedAttribute::Color, {}).pBufferPtrBuffer,
                .positionByteStride = pPrimitiveInfo->positionInfo.byteStride,
                .normalByteStride = pPrimitiveInfo->normalInfo.value().byteStride,
                .tangentByteStride = pPrimitiveInfo->tangentInfo.value().byteStride,
                .pTexcoordByteStridesBuffer = ranges::value_or(
                    pPrimitiveInfo->indexedAttributeMappingInfos,
                    AssetResources::IndexedAttribute::Texcoord, {}).pByteStridesBuffer,
                .pColorByteStridesBuffer = ranges::value_or(
                    pPrimitiveInfo->indexedAttributeMappingInfos,
                    AssetResources::IndexedAttribute::Color, {}).pByteStridesBuffer,
                .nodeIndex = pPrimitiveInfo->nodeIndex,
                .materialIndex = pPrimitiveInfo->materialIndex.value(),
            };
        });
    return { gpu.allocator, std::from_range, primitiveInfos, vk::BufferUsageFlagBits::eStorageBuffer };
}

/*auto vk_gltf_viewer::gltf::SceneResources::createIndirectDrawCommandBuffer(
    const AssetResources &assetResources,
    const vulkan::Gpu &gpu
) const -> decltype(indirectDrawCommandBuffers) {
    std::map<CommandSeparationCriteria, std::vector<vk::DrawIndexedIndirectCommand>> indexedDrawCommands;
    // std::map<CommandSeparationCriteria, std::vector<vk::DrawIndirectCommand>> nonIndexedDrawCommands;

    // Collect glTF mesh primitives.
    for (std::stack dfs { std::from_range, asset.scenes[asset.defaultScene.value_or(0)].nodeIndices | reverse }; !dfs.empty();) {
        const std::size_t nodeIndex = dfs.top();
        const fastgltf::Node &node = asset.nodes[nodeIndex];
        if (node.meshIndex) {
            for (const fastgltf::Primitive &primitive : asset.meshes[*node.meshIndex]){
                const AssetResources::PrimitiveInfo &primitiveInfo = assetResources.primitiveInfos.at(&primitive);

                const CommandSeparationCriteria criteria {
                    primitiveInfo.indexInfo.transform([](const auto &info) { return info.type; }),
                    asset.materials[primitive.materialIndex.value()].doubleSided,
                };

                if (criteria.indexType) {
                    const std::size_t indexByteSize = [=]() {
                        switch (*criteria.indexType) {
                            case vk::IndexType::eUint16: return sizeof(std::uint16_t);
                            case vk::IndexType::eUint32: return sizeof(std::uint32_t);
                            default: throw std::runtime_error{ "Unsupported index type: only Uint16 and Uint32 are supported" };
                        };
                    }();

                    indexedDrawCommands[criteria].emplace_back(
                        primitiveInfo.drawCount, 1,
                        static_cast<std::uint32_t>(primitiveInfo.indexInfo->offset / indexByteSize), 0, 0);
                }
                else {
                    assert(false && "Non-indexed primitive drawing not implemented");
                    // nonIndexedDrawCommands[criteria].emplace_back(primitiveInfo.drawCount, 1, 0, 0);
                }
            }
        }
        dfs.pop();
        dfs.push_range(node.children | reverse);
    }
}*/