module;

#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :gltf.AssetSceneGpuBuffers;

import std;
import :helpers.fastgltf;
import :helpers.ranges;

#define FWD(...) static_cast<decltype(__VA_ARGS__) &&>(__VA_ARGS__)
#define LIFT(...) [](auto &&x) { return __VA_ARGS__(FWD(x)); }

vk_gltf_viewer::gltf::AssetSceneGpuBuffers::AssetSceneGpuBuffers(
    const fastgltf::Asset &asset [[clang::lifetimebound]],
    const std::unordered_map<const fastgltf::Primitive*, AssetPrimitiveInfo> &primitiveInfos,
    const fastgltf::Scene &scene,
    const vulkan::Gpu &gpu
) : orderedNodePrimitiveInfoPtrs { createOrderedNodePrimitiveInfoPtrs(asset, primitiveInfos, scene) },
    nodeWorldTransformBuffer { createNodeWorldTransformBuffer(asset, scene, gpu.allocator) },
    primitiveBuffer { createPrimitiveBuffer(gpu) } { }

auto vk_gltf_viewer::gltf::AssetSceneGpuBuffers::createOrderedNodePrimitiveInfoPtrs(
    const fastgltf::Asset &asset,
    const std::unordered_map<const fastgltf::Primitive*, AssetPrimitiveInfo> &primitiveInfos,
    const fastgltf::Scene &scene
) const -> std::vector<std::pair<std::size_t, const AssetPrimitiveInfo*>> {
    std::vector<std::pair<std::size_t /* nodeIndex */, const AssetPrimitiveInfo*>> nodePrimitiveInfoPtrs;

    // Traverse the scene nodes and collect the glTF mesh primitives with their node indices.
    // FIXME: due to the Clang 18's explicit object parameter bug, const fastgltf::Asset& is passed (but it is unnecessary). Remove the parameter when fixed.
    const auto traverseMeshPrimitivesRecursive = [&](this const auto &self, const fastgltf::Asset &asset, std::size_t nodeIndex) -> void {
        const fastgltf::Node &node = asset.nodes[nodeIndex];
        if (node.meshIndex) {
            const fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
            for (const fastgltf::Primitive &primitive : mesh.primitives) {
                const AssetPrimitiveInfo &primitiveInfo = primitiveInfos.at(&primitive);
                nodePrimitiveInfoPtrs.emplace_back(nodeIndex, &primitiveInfo);
            }
        }

        for (std::size_t childNodeIndex : node.children) {
            self(asset, childNodeIndex);
        }
    };

    for (std::size_t nodeIndex : scene.nodeIndices) {
        traverseMeshPrimitivesRecursive(asset, nodeIndex);
    }

    return nodePrimitiveInfoPtrs;
}

vku::MappedBuffer vk_gltf_viewer::gltf::AssetSceneGpuBuffers::createNodeWorldTransformBuffer(
    const fastgltf::Asset &asset,
    const fastgltf::Scene &scene,
    vma::Allocator allocator
) const {
    std::vector<glm::mat4> nodeWorldTransforms(asset.nodes.size());

    // Traverse the scene nodes and calculate the world transform of each node (by multiplying their local transform to
    // their parent's world transform).
    const auto calculateNodeWorldTransformsRecursive
        // TODO: since the multiplication of parent node's world transform and node's local transform will be assigned
        //  to nodeWorldTransforms[nodeIndex], parentNodeWorldTransform parameter should be const-ref qualified. However,
        //  Clang ≤ 18 does not accept this signature (according to explicit object parameter bug). Change when it fixed.
        = [&](this const auto &self, std::size_t nodeIndex, glm::mat4 parentNodeWorldTransform = { 1.f }) -> void {
            const fastgltf::Node &node = asset.nodes[nodeIndex];
            parentNodeWorldTransform *= visit(LIFT(fastgltf::toMatrix), node.transform);
            nodeWorldTransforms[nodeIndex] = parentNodeWorldTransform;

            for (std::size_t childNodeIndex : node.children) {
                self(childNodeIndex, parentNodeWorldTransform);
            }
        };
    for (std::size_t nodeIndex : scene.nodeIndices) {
        calculateNodeWorldTransformsRecursive(nodeIndex);
    }

    return { allocator, std::from_range, nodeWorldTransforms, vk::BufferUsageFlagBits::eStorageBuffer, vku::allocation::hostRead };
}

vku::AllocatedBuffer vk_gltf_viewer::gltf::AssetSceneGpuBuffers::createPrimitiveBuffer(const vulkan::Gpu &gpu) const {
    vku::MappedBuffer stagingBuffer{
        gpu.allocator,
        std::from_range,
        orderedNodePrimitiveInfoPtrs
            | ranges::views::decompose_transform([](std::size_t nodeIndex, const AssetPrimitiveInfo* pPrimitiveInfo) {
            // If normal and tangent not presented (nullopt), it will use a faceted mesh renderer, and they will does not
            // dereference those buffers. Therefore, it is okay to pass nullptr into shaders
            const auto normalInfo = pPrimitiveInfo->normalInfo.value_or(AssetPrimitiveInfo::AttributeBufferInfo{});
            const auto tangentInfo = pPrimitiveInfo->tangentInfo.value_or(AssetPrimitiveInfo::AttributeBufferInfo{});

            return GpuPrimitive {
                .pPositionBuffer = pPrimitiveInfo->positionInfo.address,
                .pNormalBuffer = normalInfo.address,
                .pTangentBuffer = tangentInfo.address,
                .pTexcoordAttributeMappingInfoBuffer = pPrimitiveInfo->texcoordsInfo.pMappingBuffer,
                .pColorAttributeMappingInfoBuffer = 0ULL, // TODO: implement color attribute mapping.
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
        gpu.isUmaDevice ? vk::BufferUsageFlagBits::eStorageBuffer : vk::BufferUsageFlagBits::eTransferSrc,
    };

    if (gpu.isUmaDevice) {
        return std::move(stagingBuffer).unmap();
    }

    vku::AllocatedBuffer dstBuffer{ gpu.allocator, vk::BufferCreateInfo {
        {},
        stagingBuffer.size,
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    } };

    const vk::raii::CommandPool transferCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.transfer } };
    const vk::raii::Fence fence { gpu.device, vk::FenceCreateInfo{} };
    vku::executeSingleCommand(*gpu.device, *transferCommandPool, gpu.queues.transfer, [&](vk::CommandBuffer cb) {
        cb.copyBuffer(
            stagingBuffer, dstBuffer,
            vk::BufferCopy { 0, 0, dstBuffer.size });
    }, *fence);

    // Wait for the copy operation to complete.
    if (vk::Result result = gpu.device.waitForFences(*fence, true, ~0ULL); result != vk::Result::eSuccess) {
        throw std::runtime_error { std::format("Failed to generate scene primitive infos: {}", to_string(result)) };
    }

    return dstBuffer;
}