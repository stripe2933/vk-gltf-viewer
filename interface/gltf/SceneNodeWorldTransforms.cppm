export module vk_gltf_viewer:gltf.SceneNodeWorldTransforms;

import std;
export import fastgltf;
import :gltf.algorithm.traversal;

namespace vk_gltf_viewer::gltf {
    export class SceneNodeWorldTransforms {
        std::reference_wrapper<const fastgltf::Asset> asset;

    public:
        std::vector<fastgltf::math::fmat4x4> worldTransforms;

        SceneNodeWorldTransforms(const fastgltf::Asset &asset [[clang::lifetimebound]], const fastgltf::Scene &scene)
            : asset { asset }
            , worldTransforms { createNodeWorldTransforms(scene) }{ }

        /**
         * @brief Update the world transform matrices of the current (specified by \p nodeIndex) and its descendant nodes.
         *
         * You can call this function when <tt>asset.nodes[nodeIndex]</tt> (local transform of the node) is changed, to update the world transform matrices of the current and its descendant nodes.
         *
         * @param nodeIndex Node index to be started.
         * @param worldTransform Start node world transform matrix.
         */
        void updateFrom(std::size_t nodeIndex, const fastgltf::math::fmat4x4 &worldTransform) {
            algorithm::traverseNode(asset, nodeIndex, [this](std::size_t nodeIndex, const fastgltf::math::fmat4x4 &nodeWorldTransform) {
                worldTransforms[nodeIndex] = nodeWorldTransform;
            }, worldTransform);
        }

    private:
        [[nodiscard]] std::vector<fastgltf::math::fmat4x4> createNodeWorldTransforms(const fastgltf::Scene &scene) const noexcept {
            std::vector<fastgltf::math::fmat4x4> result(asset.get().nodes.size());
            algorithm::traverseScene(asset, scene, [&](std::size_t nodeIndex, const fastgltf::math::fmat4x4 &nodeWorldTransform) {
                result[nodeIndex] = nodeWorldTransform;
            });
            return result;
        }
    };
}