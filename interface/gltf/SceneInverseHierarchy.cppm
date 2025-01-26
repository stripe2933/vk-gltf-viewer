export module vk_gltf_viewer:gltf.SceneInverseHierarchy;

import std;
export import fastgltf;
import :gltf.algorithm.traversal;
import :helpers.fastgltf;
import :helpers.optional;
import :helpers.ranges;

namespace vk_gltf_viewer::gltf {
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
        SceneInverseHierarchy(const fastgltf::Asset &asset, const fastgltf::Scene &scene)
            : parentNodeIndices { createParentNodeIndices(asset, scene) } { }

        /**
         * @brief Get parent node index from current node index.
         * @param nodeIndex Index of the node.
         * @return Index of the parent node if the current node is not root node, otherwise <tt>std::nullopt</tt>.
         */
        [[nodiscard]] std::optional<std::size_t> getParentNodeIndex(std::size_t nodeIndex) const noexcept {
            const std::size_t parentNodeIndex = parentNodeIndices[nodeIndex];
            return value_if(parentNodeIndex != nodeIndex, parentNodeIndex);
        }

    private:
        [[nodiscard]] std::vector<std::size_t> createParentNodeIndices(
            const fastgltf::Asset &asset,
            const fastgltf::Scene &scene
        ) const noexcept {
            std::vector<std::size_t> result { std::from_range, ranges::views::upto(asset.nodes.size()) };
            algorithm::traverseScene(asset, scene, [&](std::size_t nodeIndex) {
                for (std::size_t childIndex : asset.nodes[nodeIndex].children) {
                    result[childIndex] = nodeIndex;
                }

            });
            return result;
        }
    };
}