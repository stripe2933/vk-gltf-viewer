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
) : pAsset { &asset },
    nodeWorldTransformBuffer { createNodeWorldTransformBuffer(scene, gpu.allocator) } { }

vku::MappedBuffer vk_gltf_viewer::gltf::AssetSceneGpuBuffers::createNodeWorldTransformBuffer(
    const fastgltf::Scene &scene,
    vma::Allocator allocator
) const {
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

    return { allocator, std::from_range, nodeWorldTransforms, vk::BufferUsageFlagBits::eStorageBuffer, vku::allocation::hostRead };
}