module;

#include <cassert>

export module vk_gltf_viewer.vulkan.FrameDeferredTask;

import std;

import vk_gltf_viewer.helpers.fastgltf;
import vk_gltf_viewer.helpers.functional;
export import vk_gltf_viewer.vulkan.Frame;

namespace vk_gltf_viewer::vulkan {
    export class FrameDeferredTask {
    public:
        void executeAndReset(Frame &frame);
        void reset();

        void updateNodeWorldTransform(std::size_t nodeIndex);
        void updateNodeWorldTransformHierarchical(std::size_t nodeIndex);
        void updateNodeWorldTransformScene(std::size_t sceneIndex);
        void updateNodeTargetWeights(std::size_t nodeIndex, std::size_t startIndex, std::size_t count);

    private:
        struct UpdateNodeWorldTransform {
            std::vector<std::size_t> hierarchicalNodeIndices;
            std::unordered_set<std::size_t> nodeIndices;
        };

        struct UpdateNodeWorldTransformScene {
            std::size_t sceneIndex;
        };

        std::variant<std::monostate, UpdateNodeWorldTransform, UpdateNodeWorldTransformScene> nodeWorldTransformUpdateTask;
        std::unordered_map<std::size_t /* node index */, std::pair<std::size_t /* weight start index */, std::size_t /* weight count */>> nodeTargetWeightUpdateTask;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [&](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

void vk_gltf_viewer::vulkan::FrameDeferredTask::executeAndReset(Frame &frame) {
    visit(multilambda {
        [](std::monostate) noexcept { },
        [&](UpdateNodeWorldTransform &task) {
            // ----- Hierarchical Update -----

            // Sort hierarchicalNodeIndices by their node level in the scene.
            std::ranges::sort(task.hierarchicalNodeIndices, {}, LIFT(frame.gltfAsset->assetExtended->sceneNodeLevels.operator[]));

            // Remove duplicates.
            const auto [begin, end] = std::ranges::unique(task.hierarchicalNodeIndices);
            task.hierarchicalNodeIndices.erase(begin, end);

            // Obtain nodes that are the top of the subtree of hierarchical nodes.
            std::vector visited(frame.gltfAsset->assetExtended->asset.nodes.size(), false);
            for (std::size_t nodeIndex : task.hierarchicalNodeIndices) {
                // If node is marked as visited, its world transform is already updated by its ancestor node. Skipping it.
                if (visited[nodeIndex]) continue;

                frame.gltfAsset->updateNodeWorldTransformHierarchical(nodeIndex);

                traverseNode(frame.gltfAsset->assetExtended->asset, nodeIndex, [&](std::size_t nodeIndex) noexcept {
                    assert(!visited[nodeIndex] && "This must be not visited");
                    visited[nodeIndex] = true;

                    // Erase duplicated element from nodeIndices to prevent updating the same node multiple times.
                    if (auto it = task.nodeIndices.find(nodeIndex); it != task.nodeIndices.end()) {
                        task.nodeIndices.erase(it);
                    }
                });
            }

            // ----- Individual Update -----

            for (std::size_t nodeIndex : task.nodeIndices) {
                frame.gltfAsset->updateNodeWorldTransform(nodeIndex);
            }
        },
        [&](const UpdateNodeWorldTransformScene &task) {
            frame.gltfAsset->updateNodeWorldTransformScene(task.sceneIndex);
        },
    }, nodeWorldTransformUpdateTask);
    nodeWorldTransformUpdateTask.emplace<std::monostate>();
    
    for (const auto &[nodeIndex, weightStartAndCount] : nodeTargetWeightUpdateTask) {
        const auto &[weightStart, weightCount] = weightStartAndCount;
        frame.gltfAsset->updateNodeTargetWeights(nodeIndex, weightStart, weightCount);
    }
    nodeTargetWeightUpdateTask.clear();
}

void vk_gltf_viewer::vulkan::FrameDeferredTask::reset() {
    nodeWorldTransformUpdateTask.emplace<std::monostate>();
    nodeTargetWeightUpdateTask.clear();
}

void vk_gltf_viewer::vulkan::FrameDeferredTask::updateNodeWorldTransform(std::size_t nodeIndex) {
    visit(multilambda {
        [&](std::monostate) noexcept {
            nodeWorldTransformUpdateTask.emplace<UpdateNodeWorldTransform>().nodeIndices.emplace(nodeIndex);
        },
        [&](UpdateNodeWorldTransform &task) {
            task.nodeIndices.emplace(nodeIndex);
        },
        [&] [[noreturn]] (const UpdateNodeWorldTransformScene&) {
            throw std::runtime_error { "Scene update is planned, updating individual node world transform is not allowed" };
        },
    }, nodeWorldTransformUpdateTask);
}

void vk_gltf_viewer::vulkan::FrameDeferredTask::updateNodeWorldTransformHierarchical(std::size_t nodeIndex) {
    visit(multilambda {
        [&](std::monostate) noexcept {
            nodeWorldTransformUpdateTask.emplace<UpdateNodeWorldTransform>().hierarchicalNodeIndices.push_back(nodeIndex);
        },
        [&](UpdateNodeWorldTransform &task) {
            task.hierarchicalNodeIndices.push_back(nodeIndex);
        },
        [] [[noreturn]] (const UpdateNodeWorldTransformScene&) {
            throw std::runtime_error { "Scene update is planned, updating individual node world transform is not allowed" };
        },
    }, nodeWorldTransformUpdateTask);
}

void vk_gltf_viewer::vulkan::FrameDeferredTask::updateNodeWorldTransformScene(std::size_t sceneIndex) {
    visit([&](const auto&) noexcept {
        // Regardless of what was in nodeWorldTransformUpdateTask, we are going to update the whole scene.
        nodeWorldTransformUpdateTask.emplace<UpdateNodeWorldTransformScene>().sceneIndex = sceneIndex;
    }, nodeWorldTransformUpdateTask);

}

void vk_gltf_viewer::vulkan::FrameDeferredTask::updateNodeTargetWeights(std::size_t nodeIndex, std::size_t startIndex, std::size_t count) {
    auto it = nodeTargetWeightUpdateTask.find(nodeIndex);
    if (it == nodeTargetWeightUpdateTask.end()) {
        nodeTargetWeightUpdateTask.try_emplace(nodeIndex, startIndex, count);
    }
    else {
        auto &[weightStart, weightCount] = it->second;
        weightStart = std::min(weightStart, startIndex);
        const std::size_t end = std::min(weightStart + weightCount, startIndex + count);
        weightCount = end - weightStart;
    }
}