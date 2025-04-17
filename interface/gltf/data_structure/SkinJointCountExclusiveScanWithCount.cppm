export module vk_gltf_viewer:gltf.data_structure.SkinJointCountExclusiveScanWithCount;

import std;
export import fastgltf;
import :helpers.algorithm;

namespace vk_gltf_viewer::gltf::ds {
    /**
     * @brief Exclusive scan of skin joints count in an asset, with additional total count at the end.
     */
    export struct SkinJointCountExclusiveScanWithCount final : std::vector<std::uint32_t> {
        explicit SkinJointCountExclusiveScanWithCount(const fastgltf::Asset &asset)
            : vector { exclusive_scan_with_count(asset.skins | std::views::transform([](const fastgltf::Skin &skin) -> std::uint32_t {
                return skin.joints.size();
            })) } { }
    };
}