export module vk_gltf_viewer:gltf.data_structure.SkinJointCountExclusiveScan;

import std;
export import fastgltf;
import :helpers.algorithm;

namespace vk_gltf_viewer::gltf::ds {
    /**
     * @brief Exclusive scan of skin joints count in an asset.
     */
    export struct SkinJointCountExclusiveScan final : std::vector<std::uint32_t> {
        explicit SkinJointCountExclusiveScan(const fastgltf::Asset &asset)
            : vector { exclusive_scan(asset.skins | std::views::transform([](const fastgltf::Skin &skin) -> std::uint32_t {
                return skin.joints.size();
            })) } { }
    };
}