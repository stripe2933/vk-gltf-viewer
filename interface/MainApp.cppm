module;

#include <compare>
#include <string_view>

#include <GLFW/glfw3.h>

export module vk_gltf_viewer:MainApp;

export import vulkan_hpp; // have to be exported for initializing DispatchLoader.
import :vulkan.Gpu;

namespace vk_gltf_viewer {
	export class MainApp {
	public:
		GLFWwindow* pWindow;

		vk::raii::Context context;
		vk::raii::Instance instance = createInstance();
		vk::raii::SurfaceKHR surface = createSurface();
		vulkan::Gpu gpu { instance, *surface };

		MainApp();
		~MainApp();

		auto run() -> void;

	private:
		[[nodiscard]] auto createInstance() const -> decltype(instance);
		[[nodiscard]] auto createSurface() const -> decltype(surface);
	};
}