export module vk_gltf_viewer.gltf.data_structure.SceneInverseHierarchy;

import std;
export import fastgltf;

import vk_gltf_viewer.gltf.algorithm.traversal;
import vk_gltf_viewer.helpers.optional;

namespace vk_gltf_viewer::gltf::ds {
    /**
     * @brief Cached data structure of every node's parent node index in a scene.
     */
    export struct SceneInverseHierarchy {
        /**
         * @brief Index of the parent node for each node. If the node is root node, <tt>std::nullopt</tt> will be used.
         */
        std::vector<std::optional<std::size_t>> parentNodeIndices;

        SceneInverseHierarchy(const fastgltf::Asset &asset, const fastgltf::Scene &scene);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::gltf::ds::SceneInverseHierarchy::SceneInverseHierarchy(
    const fastgltf::Asset &asset,
    const fastgltf::Scene &scene
) {
    parentNodeIndices.resize(asset.nodes.size());
    algorithm::traverseScene(asset, scene, [&](std::size_t nodeIndex) {
        for (std::size_t childIndex : asset.nodes[nodeIndex].children) {
            parentNodeIndices[childIndex].emplace(nodeIndex);
        }
    });
}