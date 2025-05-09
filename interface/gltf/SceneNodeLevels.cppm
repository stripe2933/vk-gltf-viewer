export module vk_gltf_viewer:gltf.SceneNodeLevels;

import std;
export import fastgltf;

namespace vk_gltf_viewer::gltf {
    /**
     * @brief Data structure that caches the level of a node in the glTF asset scene.
     *
     * You can access the level of <tt>i</tt>-th node as <tt>sceneNodeLevels[i]</tt>.
     *
     * @note Accessing a node that is not in the scene will return undefined value.
     */
    export struct SceneNodeLevels : std::vector<std::size_t> {
        SceneNodeLevels(const fastgltf::Asset &asset, const fastgltf::Scene &scene) {
            resize(asset.nodes.size());
            for (std::size_t nodeIndex : scene.nodeIndices) {
                [&](this const auto &self, std::size_t nodeIndex, std::size_t level) -> void {
                    operator[](nodeIndex) = level;

                    for (std::size_t childNodeIndex : asset.nodes[nodeIndex].children) {
                        self(childNodeIndex, level + 1);
                    }
                }(nodeIndex, 0);
            }
        }
    };
}