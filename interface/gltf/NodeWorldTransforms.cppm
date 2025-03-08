module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer:gltf.NodeWorldTransforms;

import std;
export import fastgltf;
import :gltf.algorithm.traversal;
import :helpers.fastgltf;

namespace vk_gltf_viewer::gltf {
    export class NodeWorldTransforms : public std::vector<fastgltf::math::fmat4x4> {
        std::reference_wrapper<const fastgltf::Asset> asset;

    public:
        explicit NodeWorldTransforms(const fastgltf::Asset &asset LIFETIMEBOUND)
            : vector { std::from_range, asset.nodes | std::views::transform([](const fastgltf::Node &node) {
                return visit(fastgltf::visitor {
                    [](const fastgltf::TRS &trs) { return toMatrix(trs); },
                    [](fastgltf::math::fmat4x4 matrix) { return matrix; },
                }, node.transform);
            }) }
            , asset { asset } { }

        /**
         * @brief Update the world transform matrices of the current (specified by \p nodeIndex) and its descendant nodes.
         *
         * You can call this function when <tt>asset.nodes[nodeIndex]</tt> (local transform of the node) is changed, to update the world transform matrices of the current and its descendant nodes.
         *
         * @param nodeIndex Node index to be started.
         * @param worldTransform Start node world transform matrix.
         */
        void update(std::size_t nodeIndex, const fastgltf::math::fmat4x4 &worldTransform) {
            algorithm::traverseNode(asset, nodeIndex, [this](std::size_t nodeIndex, const fastgltf::math::fmat4x4 &nodeWorldTransform) {
                this->operator[](nodeIndex) = nodeWorldTransform;
            }, worldTransform);
        }

        /**
         * @brief Update the world transform matrices for all nodes in a scene.
         * @param scene Scene to be updated.
         */
        void update(const fastgltf::Scene &scene) {
            algorithm::traverseScene(asset, scene, [this](std::size_t nodeIndex, const fastgltf::math::fmat4x4 &nodeWorldTransform) {
                this->operator[](nodeIndex) = nodeWorldTransform;
            });
        }
    };
}