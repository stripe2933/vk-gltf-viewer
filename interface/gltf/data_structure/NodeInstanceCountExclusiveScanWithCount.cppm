export module vk_gltf_viewer.gltf.data_structure.NodeInstanceCountExclusiveScanWithCount;

import std;
export import fastgltf;

import vk_gltf_viewer.helpers.algorithm;

namespace vk_gltf_viewer::gltf::ds {
    /**
     * @brief Exclusive scan of the instance counts, and additional total instance count at the end.
     */
    export struct NodeInstanceCountExclusiveScanWithCount final : std::vector<std::uint32_t> {
        explicit NodeInstanceCountExclusiveScanWithCount(const fastgltf::Asset &asset);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::gltf::ds::NodeInstanceCountExclusiveScanWithCount::NodeInstanceCountExclusiveScanWithCount(
    const fastgltf::Asset &asset
) : vector { exclusive_scan_with_count(asset.nodes | std::views::transform([&](const fastgltf::Node &node) -> std::uint32_t {
        if (node.instancingAttributes.empty()) {
            return 0;
        }
        else {
            // According to the EXT_mesh_gpu_instancing specification, all attribute accessors in a given node
            // must have the same count. Therefore, we can use the count of the first attribute accessor.
            return asset.accessors[node.instancingAttributes[0].accessorIndex].count;
        }
    })) } { }