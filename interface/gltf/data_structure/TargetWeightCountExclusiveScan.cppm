export module vk_gltf_viewer:gltf.data_structure.TargetWeightCountExclusiveScan;

import std;
export import fastgltf;
import :helpers.algorithm;

namespace vk_gltf_viewer::gltf::ds {
    /**
     * @brief Exclusive scan of target weight counts in an asset, ordered by their corresponding nodes.
     *
     * If a node has mesh, mesh target weight count is used.
     */
    export struct TargetWeightCountExclusiveScan final : std::vector<std::uint32_t> {
        explicit TargetWeightCountExclusiveScan(const fastgltf::Asset &asset)
            : vector { exclusive_scan(asset.nodes | std::views::transform([&](const fastgltf::Node &node) {
                std::uint32_t weightCount = node.weights.size();
                if (node.meshIndex) {
                    const fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
                    weightCount = mesh.weights.size();
                }

                return weightCount;
            })) } { }
    };
}