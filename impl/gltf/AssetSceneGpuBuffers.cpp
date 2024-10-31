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
    const fastgltf::Scene &scene [[clang::lifetimebound]],
    const vulkan::Gpu &gpu [[clang::lifetimebound]]
) : orderedNodePrimitives { createOrderedNodePrimitives(asset, scene) },
    nodeWorldTransformBuffer { createNodeWorldTransformBuffer(asset, scene, gpu.allocator) } { }

std::vector<std::pair<std::size_t, const fastgltf::Primitive*>> vk_gltf_viewer::gltf::AssetSceneGpuBuffers::createOrderedNodePrimitives(
    const fastgltf::Asset &asset,
    const fastgltf::Scene &scene
) const {
    std::vector<std::pair<std::size_t, const fastgltf::Primitive*>> result;

    // Traverse the scene nodes and collect the glTF mesh primitives with their node indices.
    // FIXME: due to the Clang 18's explicit object parameter bug, const fastgltf::Asset& is passed (but it is unnecessary). Remove the parameter when fixed.
    const auto traverseMeshPrimitivesRecursive = [&](this const auto &self, const fastgltf::Asset &asset, std::size_t nodeIndex) -> void {
        const fastgltf::Node &node = asset.nodes[nodeIndex];
        if (node.meshIndex) {
            const fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
            for (const fastgltf::Primitive &primitive : mesh.primitives) {
                result.emplace_back(nodeIndex, &primitive);
            }
        }

        for (std::size_t childNodeIndex : node.children) {
            self(asset, childNodeIndex);
        }
    };

    for (std::size_t nodeIndex : scene.nodeIndices) {
        traverseMeshPrimitivesRecursive(asset, nodeIndex);
    }

    return result;
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