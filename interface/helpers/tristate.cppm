module;

#include <cassert>

export module vk_gltf_viewer:helpers.tristate;

import std;
import :helpers.concepts;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

namespace tristate {
    /**
     * Propagate the current node (in \p nodeIndex) state to all its children.
     * @param childIndicesGetter A function that returns the children indices of a node with the given index.
     * @param nodeIndex Current node index.
     * @param tristates Tri-state values of all nodes. Indeterminate state is represented by <tt>std::nullopt</tt>.
     * @note It asserts that the current node state is not indeterminate(<tt>std::nullopt</tt>).
     * @note This function is a recursive function, and will be terminated when all children have been visited.
     */
    export auto propagateTopDown(
        std::invocable<std::size_t> auto &&childIndicesGetter,
        std::size_t nodeIndex,
        std::span<std::optional<bool>> tristates
    ) -> void {
        const auto &currentState = tristates[nodeIndex];
        assert(currentState.has_value() && "Indeterminate state cannot be propagated top-down.");
        for (std::size_t childNodeIndex : childIndicesGetter(nodeIndex)) {
            tristates[childNodeIndex] = currentState;
            propagateTopDown(childIndicesGetter, childNodeIndex, tristates);
        }
    }

    /**
     * Propagate the current node (in \p nodeIndex) state to all its ancestors.
     * @param parentIndexGetter A function that returns the parent index of a node with the given index. If the current node is a root node, it must return <tt>std::nullopt</tt>.
     * @param childrenIndicesGetter A function that returns the children indices of a node with the given index.
     * @param nodeIndex Current node index.
     * @param tristates Tri-state values of all nodes. Indeterminate state is represented by <tt>std::nullopt</tt>.
     * @note This function is a recursive function, and will be terminated when the current node is a root node.
     * @note It works by the following algorithm:
     *   - If the current node state is indeterminate, then all its ancestors will be indeterminate.
     *   - If all siblings have the same state, then the parent node will have the same state.
     *   - Otherwise, the parent node state is indeterminate.
     */
    export auto propagateBottomUp(
        concepts::compatible_signature_of<std::optional<std::size_t>, std::size_t> auto &&parentIndexGetter,
        std::invocable<std::size_t> auto &&childrenIndicesGetter,
        std::size_t nodeIndex,
        std::span<std::optional<bool>> tristates
    ) -> void {
        std::optional<std::size_t> parentNodeIndex = parentIndexGetter(nodeIndex);
        if (!parentNodeIndex) {
            // Current node is a root node.
            return;
        }

        auto &currentState = tristates[nodeIndex];
        if (!currentState) {
            // If current node state is indeterminate, so do all its ancestors.
            do {
                tristates[*parentNodeIndex].reset();
            } while ((parentNodeIndex = parentIndexGetter(*parentNodeIndex)));
            return;
        }

        const auto &siblingIndices = childrenIndicesGetter(*parentNodeIndex);
        const bool isSiblingStatesEqual = std::ranges::adjacent_find(
            siblingIndices, std::ranges::not_equal_to{},
            [=](auto siblingIndex) { return tristates[siblingIndex]; }) == siblingIndices.end();
        if (isSiblingStatesEqual) {
            // If all siblings have the same state, parent node should have the same state (regardless of on/off).
            tristates[*parentNodeIndex].emplace(*currentState);
        }
        else {
            // Otherwise, parent node state is indeterminate.
            tristates[*parentNodeIndex].reset();
        }

        propagateBottomUp(FWD(parentIndexGetter), FWD(childrenIndicesGetter), *parentNodeIndex, tristates);
    }
}