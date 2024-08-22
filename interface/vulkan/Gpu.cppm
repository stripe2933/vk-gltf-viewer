export module vk_gltf_viewer:vulkan.Gpu;

import std;
export import vku;

namespace vk_gltf_viewer::vulkan {
	export struct QueueFamilies {
		std::uint32_t compute, graphicsPresent, transfer;

		QueueFamilies(vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface) {
			const std::vector queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
			compute = vku::getComputeSpecializedQueueFamily(queueFamilyProperties)
				.or_else([&] { return vku::getComputeQueueFamily(queueFamilyProperties); })
				.value();
			graphicsPresent = vku::getGraphicsPresentQueueFamily(physicalDevice, surface, queueFamilyProperties).value();
			transfer = vku::getTransferSpecializedQueueFamily(queueFamilyProperties).value_or(compute);
		}

		[[nodiscard]] auto getUniqueIndices() const noexcept -> std::vector<std::uint32_t> {
			std::vector indices { compute, graphicsPresent, transfer };
			std::ranges::sort(indices);

			const auto ret = std::ranges::unique(indices);
			indices.erase(ret.begin(), ret.end());

			return indices;
		}
	};

	export struct Queues {
		vk::Queue compute, graphicsPresent, transfer;

		Queues(vk::Device device, const QueueFamilies& queueFamilies) noexcept
		    : compute { device.getQueue(queueFamilies.compute, 0) }
		    , graphicsPresent{ device.getQueue(queueFamilies.graphicsPresent, 0) }
		    , transfer { device.getQueue(queueFamilies.transfer, 0) } { }

		[[nodiscard]] static auto getCreateInfos(vk::PhysicalDevice, const QueueFamilies &queueFamilies) noexcept {
			return vku::RefHolder {
				[&](std::span<const float> priorities) {
					return queueFamilies.getUniqueIndices()
						| std::views::transform([=](std::uint32_t queueFamilyIndex) {
							return vk::DeviceQueueCreateInfo {
								{},
								queueFamilyIndex,
								priorities,
							};
						})
						| std::ranges::to<std::vector>();
				},
				std::array { 1.f },
			};
		}
	};

	export struct Gpu : vku::Gpu<QueueFamilies, Queues> {
		bool supportUint8Index;

		Gpu(const vk::raii::Instance &instance [[clang::lifetimebound]], vk::SurfaceKHR surface)
			: vku::Gpu<QueueFamilies, Queues> { instance, Config {
				.verbose = true,
				.deviceExtensions = {
#if __APPLE__
					vk::KHRPortabilitySubsetExtensionName,
#endif
					vk::KHRDynamicRenderingExtensionName,
					vk::KHRSynchronization2ExtensionName,
					vk::EXTExtendedDynamicStateExtensionName,
					vk::KHRTimelineSemaphoreExtensionName,
					vk::KHRSwapchainExtensionName,
					vk::KHRSwapchainMutableFormatExtensionName, // For ImGui gamma correction.
				},
				.physicalDeviceFeatures = vk::PhysicalDeviceFeatures{}
					.setSamplerAnisotropy(true)
					.setShaderInt64(true)
					.setMultiDrawIndirect(true)
					.setDepthBiasClamp(true)
					.setShaderStorageImageWriteWithoutFormat(true),
				.queueFamilyGetter = [=](vk::PhysicalDevice physicalDevice) -> QueueFamilies {
					return { physicalDevice, surface };
				},
				.devicePNexts = std::tuple {
					vk::PhysicalDeviceVulkan11Features{}
						.setShaderDrawParameters(true)
						.setStorageBuffer16BitAccess(true)
						.setUniformAndStorageBuffer16BitAccess(true),
					vk::PhysicalDeviceVulkan12Features{}
						.setBufferDeviceAddress(true)
						.setDescriptorIndexing(true)
						.setDescriptorBindingSampledImageUpdateAfterBind(true)
						.setDescriptorBindingStorageImageUpdateAfterBind(true)
						.setImagelessFramebuffer(true)
						.setRuntimeDescriptorArray(true)
						.setStorageBuffer8BitAccess(true)
						.setUniformAndStorageBuffer8BitAccess(true)
						.setStoragePushConstant8(true)
						.setScalarBlockLayout(true)
						.setTimelineSemaphore(true),
					vk::PhysicalDeviceDynamicRenderingFeatures { true },
					vk::PhysicalDeviceSynchronization2Features { true },
					vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT { true },
				},
				.allocatorCreateFlags = vma::AllocatorCreateFlagBits::eBufferDeviceAddress,
				.apiVersion = vk::makeApiVersion(0, 1, 2, 0),
			} } {
			const vk::StructureChain physicalDeviceFeatures = physicalDevice.getFeatures2<
				vk::PhysicalDeviceFeatures2,
				vk::PhysicalDeviceIndexTypeUint8FeaturesKHR>();
			supportUint8Index = physicalDeviceFeatures.get<vk::PhysicalDeviceIndexTypeUint8FeaturesKHR>().indexTypeUint8;
		}
    };
};