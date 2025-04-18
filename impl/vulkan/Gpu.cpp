module;

#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.Gpu;

import std;
import :helpers.ranges;

constexpr std::array requiredExtensions {
#if __APPLE__
    vk::KHRPortabilitySubsetExtensionName,
#endif
    vk::KHRDynamicRenderingExtensionName,
    vk::KHRSynchronization2ExtensionName,
    vk::EXTExtendedDynamicStateExtensionName,
    vk::KHRPushDescriptorExtensionName,
    vk::KHRSwapchainExtensionName,
};

constexpr std::array optionalExtensions {
    vk::KHRSwapchainMutableFormatExtensionName,
    vk::EXTIndexTypeUint8ExtensionName,
    vk::AMDShaderImageLoadStoreLodExtensionName,
};

constexpr vk::PhysicalDeviceFeatures requiredFeatures = vk::PhysicalDeviceFeatures{}
    .setSamplerAnisotropy(true)
    .setShaderInt16(true)
    .setMultiDrawIndirect(true)
    .setShaderStorageImageWriteWithoutFormat(true)
    .setIndependentBlend(true);

vk_gltf_viewer::vulkan::QueueFamilies::QueueFamilies(vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface) {
    const std::vector queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

    compute = vku::getComputeSpecializedQueueFamily(queueFamilyProperties)
        .or_else([&]() { return vku::getComputeQueueFamily(queueFamilyProperties); })
        .value();
    graphicsPresent = vku::getGraphicsPresentQueueFamily(physicalDevice, surface, queueFamilyProperties).value();
    transfer = vku::getTransferSpecializedQueueFamily(queueFamilyProperties).value_or(compute);

    // Calculate unique queue family indices.
    uniqueIndices = { compute, graphicsPresent, transfer };
    std::ranges::sort(uniqueIndices);
    const auto ret = std::ranges::unique(uniqueIndices);
    uniqueIndices.erase(ret.begin(), ret.end());
}

vk_gltf_viewer::vulkan::Queues::Queues(vk::Device device, const QueueFamilies& queueFamilies) noexcept
    : compute { device.getQueue(queueFamilies.compute, 0) }
    , graphicsPresent{ device.getQueue(queueFamilies.graphicsPresent, 0) }
    , transfer { device.getQueue(queueFamilies.transfer, 0) } { }

vku::RefHolder<std::vector<vk::DeviceQueueCreateInfo>> vk_gltf_viewer::vulkan::Queues::getCreateInfos(
    vk::PhysicalDevice,
    const QueueFamilies &queueFamilies
) noexcept {
    return vku::RefHolder { [&]() {
        static constexpr std::array priorities { 1.f };
        return queueFamilies.uniqueIndices
            | std::views::transform([=](std::uint32_t queueFamilyIndex) {
                return vk::DeviceQueueCreateInfo {
                    {},
                    queueFamilyIndex,
                    priorities,
                };
            })
            | std::ranges::to<std::vector>();
    } };
}

vk_gltf_viewer::vulkan::Gpu::Gpu(const vk::raii::Instance &instance, vk::SurfaceKHR surface)
    : physicalDevice { selectPhysicalDevice(instance, surface) }
    , queueFamilies { physicalDevice, surface }
    , allocator { createAllocator(instance) } {
    // Retrieve physical device properties.
    const vk::StructureChain physicalDeviceProperties = physicalDevice.getProperties2<
        vk::PhysicalDeviceProperties2,
        vk::PhysicalDeviceSubgroupProperties,
        vk::PhysicalDeviceDescriptorIndexingProperties>();
    subgroupSize = physicalDeviceProperties.get<vk::PhysicalDeviceSubgroupProperties>().subgroupSize;
    maxPerStageDescriptorUpdateAfterBindSamplers = physicalDeviceProperties.get<vk::PhysicalDeviceDescriptorIndexingProperties>().maxPerStageDescriptorUpdateAfterBindSamplers;

	// Retrieve physical device memory properties.
	const vk::PhysicalDeviceMemoryProperties memoryProperties = physicalDevice.getMemoryProperties();
    isUmaDevice = memoryProperties.memoryHeapCount == 1;
}

vk_gltf_viewer::vulkan::Gpu::~Gpu() {
    allocator.destroy();
}

