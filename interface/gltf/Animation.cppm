module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.gltf.Animation;

import std;
export import cstring_view;
export import fastgltf;

export import vk_gltf_viewer.gltf.AssetExternalBuffers;
import vk_gltf_viewer.helpers.fastgltf;
export import vk_gltf_viewer.helpers.Flags;
import vk_gltf_viewer.helpers.ranges;

namespace vk_gltf_viewer::gltf {
    export enum class NodeAnimationUsage : std::uint8_t {
        Translation = 1, /// Node translation is used by an animation.
        Rotation = 2,    /// Node rotation is used by an animation.
        Scale = 4,       /// Node scale is used by an animation.
        Weights = 8,     /// Node target weight is used by an animation.
    };

    export
    [[nodiscard]] cpp_util::cstring_view to_string(NodeAnimationUsage usage) noexcept;

    export class Animation {
        std::reference_wrapper<fastgltf::Asset> asset;

        /**
         * @brief Copied input accessor floats, indexed by the accessor index.
         */
        std::unordered_map<std::size_t, std::vector<float>> inputAccessorData;

    public:
        std::reference_wrapper<const fastgltf::Animation> animation;
		std::unordered_map<std::size_t, Flags<NodeAnimationUsage>> nodeUsages;

        Animation(fastgltf::Asset &asset LIFETIMEBOUND, std::size_t animationIndex, const AssetExternalBuffers &adapter);

