export module vk_gltf_viewer:gltf.data_structure.NodeInstanceCountExclusiveScanWithCount;

import std;
import :helpers.algorithm;
export import fastgltf;

namespace vk_gltf_viewer::gltf::ds {
    /**
     * @brief Exclusive scan of the instance counts (or 1 if the node doesn't have any instance), and additional total instance count at the end.
     */
    export struct NodeInstanceCountExclusiveScanWithCount final : std::vector<std::uint32_t> {
        explicit NodeInstanceCountExclusiveScanWithCount(const fastgltf::Asset &asset)
            : vector { exclusive_scan_with_count(asset.nodes | std::views::transform([&](const fastgltf::Node &node) -> std::uint32_t {
                if (!node.meshIndex) {
                    return 0;
                }
                if (node.instancingAttributes.empty()) {
                    return 1;
                }
                else {
                    // According to the EXT_mesh_gpu_instancing specification, all attribute accessors in a given node
                    // must have the same count. Therefore, we can use the count of the first attribute accessor.
                    return asset.accessors[node.instancingAttributes[0].accessorIndex].count;
                }
            })) } { }
    };
}