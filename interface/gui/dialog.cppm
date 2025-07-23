export module vk_gltf_viewer.gui.dialog;

import std;

export import vk_gltf_viewer.gltf.AssetExtended;

namespace vk_gltf_viewer::gui {
    export struct AnimationCollisionResolveDialog {
        std::reference_wrapper<gltf::AssetExtended> assetExtended;
        std::size_t animationIndexToEnable;
        std::map<std::size_t /* animation index */, std::map<std::size_t /* node index */, Flags<gltf::NodeAnimationUsage>>> collisions;
    };

    export std::variant<std::monostate, AnimationCollisionResolveDialog> currentDialog;
}