vk::raii::PhysicalDevice vk_gltf_viewer::vulkan::Gpu::selectPhysicalDevice(const vk::raii::Instance &instance, vk::SurfaceKHR surface) const {
    std::vector physicalDevices = instance.enumeratePhysicalDevices();
    const auto physicalDeviceRater = [&](vk::PhysicalDevice physicalDevice) -> std::uint32_t {
        // Check queue family availability.
        try {
            std::ignore = QueueFamilies { physicalDevice, surface };
        }
        catch (const std::runtime_error&) {
            return 0U;
        }

        // Check device extension availability.
        const std::vector availableExtensions = physicalDevice.enumerateDeviceExtensionProperties();
        std::vector availableExtensionNames
            = availableExtensions
            | std::views::transform([](const vk::ExtensionProperties &properties) {
                return static_cast<std::string_view>(properties.extensionName);
            })
            | std::ranges::to<std::vector>();
        std::ranges::sort(availableExtensionNames);

        std::vector requiredExtensionNames
            = requiredExtensions
            | std::views::transform([](const char *str) { return std::string_view { str }; })
            | std::ranges::to<std::vector>();
        std::ranges::sort(requiredExtensionNames);

        if (!std::ranges::includes(availableExtensionNames, requiredExtensionNames)) {
            return 0U;
        }

        // Check physical device feature availability.
        const vk::StructureChain availableFeatures
            = physicalDevice.getFeatures2<
                vk::PhysicalDeviceFeatures2,
                vk::PhysicalDeviceVulkan11Features,
                vk::PhysicalDeviceVulkan12Features,
                vk::PhysicalDeviceDynamicRenderingFeatures,
                vk::PhysicalDeviceSynchronization2Features,
                vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
        const vk::PhysicalDeviceFeatures &features = availableFeatures.get<vk::PhysicalDeviceFeatures2>().features;
        const vk::PhysicalDeviceVulkan11Features &vulkan11Features = availableFeatures.get<vk::PhysicalDeviceVulkan11Features>();
        const vk::PhysicalDeviceVulkan12Features &vulkan12Features = availableFeatures.get<vk::PhysicalDeviceVulkan12Features>();
        if (!features.samplerAnisotropy ||
            !features.shaderInt16 ||
            !features.multiDrawIndirect ||
            !features.shaderStorageImageWriteWithoutFormat ||
            !features.independentBlend ||
            !vulkan11Features.shaderDrawParameters ||
            !vulkan11Features.storageBuffer16BitAccess ||
            !vulkan11Features.uniformAndStorageBuffer16BitAccess ||
            !vulkan11Features.multiview ||
            !vulkan12Features.bufferDeviceAddress ||
            !vulkan12Features.descriptorIndexing ||
            !vulkan12Features.descriptorBindingSampledImageUpdateAfterBind ||
            !vulkan12Features.descriptorBindingVariableDescriptorCount ||
            !vulkan12Features.runtimeDescriptorArray ||
            !vulkan12Features.storageBuffer8BitAccess ||
            !vulkan12Features.scalarBlockLayout ||
            !vulkan12Features.timelineSemaphore ||
            !vulkan12Features.shaderInt8 ||
            !availableFeatures.get<vk::PhysicalDeviceDynamicRenderingFeatures>().dynamicRendering ||
            !availableFeatures.get<vk::PhysicalDeviceSynchronization2Features>().synchronization2 ||
            !availableFeatures.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState) {
            return 0U;
        }

        // Check physical device properties.
        const vk::StructureChain physicalDeviceProperties = physicalDevice.getProperties2<
            vk::PhysicalDeviceProperties2,
            vk::PhysicalDeviceSubgroupProperties>();
        const vk::PhysicalDeviceProperties &properties = physicalDeviceProperties.get<vk::PhysicalDeviceProperties2>().properties;
        if (physicalDeviceProperties.get<vk::PhysicalDeviceSubgroupProperties>().subgroupSize < 16U) {
            return 0U;
        }

        // Rate the physical device.
        std::uint32_t score = 0;
        if (properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
            score += 1000;
        }

        score += properties.limits.maxImageDimension2D;

        return score;
    };

    vk::raii::PhysicalDevice bestPhysicalDevice = *std::ranges::max_element(physicalDevices, {}, physicalDeviceRater);
    if (physicalDeviceRater(*bestPhysicalDevice) == 0U) {
        throw std::runtime_error { "No suitable GPU for the application." };
    }

    return bestPhysicalDevice;
}

