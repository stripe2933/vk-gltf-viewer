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
    vk::KHRSwapchainExtensionName,
};

constexpr std::array optionalExtensions {
    vk::KHRSwapchainMutableFormatExtensionName,
    vk::EXTIndexTypeUint8ExtensionName,
};

constexpr vk::PhysicalDeviceFeatures requiredFeatures = vk::PhysicalDeviceFeatures{}
    .setSamplerAnisotropy(true)
    .setShaderInt64(true)
    .setMultiDrawIndirect(true)
    .setShaderStorageImageWriteWithoutFormat(true)
    .setIndependentBlend(true);

vk_gltf_viewer::vulkan::Gpu::Gpu(const vk::raii::Instance &instance, vk::SurfaceKHR surface)
    : physicalDevice { selectPhysicalDevice(instance, surface) }
    , queueFamilies { physicalDevice, surface }
    , allocator { createAllocator(instance) } {
    // Retrieve physical device properties.
    const vk::StructureChain physicalDeviceProperties = physicalDevice.getProperties2<
        vk::PhysicalDeviceProperties2,
        vk::PhysicalDeviceSubgroupProperties>();
    subgroupSize = physicalDeviceProperties.get<vk::PhysicalDeviceSubgroupProperties>().subgroupSize;
}

vk_gltf_viewer::vulkan::Gpu::~Gpu() {
    allocator.destroy();
}

auto vk_gltf_viewer::vulkan::Gpu::selectPhysicalDevice(const vk::raii::Instance &instance, vk::SurfaceKHR surface) const -> vk::raii::PhysicalDevice {
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
            !features.shaderInt64 ||
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
            !vulkan12Features.descriptorBindingStorageImageUpdateAfterBind ||
            !vulkan12Features.runtimeDescriptorArray ||
            !vulkan12Features.storageBuffer8BitAccess ||
            !vulkan12Features.uniformAndStorageBuffer8BitAccess ||
            !vulkan12Features.storagePushConstant8 ||
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

auto vk_gltf_viewer::vulkan::Gpu::createDevice() -> vk::raii::Device {
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

    // Set optional features if available.
    const vk::StructureChain availableFeatures
        = physicalDevice.getFeatures2<
            vk::PhysicalDeviceFeatures2,
            vk::PhysicalDeviceIndexTypeUint8FeaturesKHR>();

    supportTessellationShader = availableFeatures.get<vk::PhysicalDeviceFeatures2>().features.tessellationShader;
    supportUint8Index = availableFeatures.template get<vk::PhysicalDeviceIndexTypeUint8FeaturesKHR>().indexTypeUint8;

    const auto unlinkUnsupportedFeatures = [this](auto strutureChain) {
        if (!supportTessellationShader) {
            strutureChain.template get<vk::PhysicalDeviceFeatures2>().features.tessellationShader = false;
        }
        if (!supportUint8Index) {
            strutureChain.template unlink<vk::PhysicalDeviceIndexTypeUint8FeaturesKHR>();
        }
        return strutureChain;
    };

    vk::raii::Device device { physicalDevice, unlinkUnsupportedFeatures(vk::StructureChain {
        vk::DeviceCreateInfo {
            {},
            Queues::getCreateInfos(physicalDevice, queueFamilies).get(),
            {},
            extensions,
        },
        vk::PhysicalDeviceFeatures2 {
            vk::PhysicalDeviceFeatures{}
                .setSamplerAnisotropy(true)
                .setShaderInt64(true)
                .setMultiDrawIndirect(true)
                .setShaderStorageImageWriteWithoutFormat(true)
                .setIndependentBlend(true)
                .setTessellationShader(supportTessellationShader),
        },
        vk::PhysicalDeviceVulkan11Features{}
            .setShaderDrawParameters(true)
            .setStorageBuffer16BitAccess(true)
            .setUniformAndStorageBuffer16BitAccess(true)
            .setMultiview(true),
        vk::PhysicalDeviceVulkan12Features{}
            .setBufferDeviceAddress(true)
            .setDescriptorIndexing(true)
            .setDescriptorBindingSampledImageUpdateAfterBind(true)
            .setDescriptorBindingStorageImageUpdateAfterBind(true)
            .setRuntimeDescriptorArray(true)
            .setStorageBuffer8BitAccess(true)
            .setUniformAndStorageBuffer8BitAccess(true)
            .setStoragePushConstant8(true)
            .setScalarBlockLayout(true)
            .setTimelineSemaphore(true)
            .setShaderInt8(true),
        vk::PhysicalDeviceDynamicRenderingFeatures { true },
        vk::PhysicalDeviceSynchronization2Features { true },
        vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT { true },
        vk::PhysicalDeviceIndexTypeUint8FeaturesKHR { supportUint8Index },
    }).get() };
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*device);
    return device;
}

auto vk_gltf_viewer::vulkan::Gpu::createAllocator(const vk::raii::Instance &instance) const -> vma::Allocator {
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