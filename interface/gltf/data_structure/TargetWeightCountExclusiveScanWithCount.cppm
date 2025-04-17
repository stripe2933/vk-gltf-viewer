export module vk_gltf_viewer:gltf.data_structure.TargetWeightCountExclusiveScanWithCount;

import std;
export import fastgltf;
import :helpers.fastgltf;
import :helpers.algorithm;

namespace vk_gltf_viewer::gltf::ds {
    /**
     * @brief Exclusive scan of target weight counts in an asset, ordered by their corresponding nodes, and additional total weight count at the end.
     *
     * If a node has mesh, mesh target weight count is used.
     */
    export struct TargetWeightCountExclusiveScanWithCount final : std::vector<std::uint32_t> {
        explicit TargetWeightCountExclusiveScanWithCount(const fastgltf::Asset &asset)
            : vector { exclusive_scan_with_count(asset.nodes | std::views::transform([&](const fastgltf::Node &node) -> std::uint32_t {
                return getTargetWeightCount(node, asset);
            })) } { }
    };
}