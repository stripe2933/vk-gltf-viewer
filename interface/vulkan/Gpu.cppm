module;

#include <compare>

export module vk_gltf_viewer:vulkan.Gpu;

export import vk_mem_alloc_hpp;
export import vulkan_hpp;

namespace vk_gltf_viewer::vulkan {
    class Gpu {
	public:
		struct QueueFamilies {
			std::uint32_t graphicsPresent, transfer;

			QueueFamilies(vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface);
		};

		struct Queues {
			vk::Queue graphicsPresent, transfer;

			Queues(vk::Device device, const QueueFamilies& queueFamilies) noexcept;
		};

		vk::raii::PhysicalDevice physicalDevice;
		QueueFamilies queueFamilies;
		vk::raii::Device device = createDevice();
		Queues queues{ *device, queueFamilies };
		vma::Allocator allocator;

		Gpu(const vk::raii::Instance &instance, vk::SurfaceKHR surface);
    	~Gpu();

		[[nodiscard]] auto selectPhysicalDevice(const vk::raii::Instance &instance, vk::SurfaceKHR surface) const -> decltype(physicalDevice);
		[[nodiscard]] auto createDevice() const -> decltype(device);
		[[nodiscard]] auto createAllocator(const vk::raii::Instance &instance) const -> decltype(allocator);
    };
};