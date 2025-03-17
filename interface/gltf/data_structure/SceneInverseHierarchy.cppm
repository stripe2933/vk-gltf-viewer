module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer:gltf.data_structure.SceneInverseHierarchy;

import std;
export import fastgltf;
import :gltf.algorithm.traversal;
import :helpers.optional;

namespace vk_gltf_viewer::gltf::ds {
    /**
     * @brief Cached data structure of every node's parent node index in a scene.
     */
    export class SceneInverseHierarchy {
        /**
         * @brief Index of the parent node for each node. If the node is root node, the value is as same as the node index.
         *
         * You should use getParentNodeIndex() method to get the parent node index with proper root node handling.
         */
        std::vector<std::size_t> parentNodeIndices;

    public:
        SceneInverseHierarchy(
            const fastgltf::Asset &asset LIFETIMEBOUND,
            const fastgltf::Scene &scene
        ) : parentNodeIndices { std::vector<std::size_t>(asset.nodes.size()) } {
            algorithm::traverseScene(asset, scene, [&](std::size_t nodeIndex) {
                for (std::size_t childIndex : asset.nodes[nodeIndex].children) {
                    parentNodeIndices[childIndex] = nodeIndex;
                }
            });
        }

        /**
         * @brief Get parent node index from current node index.
         * @param nodeIndex Index of the node.
         * @return Index of the parent node if the current node is not root node, otherwise <tt>std::nullopt</tt>.
         */
        [[nodiscard]] std::optional<std::size_t> getParentNodeIndex(std::size_t nodeIndex) const noexcept {
            const std::size_t parentNodeIndex = parentNodeIndices[nodeIndex];
            return value_if(parentNodeIndex != nodeIndex, parentNodeIndex);
        }
    };
}