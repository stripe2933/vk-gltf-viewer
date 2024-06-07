module;

#include <array>
#include <ranges>
#include <stdexcept>
#include <vector>

#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.Gpu;
import :helpers;

vk_gltf_viewer::vulkan::Gpu::QueueFamilies::QueueFamilies(
	vk::PhysicalDevice physicalDevice,
	vk::SurfaceKHR surface
) {
	for (auto [queueFamilyIndex, properties] : physicalDevice.getQueueFamilyProperties() | ranges::views::enumerate) {
		if (properties.queueFlags & vk::QueueFlagBits::eGraphics && physicalDevice.getSurfaceSupportKHR(queueFamilyIndex, surface)) {
			graphicsPresent = queueFamilyIndex;
			return;
		}
	}

	throw std::runtime_error { "Failed to find the required queue families" };
}


vk_gltf_viewer::vulkan::Gpu::Queues::Queues(
	vk::Device device,
	const QueueFamilies& queueFamilies
) : graphicsPresent{ device.getQueue(queueFamilies.graphicsPresent, 0) } {}

vk_gltf_viewer::vulkan::Gpu::Gpu(
    const vk::raii::Instance &instance,
    vk::SurfaceKHR surface
) : physicalDevice { selectPhysicalDevice(instance, surface) },
	queueFamilies { *physicalDevice, surface },
	allocator { createAllocator(instance) } { }

vk_gltf_viewer::vulkan::Gpu::~Gpu() {
	allocator.destroy();
}

auto vk_gltf_viewer::vulkan::Gpu::selectPhysicalDevice(
	const vk::raii::Instance &instance,
	vk::SurfaceKHR surface
) const -> decltype(physicalDevice) {
	VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);
	return instance.enumeratePhysicalDevices().front();
}

auto vk_gltf_viewer::vulkan::Gpu::createDevice() const -> decltype(device) {
	constexpr std::array queuePriorities{ 1.0f };
	const std::array queueCreateInfos {
		vk::DeviceQueueCreateInfo{
			{},
			queueFamilies.graphicsPresent,
			queuePriorities,
		},
	};
	constexpr std::array extensions{
#if __APPLE__
		vk::KHRPortabilitySubsetExtensionName,
#endif
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
		vk::KHRDynamicRenderingExtensionName,
#pragma clang diagnostic pop
		vk::KHRSwapchainExtensionName,
	};
	return { physicalDevice, vk::StructureChain {
		vk::DeviceCreateInfo{
			{},
			queueCreateInfos,
			{},
			extensions,
		},
		vk::PhysicalDeviceVulkan12Features{}
			.setBufferDeviceAddress(vk::True)
            .setStoragePushConstant8(vk::True),
		vk::PhysicalDeviceDynamicRenderingFeatures { vk::True },
	}.get() };
}

auto vk_gltf_viewer::vulkan::Gpu::createAllocator(
	const vk::raii::Instance &instance
) const -> decltype(allocator) {
	VULKAN_HPP_DEFAULT_DISPATCHER.init(*device);
	const vma::VulkanFunctions vulkanFuncs{
		instance.getDispatcher()->vkGetInstanceProcAddr,
		device.getDispatcher()->vkGetDeviceProcAddr,
	};
	return vma::createAllocator(vma::AllocatorCreateInfo{
		vma::AllocatorCreateFlagBits::eBufferDeviceAddress,
		*physicalDevice,
		*device,
		{}, {}, {}, {}, &vulkanFuncs,
		*instance,
		vk::makeApiVersion(0, 1, 2, 0),
	});
}