module;

#include <array>
#include <compare>
#include <string_view>

#include <GLFW/glfw3.h>

export module vk_gltf_viewer:MainApp;

import vk_mem_alloc_hpp;
export import vulkan_hpp; // have to be exported for initializing DispatchLoader.

namespace vk_gltf_viewer {
	export class MainApp {
	public:
		struct QueueFamilies {
			std::uint32_t graphicsPresent;

			QueueFamilies(vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface);
		};

		struct Queues {
			vk::Queue graphicsPresent;

			Queues(vk::Device device, const QueueFamilies& queueFamilies);

			[[nodiscard]] static auto getDeviceQueueCreateInfos(const QueueFamilies& queueFamilies) -> std::array<vk::DeviceQueueCreateInfo, 1>;
		};

		GLFWwindow* pWindow;

		vk::raii::Context context;
		vk::raii::Instance instance = createInstance();
		vk::raii::SurfaceKHR surface = createSurface();
		vk::raii::PhysicalDevice physicalDevice = selectPhysicalDevice();
		QueueFamilies queueFamilies{ physicalDevice, *surface };
		vk::raii::Device device = createDevice();
		Queues queues{ *device, queueFamilies };
		vma::Allocator allocator = createAllocator();

		MainApp();
		~MainApp();

		auto run() -> void;

	private:
		[[nodiscard]] auto createInstance() const -> decltype(instance);
		[[nodiscard]] auto createSurface() const -> decltype(surface);
		[[nodiscard]] auto selectPhysicalDevice() const -> decltype(physicalDevice);
		[[nodiscard]] auto createDevice() const -> decltype(device);
		[[nodiscard]] auto createAllocator() const -> decltype(allocator);

		[[nodiscard]] auto createSwapchain(vk::SwapchainKHR oldSwapchain = {}) const -> vk::raii::SwapchainKHR;
		[[nodiscard]] auto createCommandPool(std::uint32_t queueFamilyIndex) const -> vk::raii::CommandPool;
	};
}