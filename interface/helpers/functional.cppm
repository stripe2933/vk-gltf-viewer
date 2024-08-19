export module vk_gltf_viewer:helpers.functional;

namespace vk_gltf_viewer::inline helpers {
	export template <typename ...Fs>
	struct multilambda : Fs... {
		using Fs::operator()...;
	};
}