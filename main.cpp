#include <array>
#include <compare>
#include <format>
#include <functional>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_hpp_macros.hpp>

import vulkan_hpp;
import vk_mem_alloc_hpp;

class MainApp {
public:
	struct QueueFamilies {
		std::uint32_t graphicsPresent;

		QueueFamilies(vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface) {
			for (auto [queueFamilyIndex, properties] : physicalDevice.getQueueFamilyProperties() | std::views::enumerate) {
				if ((properties.queueFlags & vk::QueueFlagBits::eGraphics) && physicalDevice.getSurfaceSupportKHR(queueFamilyIndex, surface)) {
					graphicsPresent = queueFamilyIndex;
					return;
				}
			}

			throw std::runtime_error { "Failed to find the required queue families" };
		}
	};

	struct Queues {
		vk::Queue graphicsPresent;

		Queues(vk::Device device, const QueueFamilies& queueFamilies)
			: graphicsPresent{ device.getQueue(queueFamilies.graphicsPresent, 0) } {}

		[[nodiscard]] static auto getDeviceQueueCreateInfos(const QueueFamilies& queueFamilies) -> std::array<vk::DeviceQueueCreateInfo, 1> {
			static constexpr std::array queuePriorities{ 1.0f };
			return std::array {
				vk::DeviceQueueCreateInfo{
					{},
					queueFamilies.graphicsPresent,
					queuePriorities,
				},
			};
		}
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

	MainApp()
		: pWindow { glfwCreateWindow(800, 600, "Vulkan glTF Viewer", nullptr, nullptr) } {
		if (!pWindow) {
			const char* error;
			const int code = glfwGetError(&error);
			throw std::runtime_error{ std::format("Failed to create GLFW window: {} (error code {})", error, code) };
		}
	}

	~MainApp() {
		glfwDestroyWindow(pWindow);
	}

	auto run() -> void {
		vk::raii::SwapchainKHR swapchain = createSwapchain();
		std::vector swapchainImages = swapchain.getImages();

		// Descriptor/command pools.
		const vk::raii::CommandPool graphicsCommandPool = createCommandPool(queueFamilies.graphicsPresent);

		// Command buffers.
		const vk::CommandBuffer drawCommandBuffer = (*device).allocateCommandBuffers(vk::CommandBufferAllocateInfo{
			*graphicsCommandPool,
			vk::CommandBufferLevel::ePrimary,
			1,
		}).front();

		// Synchronization stuffs.
		const vk::raii::Semaphore swapchainImageAcquireSema{ device, vk::SemaphoreCreateInfo{} };
		const vk::raii::Fence inFlightFence{ device, vk::FenceCreateInfo { vk::FenceCreateFlagBits::eSignaled } };

		const auto initImageLayouts = [&]() -> void {
			const vk::CommandBuffer cb = (*device).allocateCommandBuffers(vk::CommandBufferAllocateInfo{
				*graphicsCommandPool,
				vk::CommandBufferLevel::ePrimary,
				1,
			}).front();
			cb.begin(vk::CommandBufferBeginInfo{});

			cb.pipelineBarrier(
				vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe,
				{}, {}, {},
				swapchainImages
					| std::views::transform([](vk::Image image) {
						return vk::ImageMemoryBarrier{
							{}, {},
							{}, vk::ImageLayout::ePresentSrcKHR,
							vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
							image,
							vk::ImageSubresourceRange{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
						};
						})
					| std::ranges::to<std::vector>());

			cb.end();
			queues.graphicsPresent.submit(vk::SubmitInfo{
				{},
				{},
				cb,
			});
			queues.graphicsPresent.waitIdle();
		};

		const auto recreateSwapchain = [&]() -> void {
			// Yield while window is minimized.
			for (int width, height; !glfwWindowShouldClose(pWindow) && (glfwGetFramebufferSize(pWindow, &width, &height), width == 0 || height == 0); std::this_thread::yield());

			swapchain = createSwapchain(*swapchain);
			swapchainImages = swapchain.getImages();

			initImageLayouts();
		};

		initImageLayouts();
		
		while (!glfwWindowShouldClose(pWindow)) {
			glfwPollEvents();

			constexpr std::uint64_t MAX_TIMEOUT = std::numeric_limits<std::uint64_t>::max();

			// Wait for the previous frame execution to finish.
			if (auto result = device.waitForFences(*inFlightFence, true, MAX_TIMEOUT); result != vk::Result::eSuccess) {
				throw std::runtime_error{ std::format("Failed to wait for in-flight fence: {}", to_string(result)) };
			}
			device.resetFences(*inFlightFence);

			// Acquire the next swapchain image.
			std::uint32_t imageIndex;
			try {
				imageIndex = (*device).acquireNextImageKHR(*swapchain, MAX_TIMEOUT, *swapchainImageAcquireSema).value;
			}
			catch (const vk::OutOfDateKHRError&) {
				device.waitIdle();
				recreateSwapchain();
				continue;
			}

			// Record draw command.
			drawCommandBuffer.begin(vk::CommandBufferBeginInfo{});
			drawCommandBuffer.end();

			// Submit draw command to the graphics queue.
			constexpr vk::PipelineStageFlags waitStages = vk::PipelineStageFlagBits::eColorAttachmentOutput;
			queues.graphicsPresent.submit(vk::SubmitInfo{
				*swapchainImageAcquireSema,
				waitStages,
				drawCommandBuffer,
			}, *inFlightFence);

			// Present the image to the swapchain.
			try {
				queues.graphicsPresent.presentKHR({ {}, *swapchain, imageIndex });
			} 
			catch (const vk::OutOfDateKHRError&) {
				device.waitIdle();
				recreateSwapchain();
			}
		}
		device.waitIdle();
	}

private:
	[[nodiscard]] auto createInstance() const -> decltype(instance) {
		constexpr vk::ApplicationInfo appInfo{
			"Vulkan glTF Viewer", 0,
			nullptr, 0,
			vk::makeApiVersion(0, 1, 3, 0),
		};

		const std::vector layers{
#ifndef NDEBUG
			"VK_LAYER_KHRONOS_validation",
#endif
		};

		std::vector<const char*> extensions;
		std::uint32_t glfwExtensionCount;
		const char** const glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
		extensions.append_range(std::views::counted(glfwExtensions, glfwExtensionCount));

		return { context, vk::InstanceCreateInfo{
			{},
			&appInfo,
			layers,
			extensions,
		} };
	}

	[[nodiscard]] auto createSurface() const -> decltype(surface) {
		VkSurfaceKHR surface;
		if (glfwCreateWindowSurface(*instance, pWindow, nullptr, &surface) != VK_SUCCESS) {
			const char* error;
			const int code = glfwGetError(&error);
			throw std::runtime_error{ std::format("Failed to create window surface: {} (error code {})", error, code) };
		}
		return { instance, surface };
	}

	[[nodiscard]] auto selectPhysicalDevice() const -> decltype(physicalDevice) {
#if VULKAN_HPP_DISPATCH_LOADER_DYNAMIC == 1
		VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);
#endif
		return instance.enumeratePhysicalDevices().front();
	}

