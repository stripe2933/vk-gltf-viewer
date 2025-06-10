module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.gltf.NodeWorldTransforms;

import std;
export import fastgltf;

import vk_gltf_viewer.gltf.algorithm.traversal;

namespace vk_gltf_viewer::gltf {
    export class NodeWorldTransforms : public std::vector<fastgltf::math::fmat4x4> {
        std::reference_wrapper<const fastgltf::Asset> asset;

    public:
        NodeWorldTransforms(const fastgltf::Asset &asset LIFETIMEBOUND, const fastgltf::Scene &scene);

        /**
         * @brief Update the world transform matrices for all nodes in a scene.
         * @param scene Scene to be updated.
         */
        void update(const fastgltf::Scene &scene);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::gltf::NodeWorldTransforms::NodeWorldTransforms(
    const fastgltf::Asset &asset,
    const fastgltf::Scene &scene
) : asset { asset } {
    resize(asset.nodes.size());
    update(scene);
}

void vk_gltf_viewer::gltf::NodeWorldTransforms::update(const fastgltf::Scene &scene) {
    algorithm::traverseScene(asset, scene, [this](std::size_t nodeIndex, const fastgltf::math::fmat4x4 &nodeWorldTransform) {
        operator[](nodeIndex) = nodeWorldTransform;
    });
}