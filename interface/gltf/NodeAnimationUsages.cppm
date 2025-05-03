export module vk_gltf_viewer:gltf.NodeAnimationUsages;

import std;
export import cstring_view;
export import fastgltf;
export import :helpers.enums.Flags;
import :helpers.ranges;

namespace vk_gltf_viewer::gltf {
    export enum class NodeAnimationUsage : std::uint8_t {
        Translation = 1, /// Node translation is used by an animation.
        Rotation = 2,    /// Node rotation is used by an animation.
        Scale = 4,       /// Node scale is used by an animation.
        Weights = 8,     /// Node target weight is used by an animation.
    };

    [[nodiscard]] constexpr NodeAnimationUsage convert(fastgltf::AnimationPath path) noexcept {
        switch (path) {
            case fastgltf::AnimationPath::Translation:
                return NodeAnimationUsage::Translation;
            case fastgltf::AnimationPath::Rotation:
                return NodeAnimationUsage::Rotation;
            case fastgltf::AnimationPath::Scale:
                return NodeAnimationUsage::Scale;
            case fastgltf::AnimationPath::Weights:
                return NodeAnimationUsage::Weights;
        }
        std::unreachable();
    }

    export
    [[nodiscard]] constexpr cpp_util::cstring_view to_string(NodeAnimationUsage usage) {
        switch (usage) {
            case NodeAnimationUsage::Translation: return "Translation";
            case NodeAnimationUsage::Rotation: return "Rotation";
            case NodeAnimationUsage::Scale: return "Scale";
            case NodeAnimationUsage::Weights: return "Weights";
        }
        std::unreachable();
    }

    /**
     * Element at the index <tt>i</tt> represents the association whose key is animation index, and value is which path
     * is used by the animation.
     *
     * @code
     * for (const auto &[nodeIndex, map] : nodeAnimationUsages) {
     *     std::println("Animation associated by node {}: ", nodeIndex);
     *     for (const auto &[animationIndex, usage] : map) {
     *         std::println("Animation {} (name={}) uses {}", animationIndex, asset.animations[animationIndex].name, usage);
     *     }
     * }
     * @endcode
     */
    export struct NodeAnimationUsages : std::vector<std::unordered_map<std::size_t, Flags<NodeAnimationUsage>>> {
        explicit NodeAnimationUsages(const fastgltf::Asset &asset) {
            resize(asset.nodes.size());
            for (const auto &[animationIndex, animation] : asset.animations | ranges::views::enumerate) {
                for (const fastgltf::AnimationChannel &channel : animation.channels) {
                    if (channel.nodeIndex) {
                        operator[](*channel.nodeIndex)[animationIndex] |= convert(channel.path);
                    }
                }
            }
        }
    };
}

export template <>
struct std::formatter<vk_gltf_viewer::gltf::NodeAnimationUsage> : formatter<std::string_view> {
    auto format(vk_gltf_viewer::gltf::NodeAnimationUsage usage, auto &ctx) const {
        return formatter<std::string_view>::format(to_string(usage), ctx);
    }
};

export template <>
struct FlagTraits<vk_gltf_viewer::gltf::NodeAnimationUsage> {
    static constexpr bool isBitmask = true;
    static constexpr Flags<vk_gltf_viewer::gltf::NodeAnimationUsage> allFlags
        = vk_gltf_viewer::gltf::NodeAnimationUsage::Translation
        | vk_gltf_viewer::gltf::NodeAnimationUsage::Rotation
        | vk_gltf_viewer::gltf::NodeAnimationUsage::Scale
        | vk_gltf_viewer::gltf::NodeAnimationUsage::Weights;
};