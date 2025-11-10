module;

#include <cassert>

#include <lifetimebound.hpp>

export module vk_gltf_viewer.gltf.SceneHierarchy;

import std;
export import fastgltf;

import vk_gltf_viewer.helpers.fastgltf;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [&](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

namespace vk_gltf_viewer::gltf {
    export class SceneHierarchy {
    public:
        enum class VisibilityState : std::uint8_t {
            /// Every node that has a mesh is visible among the current and its descendants.
            AllVisible = 0,
            /// Every node that has a mesh is invisible among the current and its descendants.
            AllInvisible = 1,
            /// Every node that has a mesh is mixed state (some are visible, some are invisible) among the current and its descendants.
            Intermediate = 2,
            /// None of the current and its descendant nodes has a mesh.
            Indeterminate = 3,
        };

        SceneHierarchy(
            const fastgltf::Asset &asset LIFETIMEBOUND,
            std::size_t sceneIndex
        ) : asset { asset },
            parentNodeIndices(asset.nodes.size(), ~0UZ),
            nodeLevels(asset.nodes.size()),
            visibilities(asset.nodes.size(), true),
            states(asset.nodes.size()) {
            for (std::size_t nodeIndex : asset.scenes[sceneIndex].nodeIndices) {
                [&](this const auto &self, std::size_t nodeIndex, std::size_t level) -> void {
                    // parentNodeIndices
                    for (std::size_t childIndex : asset.nodes[nodeIndex].children) {
                        parentNodeIndices[childIndex] = nodeIndex;
                    }

                    // nodeLevels
                    nodeLevels[nodeIndex] = level;

                    for (std::size_t childNodeIndex : asset.nodes[nodeIndex].children) {
                        self(childNodeIndex, level + 1);
                    }

                    updateVisibilityState(nodeIndex);
                }(nodeIndex, 0);
            }
        }

        /**
         * @brief Get the parent node index of the node at \p nodeIndex.
         * @param nodeIndex Index of the node to get the parent of.
         * @return Parent node index if exists, <tt>std::nullopt</tt> if the node is a root node.
         * @pre The node at \p nodeIndex must be in the scene.
         */
        [[nodiscard]] std::optional<std::size_t> getParentNodeIndex(std::size_t nodeIndex) const noexcept {
            const std::size_t parentIndex = parentNodeIndices[nodeIndex];
            if (parentIndex != ~0UZ) {
                return parentIndex;
            }
            return std::nullopt;
        }

        /**
         * @brief Get the level (depth) of the node at \p nodeIndex in the scene hierarchy.
         * @param nodeIndex Index of the node to get the level of.
         * @return Level (depth) of the node.
         * @pre The node at \p nodeIndex must be in the scene.
         */
        [[nodiscard]] std::size_t getNodeLevel(std::size_t nodeIndex) const noexcept {
            return nodeLevels[nodeIndex];
        }

        /**
         * @brief Get the visibility of the node at \p nodeIndex.
         * @param nodeIndex Index of the node to get the visibility of.
         * @return Visibility of the node.
         */
        [[nodiscard]] bool getVisibility(std::size_t nodeIndex) const noexcept {
            return visibilities[nodeIndex];
        }

        /**
         * @brief Set the visibility of the node at \p nodeIndex to \p value.
         * @param nodeIndex Index of the node to set the visibility of.
         * @param value New visibility value.
         */
        void setVisibility(std::size_t nodeIndex, bool value) noexcept {
            assert(asset.get().nodes[nodeIndex].meshIndex);
            visibilities[nodeIndex] = value;

            // Update the visibility state of the node.
            updateVisibilityState(nodeIndex);

            // Update the visibility states of the ancestors.
            while ((nodeIndex = parentNodeIndices[nodeIndex]) != ~0UZ) {
                updateVisibilityState(nodeIndex);
            }
        }

        /**
         * @brief Flip the visibility of the node at \p nodeIndex, and return the new visibility.
         * @param nodeIndex Index of the node to flip the visibility of.
         * @return The new visibility of the node.
         */
        bool flipVisibility(std::size_t nodeIndex) noexcept {
            setVisibility(nodeIndex, !visibilities[nodeIndex]);
            return visibilities[nodeIndex];
        }

        /**
         * @brief Get the visibility state of the node at \p nodeIndex in O(1) time.
         * @param nodeIndex Index of the node to get the visibility state of.
         * @return Visibility state of the node.
         * @see VisibilityState
         */
        [[nodiscard]] VisibilityState getVisibilityState(std::size_t nodeIndex) const noexcept {
            return states[nodeIndex];
        }

        /**
         * @brief Update the visibility state of the node at \p nodeIndex, based on its own and its children's visibilities.
         * @param nodeIndex Index of the node to update the visibility state of.
         * @pre The node at \p nodeIndex must be in the scene.
         */
        void updateVisibilityState(std::size_t nodeIndex) {
            VisibilityState &result = states[nodeIndex];

            const fastgltf::Node &node = asset.get().nodes[nodeIndex];
            if (node.meshIndex) {
                result = visibilities[nodeIndex] ? VisibilityState::AllVisible : VisibilityState::AllInvisible;
            }
            else {
                result = VisibilityState::Indeterminate;
            }

            for (std::size_t childNodeIndex : node.children) {
                const VisibilityState childVisibilityState = states[childNodeIndex];

                if (childVisibilityState == VisibilityState::Indeterminate) {
                    // Child node is meshless, doesn't affect to the state.
                }
                else if (childVisibilityState == VisibilityState::AllVisible &&
                         (result == VisibilityState::AllVisible || result == VisibilityState::Indeterminate)) {
                    result = VisibilityState::AllVisible;
                }
                else if (childVisibilityState == VisibilityState::AllInvisible &&
                         (result == VisibilityState::AllInvisible || result == VisibilityState::Indeterminate)) {
                    result = VisibilityState::AllInvisible;
                }
                else {
                    // Child node is either in intermediate state, or visibility is different from the current.
                    result = VisibilityState::Intermediate;
                }
            }
        }

        /**
         * @brief Given a list of node indices, leave only the nodes that are not descendants of other nodes in the list,
         * and make them ordered by their levels in ascending order.
         *
         *      0      e.g. v = [2, 3, 4, 7];
         *     / \          pruneDescendantNodesInPlace(v);
         *    1   2         v == [2, 3, 4] (7 is removed as it is a descendant of 4)
         *   / \   \
         *  3   4   5       v = [0, 1, 5];
         *     / \          pruneDescendantNodesInPlace(v);
         *    6  7          v == [0] (both 1 and 5 are removed as they are descendants of 0)
         *
         * @param nodeIndices List of node indices to be processed in-place.
         * @pre All nodes in \p nodeIndices must be in the scene.
         * @pre \p nodeIndices must not have duplicate node indices.
         */
        void pruneDescendantNodesInPlace(std::vector<std::size_t> &nodeIndices) const noexcept {
            // Sort by node levels.
            std::ranges::sort(nodeIndices, {}, LIFT(getNodeLevel));

            // Remove nodes that are descendants of other nodes in nodeIndices. Remained nodes are re-assigned to
            // nodeIndices using `it` iterator.
            std::vector visited(asset.get().nodes.size(), false);
            auto it = nodeIndices.begin();
            for (std::size_t nodeIndex : nodeIndices) {
                if (visited[nodeIndex]) continue;

                *it++ = nodeIndex;
                traverseNode(asset, nodeIndex, [&](std::size_t nodeIndex) noexcept {
                    visited[nodeIndex] = true;
                });
            }

            // [nodeIndices.begin(), it) represents the pruned node indices.
            nodeIndices.resize(std::distance(nodeIndices.begin(), it));
        }

    private:
        std::reference_wrapper<const fastgltf::Asset> asset;

        /// ~0UZ means the node is a root node or outside the scene.
        std::vector<std::size_t> parentNodeIndices;

        /// Level (depth) of each node in the scene hierarchy.
        std::vector<std::size_t> nodeLevels;

        /// Visibility of each node that has a mesh.
        std::vector<bool> visibilities;

        std::vector<VisibilityState> states;
    };
}