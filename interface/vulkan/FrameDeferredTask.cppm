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

        /**
         * @brief Reset all asset-related tasks.
         *
         * Including:
         * - <tt>updateNodeWorldTransform</tt>
         * - <tt>updateNodeWorldTransformHierarchical</tt>
         * - <tt>updateNodeWorldTransformScene</tt>
         * - <tt>updateNodeTargetWeights</tt>
         */
        void resetAssetRelated();

        void setViewportExtent(const vk::Extent2D &extent);
        void updateViewCount();

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

        std::optional<vk::Extent2D> viewportExtent;
        bool needUpdateViewCount = false;

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
    if (viewportExtent) {
        frame.setViewportExtent(*viewportExtent);
        viewportExtent.reset();

        // Frame::setViewportExtent(const vk::Extent2D&) will also re-construct the view count related resources.
        needUpdateViewCount = false;
    }
    else if (needUpdateViewCount) {
        frame.updateViewCount();
        needUpdateViewCount = false;
    }

    visit(multilambda {
        [](std::monostate) noexcept { },
        [&](UpdateNodeWorldTransform &task) {
            // ----- Hierarchical Update -----

            // Remove duplicates.
            std::ranges::sort(task.hierarchicalNodeIndices);
            const auto [begin, end] = std::ranges::unique(task.hierarchicalNodeIndices);
            task.hierarchicalNodeIndices.erase(begin, end);

            // Erase duplicated element from nodeIndices to prevent updating the same node multiple times.
            for (std::size_t nodeIndex : task.hierarchicalNodeIndices) {
                task.nodeIndices.erase(nodeIndex);
            }

            frame.gltfAsset->assetExtended->sceneHierarchy.pruneDescendantNodesInPlace(task.hierarchicalNodeIndices);
            for (std::size_t nodeIndex : task.hierarchicalNodeIndices) {
                frame.gltfAsset->updateNodeWorldTransformHierarchical(nodeIndex);
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

void vk_gltf_viewer::vulkan::FrameDeferredTask::resetAssetRelated() {
    nodeWorldTransformUpdateTask.emplace<std::monostate>();
    nodeTargetWeightUpdateTask.clear();
}

void vk_gltf_viewer::vulkan::FrameDeferredTask::setViewportExtent(const vk::Extent2D &extent) {
    viewportExtent.emplace(extent);
}

void vk_gltf_viewer::vulkan::FrameDeferredTask::updateViewCount() {
    needUpdateViewCount = true;
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