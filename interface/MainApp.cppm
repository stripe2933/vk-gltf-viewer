module;

#include <compare>

export module vk_gltf_viewer:MainApp;

export import vulkan_hpp; // have to be exported for initializing DispatchLoader.
import vku;
import :vulkan.Gpu;

namespace vk_gltf_viewer {
	export class MainApp {
	public:
		vk::raii::Context context;
		vk::raii::Instance instance = createInstance();
		vku::GlfwWindow window { 800, 480, "Vulkan glTF Viewer", instance };
		vulkan::Gpu gpu { instance, *window.surface };

		auto run() -> void;

	private:
		[[nodiscard]] auto createInstance() const -> decltype(instance);
	};
}