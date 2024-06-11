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
    // Collect glTF mesh primitives.
    std::vector<const AssetResources::PrimitiveData*> primitiveDataPtrs;
    for (std::stack dfs { std::from_range, assetResources.asset.scenes[assetResources.asset.defaultScene.value_or(0)].nodeIndices | reverse }; !dfs.empty(); ) {
        const std::size_t nodeIndex = dfs.top();
        const fastgltf::Node &node = assetResources.asset.nodes[nodeIndex];
        if (node.meshIndex) {
            const fastgltf::Mesh &mesh = assetResources.asset.meshes[*node.meshIndex];
            primitiveDataPtrs.append_range(
                mesh.primitives | transform([&](const fastgltf::Primitive &primitive) {
                    const AssetResources::PrimitiveData &primitiveData = assetResources.primitiveData.at(&primitive);
                    return &primitiveData;
                }));
        }
        dfs.pop();
        dfs.push_range(node.children | reverse);
    }

    // Sort primitive by index type.
    constexpr auto projection = [](const AssetResources::PrimitiveData *pPrimitiveData) {
        return pPrimitiveData->indexInfo.transform([](const auto &info) { return info.type; });
    };
    std::ranges::sort(primitiveDataPtrs, {}, projection);

    const auto primitiveInfos
        = primitiveDataPtrs
        | transform([](const AssetResources::PrimitiveData *pPrimitiveData){
            return GpuPrimitive {
                .pPositionBuffer = pPrimitiveData->positionInfo.address,
                .pNormalBuffer = pPrimitiveData->normalInfo.value().address,
                .pTangentBuffer = pPrimitiveData->tangentInfo.value().address,
                .pTexcoordBufferPtrBuffer = pPrimitiveData->pTexcoordReferenceBuffer,
                .pColorBufferPtrBuffer = pPrimitiveData->pColorReferenceBuffer,
                .positionByteStride = pPrimitiveData->positionInfo.byteStride,
                .normalByteStride = pPrimitiveData->normalInfo.value().byteStride,
                .tangentByteStride = pPrimitiveData->tangentInfo.value().byteStride,
                .pTexcoordByteStrideBuffer = pPrimitiveData->pTexcoordByteStrideBuffer,
                .pColorByteStrideBuffer = pPrimitiveData->pColorByteStrideBuffer,
                .nodeIndex = pPrimitiveData->nodeIndex,
                .materialIndex = pPrimitiveData->materialIndex.value(),
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
                const AssetResources::PrimitiveData &primitiveData = assetResources.primitiveData.at(&primitive);

                const CommandSeparationCriteria criteria {
                    primitiveData.indexInfo.transform([](const auto &info) { return info.type; }),
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
                        primitiveData.drawCount, 1,
                        static_cast<std::uint32_t>(primitiveData.indexInfo->offset / indexByteSize), 0, 0);
                }
                else {
                    assert(false && "Non-indexed primitive drawing not implemented");
                    // nonIndexedDrawCommands[criteria].emplace_back(primitiveData.drawCount, 1, 0, 0);
                }
            }
        }
        dfs.pop();
        dfs.push_range(node.children | reverse);
    }
}*/