	[[nodiscard]] auto createDevice() const -> decltype(device) {
		const auto queueCreateInfos = Queues::getDeviceQueueCreateInfos(queueFamilies);
		constexpr std::array extensions{
			vk::KHRSwapchainExtensionName,
		};
		return { physicalDevice, vk::DeviceCreateInfo{
			{},
			queueCreateInfos,
			{},
			extensions,
		} };
	}

	[[nodiscard]] auto createAllocator() const -> decltype(allocator) {
#if VULKAN_HPP_DISPATCH_LOADER_DYNAMIC == 1
		VULKAN_HPP_DEFAULT_DISPATCHER.init(*device);
#endif
		const vma::VulkanFunctions vulkanFuncs{
			instance.getDispatcher()->vkGetInstanceProcAddr,
			device.getDispatcher()->vkGetDeviceProcAddr,
		};
		return vma::createAllocator(vma::AllocatorCreateInfo{
			{},
			*physicalDevice,
			*device,
			{}, {}, {}, {}, &vulkanFuncs,
			*instance,
			vk::makeApiVersion(0, 1, 3, 0),
		});
	}

	[[nodiscard]] auto createSwapchain(
		vk::SwapchainKHR oldSwapchain = {}
	) const -> vk::raii::SwapchainKHR {
		// Get window framebuffer size.
		int width, height;
		glfwGetFramebufferSize(pWindow, &width, &height);

		const vk::SurfaceCapabilitiesKHR surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface);
		return { device, vk::SwapchainCreateInfoKHR{
			{},
			*surface,
			std::min(surfaceCapabilities.minImageCount + 1, surfaceCapabilities.maxImageCount),
			vk::Format::eB8G8R8A8Srgb,
			vk::ColorSpaceKHR::eSrgbNonlinear,
			vk::Extent2D { static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height) },
			1,
			vk::ImageUsageFlagBits::eColorAttachment,
			{}, {},
			surfaceCapabilities.currentTransform,
			vk::CompositeAlphaFlagBitsKHR::eOpaque,
			vk::PresentModeKHR::eFifo,
			true,
			oldSwapchain,
		} };
	}

	[[nodiscard]] auto createCommandPool(
		std::uint32_t queueFamilyIndex
	) const -> vk::raii::CommandPool {
		return { device, vk::CommandPoolCreateInfo{
			vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
			queueFamilyIndex,
		} };
	}
};

#if VULKAN_HPP_DISPATCH_LOADER_DYNAMIC == 1
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
#endif

int main() {
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

#if ( VULKAN_HPP_DISPATCH_LOADER_DYNAMIC == 1 )
	VULKAN_HPP_DEFAULT_DISPATCHER.init();
#endif

	MainApp{}.run();

	glfwTerminate();
}