        /**
         * @brief Fetch transforms and target weights at given \p time, update asset data using them.
         * @param time Time to sample the animation.
         * @param transformedNodes Node indices that have been transformed by the animation.
         * @param morphedNodes Node indices that have been morphed by the animation (i.e. target weights have been updated).
         * @param adapter Buffer data adapter.
         */
        void update(float time, std::vector<std::size_t> &transformedNodes, std::vector<std::size_t> &morphedNodes, const AssetExternalBuffers &adapter) const;
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

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

/**
 * @brief Sample value using cubic spline interpolation.
 * @tparam T Type that supports addition and scalar multiplication.
 * @param propertyBefore Property value of k-th frame.
 * @param outTangentBefore Out-tangent of k-th frame.
 * @param propertyAfter Property value of (k+1)-th frame.
 * @param inTangentAfter In-tangent of (k+1)-th frame.
 * @param t Timestamp.
 * @param dt Time difference between k-th and (k+1)-th frame.
 * @return Interpolated property value of k-th frame.
 */
template <typename T>
[[nodiscard]] T cubicSpline(const T &propertyBefore, const T &outTangentBefore, const T &propertyAfter, const T &inTangentAfter, float t, float dt) noexcept {
	const float t2 = t * t;
	const float t3 = t2 * t;
	return propertyBefore * (2 * t3 - 3 * t2 + 1)
		+ outTangentBefore * dt * (t3 - 2 * t2 + t)
		+ propertyAfter * (-2 * t3 + 3 * t2)
		+ inTangentAfter * dt * (t3 - t2);
}

template <typename T>
[[nodiscard]] T sample(
	const fastgltf::Asset &asset,
	const fastgltf::AnimationSampler &sampler,
	const std::unordered_map<std::size_t, std::vector<float>> &inputAccessorData,
	float time,
	const vk_gltf_viewer::gltf::AssetExternalBuffers &adapter
) {
	const fastgltf::Accessor &outputAccessor = asset.accessors[sampler.outputAccessor];

	// Create functor that returns the output accessor data with the given index.
	const auto outputAccessorElementAt = [&](std::size_t index) {
		return fastgltf::getAccessorElement<T>(asset, outputAccessor, index, adapter);
	};

	const std::span input = inputAccessorData.at(sampler.inputAccessor);
 	time = std::fmod(time, input.back()); // Ugly hack to loop animations
 	auto it = std::ranges::lower_bound(input, time);
 	if (it == input.begin()) {
 		return outputAccessorElementAt(sampler.interpolation == fastgltf::AnimationInterpolation::CubicSpline);
 	}
 	if (it == input.end()) {
 		return outputAccessorElementAt(outputAccessor.count - (sampler.interpolation == fastgltf::AnimationInterpolation::CubicSpline ? 2 : 1));
 	}

 	auto i = it - input.begin();
 	const float t = (time - input[i - 1]) / (input[i] - input[i - 1]);

 	switch (sampler.interpolation) {
 		case fastgltf::AnimationInterpolation::Step:
 			return outputAccessorElementAt(i - 1);
 		case fastgltf::AnimationInterpolation::Linear: {
 			const auto vk = outputAccessorElementAt(i - 1);
 			const auto vk1 = outputAccessorElementAt(i);

 			if constexpr (std::same_as<T, fastgltf::math::fquat>) {
 				return fastgltf::math::slerp(vk, vk1, t);
 			}
 			else {
 				return fastgltf::math::lerp(vk, vk1, t);
 			}
 		}
 		case fastgltf::AnimationInterpolation::CubicSpline: {
 			const float dt = input[i] - input[i - 1];
 			auto v = cubicSpline(
 				outputAccessorElementAt(3 * (i - 1) + 1),
				outputAccessorElementAt(3 * (i - 1) + 2),
				outputAccessorElementAt(3 * i + 1),
				outputAccessorElementAt(3 * i),
				t, dt);

 			if constexpr (std::same_as<T, fastgltf::math::fquat>) {
 				v = normalize(v);
 			}
 			return v;
 		}
 	}
 	std::unreachable();
}

[[nodiscard]] std::valarray<float> sample(
	const fastgltf::Asset &asset,
	const fastgltf::AnimationSampler &sampler,
	const std::unordered_map<std::size_t, std::vector<float>> &inputAccessorData,
	float time,
	std::size_t targetWeightCount,
	const vk_gltf_viewer::gltf::AssetExternalBuffers &adapter
) {
	const fastgltf::Accessor &outputAccessor = asset.accessors[sampler.outputAccessor];

	// Create functor that returns the output accessor data with the given index.
	const auto outputAccessorElementsAt = [&](std::size_t index) {
		std::valarray<float> result(targetWeightCount);
		for (std::size_t i = 0; i < targetWeightCount; ++i) {
			result[i] = fastgltf::getAccessorElement<float>(asset, outputAccessor, targetWeightCount * index + i, adapter);
		}
		return result;
	};

	const std::span input = inputAccessorData.at(sampler.inputAccessor);
	time = std::fmod(time, input.back()); // Ugly hack to loop animations
	auto it = std::ranges::lower_bound(input, time);
	if (it == input.begin()) {
		return outputAccessorElementsAt(sampler.interpolation == fastgltf::AnimationInterpolation::CubicSpline);
	}
	if (it == input.end()) {
		return outputAccessorElementsAt(outputAccessor.count - (sampler.interpolation == fastgltf::AnimationInterpolation::CubicSpline ? 2 : 1));
	}

	auto i = it - input.begin();
	const float t = (time - input[i - 1]) / (input[i] - input[i - 1]);

	switch (sampler.interpolation) {
		case fastgltf::AnimationInterpolation::Step:
			return outputAccessorElementsAt(i - 1);
		case fastgltf::AnimationInterpolation::Linear: {
			const auto vk = outputAccessorElementsAt(i - 1);
			const auto vk1 = outputAccessorElementsAt(i);
			return vk + (vk1 - vk) * t;
		}
		case fastgltf::AnimationInterpolation::CubicSpline: {
			const float dt = input[i] - input[i - 1];
			return cubicSpline(
				outputAccessorElementsAt(3 * (i - 1) + 1),
				outputAccessorElementsAt(3 * (i - 1) + 2),
				outputAccessorElementsAt(3 * i + 1),
				outputAccessorElementsAt(3 * i),
				t, dt);
		}
	}
	std::unreachable();
}

cpp_util::cstring_view vk_gltf_viewer::gltf::to_string(NodeAnimationUsage usage) noexcept {
    switch (usage) {
        case NodeAnimationUsage::Translation: return "Translation";
        case NodeAnimationUsage::Rotation: return "Rotation";
        case NodeAnimationUsage::Scale: return "Scale";
        case NodeAnimationUsage::Weights: return "Weights";
    }
    std::unreachable();
}

vk_gltf_viewer::gltf::Animation::Animation(
	fastgltf::Asset &asset,
	std::size_t animationIndex,
	const AssetExternalBuffers &adapter
) : asset { asset },
	animation { asset.animations[animationIndex] } {
	for (const fastgltf::AnimationSampler &sampler : animation.get().samplers) {
		const fastgltf::Accessor &inputAccessor = asset.accessors[sampler.inputAccessor];
		if (auto [it, inserted] = inputAccessorData.try_emplace(sampler.inputAccessor, inputAccessor.count); inserted) {
			fastgltf::copyFromAccessor<float>(asset, inputAccessor, it->second.data(), adapter);
		}
	}

	for (const fastgltf::AnimationChannel &channel : animation.get().channels) {
		if (channel.nodeIndex) {
			Flags<NodeAnimationUsage> &usage = nodeUsages[*channel.nodeIndex];
			switch (channel.path) {
				case fastgltf::AnimationPath::Translation:
					usage |= NodeAnimationUsage::Translation;
					break;
				case fastgltf::AnimationPath::Rotation:
					usage |= NodeAnimationUsage::Rotation;
					break;
				case fastgltf::AnimationPath::Scale:
					usage |= NodeAnimationUsage::Scale;
					break;
				case fastgltf::AnimationPath::Weights:
					usage |= NodeAnimationUsage::Weights;
					break;
			}
		}
	}
}

void vk_gltf_viewer::gltf::Animation::update(
	float time,
	std::vector<std::size_t> &transformedNodes,
	std::vector<std::size_t> &morphedNodes,
	const AssetExternalBuffers &adapter
) const {
	for (const fastgltf::AnimationChannel &channel : animation.get().channels) {
		if (!channel.nodeIndex) {
			continue;
		}

		const std::size_t nodeIndex = *channel.nodeIndex;
		fastgltf::Node &node = asset.get().nodes[nodeIndex];

		const fastgltf::AnimationSampler &sampler = animation.get().samplers[channel.samplerIndex];
		switch (channel.path) {
			case fastgltf::AnimationPath::Translation:
				// glTF specification:
				// When a node is targeted for animation (referenced by an animation.channel.target), only TRS properties MAY be
				// present; matrix MUST NOT be present.
				get<fastgltf::TRS>(node.transform).translation = sample<fastgltf::math::fvec3>(asset, sampler, inputAccessorData, time, adapter);
				transformedNodes.push_back(nodeIndex);
				break;
			case fastgltf::AnimationPath::Rotation:
				get<fastgltf::TRS>(node.transform).rotation = sample<fastgltf::math::fquat>(asset, sampler, inputAccessorData, time, adapter);
				transformedNodes.push_back(nodeIndex);
				break;
			case fastgltf::AnimationPath::Scale:
				get<fastgltf::TRS>(node.transform).scale = sample<fastgltf::math::fvec3>(asset, sampler, inputAccessorData, time, adapter);
				transformedNodes.push_back(nodeIndex);
				break;
			case fastgltf::AnimationPath::Weights: {
				const std::span targetWeights = getTargetWeights(node, asset);
				std::ranges::copy(sample(asset, sampler, inputAccessorData, time, targetWeights.size(), adapter), targetWeights.data());
				morphedNodes.push_back(nodeIndex);
			}
		}
	}
}