vk::raii::Device vk_gltf_viewer::vulkan::Gpu::createDevice() {
    // Add optional extensions if available.
	const std::vector availableExtensions = physicalDevice.enumerateDeviceExtensionProperties();
    const std::unordered_set availableExtensionNames
        = availableExtensions
        | std::views::transform([](const vk::ExtensionProperties &properties) {
            return static_cast<std::string_view>(properties.extensionName);
        })
        | std::ranges::to<std::unordered_set>();

    std::vector extensions { std::from_range, requiredExtensions };
    for (const char *optionalExtensionName : optionalExtensions) {
        if (availableExtensionNames.contains(optionalExtensionName)) {
            extensions.push_back(optionalExtensionName);
        }
    }

    supportSwapchainMutableFormat = availableExtensionNames.contains(vk::KHRSwapchainMutableFormatExtensionName);
    supportShaderImageLoadStoreLod = availableExtensionNames.contains(vk::AMDShaderImageLoadStoreLodExtensionName);

    // Set optional features if available.
    const vk::StructureChain availableFeatures
        = physicalDevice.getFeatures2<
            vk::PhysicalDeviceFeatures2,
            vk::PhysicalDeviceVulkan12Features,
            vk::PhysicalDeviceIndexTypeUint8FeaturesKHR>();

    supportDrawIndirectCount = availableFeatures.template get<vk::PhysicalDeviceVulkan12Features>().drawIndirectCount;
    supportUint8Index = availableFeatures.template get<vk::PhysicalDeviceIndexTypeUint8FeaturesKHR>().indexTypeUint8;
#if __APPLE__
    // MoltenVK with Metal Argument Buffer does not work with variable descriptor count.
    // Tracked issue: https://github.com/KhronosGroup/MoltenVK/issues/2343
    // TODO: Remove this workaround when the issue is fixed.
    supportVariableDescriptorCount = false;
#else
    supportVariableDescriptorCount = availableFeatures.template get<vk::PhysicalDeviceVulkan12Features>().descriptorBindingVariableDescriptorCount;
#endif

	const vku::RefHolder queueCreateInfos = Queues::getCreateInfos(physicalDevice, queueFamilies);
    vk::StructureChain createInfo {
        vk::DeviceCreateInfo {
            {},
            queueCreateInfos.get(),
            {},
            extensions,
        },
        vk::PhysicalDeviceFeatures2 { requiredFeatures },
        vk::PhysicalDeviceVulkan11Features{}
            .setShaderDrawParameters(true)
            .setStorageBuffer16BitAccess(true)
            .setUniformAndStorageBuffer16BitAccess(true)
            .setMultiview(true),
        vk::PhysicalDeviceVulkan12Features{}
            .setBufferDeviceAddress(true)
            .setDescriptorIndexing(true)
            .setDescriptorBindingSampledImageUpdateAfterBind(true)
            .setDescriptorBindingVariableDescriptorCount(supportVariableDescriptorCount)
            .setRuntimeDescriptorArray(true)
            .setStorageBuffer8BitAccess(true)
            .setScalarBlockLayout(true)
            .setTimelineSemaphore(true)
            .setShaderInt8(true)
            .setDrawIndirectCount(supportDrawIndirectCount),
        vk::PhysicalDeviceDynamicRenderingFeatures { true },
        vk::PhysicalDeviceSynchronization2Features { true },
        vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT { true },
        vk::PhysicalDeviceIndexTypeUint8FeaturesKHR { supportUint8Index },
#if __APPLE__
        vk::PhysicalDevicePortabilitySubsetFeaturesKHR{}
            .setTriangleFans(true)
            .setImageViewFormatSwizzle(true),
#endif
    };

    // Unlink unsupported features.
    if (!supportUint8Index) {
        createInfo.template unlink<vk::PhysicalDeviceIndexTypeUint8FeaturesKHR>();
    }

    vk::raii::Device device { physicalDevice, createInfo.get() };
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*device);
    return device;
}

vma::Allocator vk_gltf_viewer::vulkan::Gpu::createAllocator(const vk::raii::Instance &instance) const {
    return vma::createAllocator(vma::AllocatorCreateInfo {
        vma::AllocatorCreateFlagBits::eBufferDeviceAddress,
        *physicalDevice, *device,
        {}, {}, {}, {},
        vku::unsafeAddress(vma::VulkanFunctions{
            instance.getDispatcher()->vkGetInstanceProcAddr,
            device.getDispatcher()->vkGetDeviceProcAddr,
        }),
        *instance, vk::makeApiVersion(0, 1, 2, 0),
    });
}