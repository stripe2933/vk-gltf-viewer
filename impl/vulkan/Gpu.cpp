module;

#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer.vulkan.Gpu;

import vk_gltf_viewer.helpers.ranges;

constexpr std::array requiredExtensions {
#if __APPLE__
    vk::KHRPortabilitySubsetExtensionName,
    vk::KHRCopyCommands2ExtensionName,
    vk::KHRFormatFeatureFlags2ExtensionName,
    vk::EXTHostImageCopyExtensionName,
    vk::EXTMetalObjectsExtensionName,
#endif
    vk::KHRDynamicRenderingExtensionName,
    vk::KHRSynchronization2ExtensionName,
    vk::EXTExtendedDynamicStateExtensionName,
    vk::KHRPushDescriptorExtensionName,
    vk::KHRSwapchainExtensionName,
};

constexpr std::array optionalExtensions {
    vk::KHRSwapchainMutableFormatExtensionName,
    vk::KHRIndexTypeUint8ExtensionName,
    vk::AMDShaderImageLoadStoreLodExtensionName,
    vk::EXTAttachmentFeedbackLoopLayoutExtensionName,
    vk::EXTShaderStencilExportExtensionName,
    vk::EXTExtendedDynamicState3ExtensionName,
};

constexpr vk::PhysicalDeviceFeatures requiredFeatures = vk::PhysicalDeviceFeatures{}
    .setDrawIndirectFirstInstance(true)
    .setSamplerAnisotropy(true)
    .setShaderInt16(true)
    .setMultiDrawIndirect(true)
    .setShaderStorageImageWriteWithoutFormat(true)
    .setIndependentBlend(true)
    .setFragmentStoresAndAtomics(true);

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
    const auto [props2, subgroupProps, descriptorIndexingProps] = physicalDevice.getProperties2<
        vk::PhysicalDeviceProperties2,
        vk::PhysicalDeviceSubgroupProperties,
        vk::PhysicalDeviceDescriptorIndexingProperties>();
    subgroupSize = subgroupProps.subgroupSize;
    maxPerStageDescriptorUpdateAfterBindSamplers = descriptorIndexingProps.maxPerStageDescriptorUpdateAfterBindSamplers;

	// Retrieve physical device memory properties.
	const vk::PhysicalDeviceMemoryProperties memoryProperties = physicalDevice.getMemoryProperties();
    isUmaDevice = memoryProperties.memoryHeapCount == 1;

    // Some vendor-specific workarounds.
    switch (props2.properties.vendorID) {
        case 0x8086: // Intel
            workaround.attachmentLessRenderPass = true;
            break;
        case 0x106B: // MoltenVK
            workaround.attachmentLessRenderPass = true;
            workaround.depthStencilResolveDifferentFormat = true;
            break;
    }
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
        const auto [features2, vulkan11Features, vulkan12Features, dynamicRenderingFeatures, synchronization2Features, extendedDynamicStateFeatures]
            = physicalDevice.getFeatures2<
                vk::PhysicalDeviceFeatures2,
                vk::PhysicalDeviceVulkan11Features,
                vk::PhysicalDeviceVulkan12Features,
                vk::PhysicalDeviceDynamicRenderingFeatures,
                vk::PhysicalDeviceSynchronization2Features,
                vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
        if (!features2.features.drawIndirectFirstInstance ||
            !features2.features.samplerAnisotropy ||
            !features2.features.shaderInt16 ||
            !features2.features.multiDrawIndirect ||
            !features2.features.shaderStorageImageWriteWithoutFormat ||
            !features2.features.independentBlend ||
            !features2.features.fragmentStoresAndAtomics ||
            !vulkan11Features.shaderDrawParameters ||
            !vulkan11Features.storageBuffer16BitAccess ||
            !vulkan11Features.uniformAndStorageBuffer16BitAccess ||
            !vulkan11Features.multiview ||
            !vulkan12Features.bufferDeviceAddress ||
            !vulkan12Features.descriptorIndexing ||
            !vulkan12Features.descriptorBindingSampledImageUpdateAfterBind ||
            !vulkan12Features.descriptorBindingVariableDescriptorCount ||
            !vulkan12Features.runtimeDescriptorArray ||
            !vulkan12Features.separateDepthStencilLayouts ||
            !vulkan12Features.storageBuffer8BitAccess ||
            !vulkan12Features.scalarBlockLayout ||
            !vulkan12Features.timelineSemaphore ||
            !vulkan12Features.shaderInt8 ||
            !dynamicRenderingFeatures.dynamicRendering ||
            !synchronization2Features.synchronization2 ||
            !extendedDynamicStateFeatures.extendedDynamicState) {
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
    supportShaderTrinaryMinMax = availableExtensionNames.contains(vk::AMDShaderTrinaryMinmaxExtensionName);
    supportAttachmentFeedbackLoopLayout = availableExtensionNames.contains(vk::EXTAttachmentFeedbackLoopLayoutExtensionName);
    supportShaderStencilExport = availableExtensionNames.contains(vk::EXTShaderStencilExportExtensionName);

    // Set optional features if available.
    const auto [_, vulkan12Features, indexTypeUint8Features] = physicalDevice.getFeatures2<
        vk::PhysicalDeviceFeatures2,
        vk::PhysicalDeviceVulkan12Features,
        vk::PhysicalDeviceIndexTypeUint8FeaturesKHR>();

    supportDrawIndirectCount = vulkan12Features.drawIndirectCount;
#if __APPLE__
    // MoltenVK supports VK_KHR_index_type_uint8 from v1.3.0 by dynamically generating 16-bit indices from 8-bit indices
    // using Metal compute command encoder, therefore it breaks the render pass and has performance defect. Since the
    // application already has CPU index conversion path, disable it.
    supportUint8Index = false;

    // MoltenVK with Metal Argument Buffer does not work with variable descriptor count.
    // Tracked issue: https://github.com/KhronosGroup/MoltenVK/issues/2343
    // TODO: Remove this workaround when the issue is fixed.
    supportVariableDescriptorCount = false;
#else
    supportUint8Index = indexTypeUint8Features.indexTypeUint8;
    supportVariableDescriptorCount = vulkan12Features.descriptorBindingVariableDescriptorCount;
#endif

    if (availableExtensionNames.contains(vk::EXTExtendedDynamicStateExtensionName)) {
        const auto [_, extendedDynamicState3Props] = physicalDevice.getProperties2<
            vk::PhysicalDeviceProperties2,
            vk::PhysicalDeviceExtendedDynamicState3PropertiesEXT>();
        supportDynamicPrimitiveTopologyUnrestricted = extendedDynamicState3Props.dynamicPrimitiveTopologyUnrestricted;
    }

    constexpr vk::FormatFeatureFlags requiredFormatFeatureFlags
        = vk::FormatFeatureFlagBits::eTransferDst
        | vk::FormatFeatureFlagBits::eTransferSrc
        | vk::FormatFeatureFlagBits::eBlitSrc
        | vk::FormatFeatureFlagBits::eBlitDst
        | vk::FormatFeatureFlagBits::eSampledImage;
    supportR8SrgbImageFormat = vku::contains(physicalDevice.getFormatProperties(vk::Format::eR8Srgb).optimalTilingFeatures, requiredFormatFeatureFlags);
    supportR8G8SrgbImageFormat = vku::contains(physicalDevice.getFormatProperties(vk::Format::eR8G8Srgb).optimalTilingFeatures, requiredFormatFeatureFlags);
    supportS8UintDepthStencilAttachment = vku::contains(physicalDevice.getFormatProperties(vk::Format::eS8Uint).optimalTilingFeatures, vk::FormatFeatureFlagBits::eDepthStencilAttachment);

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
            .setSeparateDepthStencilLayouts(true)
            .setStorageBuffer8BitAccess(true)
            .setScalarBlockLayout(true)
            .setTimelineSemaphore(true)
            .setShaderInt8(true)
            .setDrawIndirectCount(supportDrawIndirectCount),
        vk::PhysicalDeviceDynamicRenderingFeatures { true },
        vk::PhysicalDeviceSynchronization2Features { true },
        vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT { true },
        vk::PhysicalDeviceIndexTypeUint8FeaturesKHR { supportUint8Index },
        vk::PhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT { supportAttachmentFeedbackLoopLayout },
#if __APPLE__
        vk::PhysicalDevicePortabilitySubsetFeaturesKHR{}
            .setTriangleFans(true)
            .setImageViewFormatSwizzle(true),
        vk::PhysicalDeviceHostImageCopyFeatures { true },
#endif
    };

    // Unlink unsupported features.
    if (!supportUint8Index) {
        createInfo.template unlink<vk::PhysicalDeviceIndexTypeUint8FeaturesKHR>();
    }
    if (!supportAttachmentFeedbackLoopLayout) {
        createInfo.template unlink<vk::PhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT>();
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