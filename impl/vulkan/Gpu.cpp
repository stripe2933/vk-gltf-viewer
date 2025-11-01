module;

#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer.vulkan.Gpu;

import vk_gltf_viewer.helpers.ranges;
import vk_gltf_viewer.vulkan.vendor;

#ifdef _MSC_VER
// FIXME: MSVC is not recognizing vk::StructureChain as a tuple-like type. Remove it when fixed.

template <typename... Ts>
struct std::tuple_size<vk::StructureChain<Ts...>> : std::integral_constant<std::size_t, sizeof...(Ts)> {};

template <std::size_t I, typename... Ts>
struct std::tuple_element<I, vk::StructureChain<Ts...>> : std::tuple_element<I, std::tuple<Ts...>> {};
#endif

constexpr std::array requiredExtensions {
#if __APPLE__
    vk::KHRPortabilitySubsetExtensionName,
    vk::EXTHostImageCopyExtensionName,
    vk::EXTMetalObjectsExtensionName,
#endif
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
    .setDepthClamp(true)
    .setDrawIndirectFirstInstance(true)
    .setMultiViewport(true)
    .setSamplerAnisotropy(true)
    .setMultiDrawIndirect(true)
    .setShaderStorageImageWriteWithoutFormat(true)
    .setIndependentBlend(true)
    .setFragmentStoresAndAtomics(true);

vk_gltf_viewer::vulkan::QueueFamilies::QueueFamilies(vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface) {
    const std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

    compute = [&] -> std::uint32_t {
        for (const auto &[queueFamilyIndex, props] : queueFamilyProperties | ranges::views::enumerate) {
            if (vku::contains(props.queueFlags, vk::QueueFlagBits::eCompute) &&
                !vku::contains(props.queueFlags, vk::QueueFlagBits::eGraphics)) {
                return queueFamilyIndex;
            }
        }

        for (const auto &[queueFamilyIndex, props] : queueFamilyProperties | ranges::views::enumerate) {
            if (vku::contains(props.queueFlags, vk::QueueFlagBits::eCompute)) {
                return queueFamilyIndex;
            }
        }

        // Vulkan always guarantees at least one queue family that supports compute.
        std::unreachable();
    }();
    graphicsPresent = [&] -> std::uint32_t {
        for (const auto &[queueFamilyIndex, props] : queueFamilyProperties | ranges::views::enumerate) {
            if (vku::contains(props.queueFlags, vk::QueueFlagBits::eGraphics)) {
                return queueFamilyIndex;
            }
        }

        throw std::runtime_error { "The GPU does not have graphics capability." };
    }();
    transfer = [&] -> std::uint32_t {
        for (const auto &[queueFamilyIndex, props] : queueFamilyProperties | ranges::views::enumerate) {
            if (vku::contains(props.queueFlags, vk::QueueFlagBits::eTransfer) &&
                !vku::contains(props.queueFlags, vk::QueueFlagBits::eGraphics) &&
                !vku::contains(props.queueFlags, vk::QueueFlagBits::eCompute)) {
                return queueFamilyIndex;
            }
        }

        return compute;
    }();

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
    maxPerStageDescriptorUpdateAfterBindSampledImages = descriptorIndexingProps.maxPerStageDescriptorUpdateAfterBindSampledImages;

	// Retrieve physical device memory properties.
	const vk::PhysicalDeviceMemoryProperties memoryProperties = physicalDevice.getMemoryProperties();
    isUmaDevice = memoryProperties.memoryHeapCount == 1;

    // Some vendor-specific workarounds.
    vendorId = props2.properties.vendorID;
    switch (vendorId) {
        case vendor::INTEL:
            workaround.attachmentLessRenderPass = true;
            break;
        case vendor::MOLTEN_VK:
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
        const auto [features2, vulkan11Features, vulkan12Features, vulkan13Features]
            = physicalDevice.getFeatures2<
                vk::PhysicalDeviceFeatures2,
                vk::PhysicalDeviceVulkan11Features,
                vk::PhysicalDeviceVulkan12Features,
                vk::PhysicalDeviceVulkan13Features>();
        if (!features2.features.depthClamp ||
            !features2.features.drawIndirectFirstInstance ||
            !features2.features.samplerAnisotropy ||
            !features2.features.multiDrawIndirect ||
            !features2.features.multiViewport ||
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
            !vulkan12Features.shaderOutputViewportIndex ||
            !vulkan12Features.storageBuffer8BitAccess ||
            !vulkan12Features.scalarBlockLayout ||
            !vulkan12Features.timelineSemaphore ||
            !vulkan13Features.dynamicRendering ||
            !vulkan13Features.shaderDemoteToHelperInvocation ||
            !vulkan13Features.synchronization2) {
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

    supportShaderBufferInt64Atomics = vulkan12Features.shaderBufferInt64Atomics;
    supportDrawIndirectCount = vulkan12Features.drawIndirectCount;
#if __APPLE__
    // MoltenVK supports VK_KHR_index_type_uint8 from v1.3.0 by dynamically generating 16-bit indices from 8-bit indices
    // using Metal compute command encoder, therefore it breaks the render pass and has performance defect. Since the
    // application already has CPU index conversion path, disable it.
    supportUint8Index = false;
#else
    supportUint8Index = indexTypeUint8Features.indexTypeUint8;
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

    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    queueCreateInfos.reserve(queueFamilies.uniqueIndices.size());

    constexpr float priority = 1.f;
    for (std::uint32_t queueFamilyIndex : queueFamilies.uniqueIndices) {
        queueCreateInfos.push_back({
            {},
            queueFamilyIndex,
            vk::ArrayProxyNoTemporaries<const float> { priority },
        });
    }

    vk::StructureChain createInfo {
        vk::DeviceCreateInfo {
            {},
            queueCreateInfos,
            {},
            extensions,
        },
        vk::PhysicalDeviceFeatures2 {
            vk::PhysicalDeviceFeatures { requiredFeatures }
                .setShaderInt64(supportShaderBufferInt64Atomics),
        },
        vk::PhysicalDeviceVulkan11Features{}
            .setShaderDrawParameters(true)
            .setStorageBuffer16BitAccess(true)
            .setUniformAndStorageBuffer16BitAccess(true)
            .setMultiview(true),
        vk::PhysicalDeviceVulkan12Features{}
            .setBufferDeviceAddress(true)
            .setDescriptorIndexing(true)
            .setDescriptorBindingPartiallyBound(true)
            .setDescriptorBindingSampledImageUpdateAfterBind(true)
            .setDescriptorBindingVariableDescriptorCount(true)
            .setRuntimeDescriptorArray(true)
            .setSeparateDepthStencilLayouts(true)
            .setShaderOutputViewportIndex(true)
            .setStorageBuffer8BitAccess(true)
            .setScalarBlockLayout(true)
            .setTimelineSemaphore(true)
            .setDrawIndirectCount(supportDrawIndirectCount)
            .setShaderBufferInt64Atomics(supportShaderBufferInt64Atomics),
        vk::PhysicalDeviceVulkan13Features{}
            .setDynamicRendering(true)
            .setShaderDemoteToHelperInvocation(true)
            .setSynchronization2(true),
        vk::PhysicalDeviceIndexTypeUint8FeaturesKHR { supportUint8Index },
        vk::PhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT { supportAttachmentFeedbackLoopLayout },
#if __APPLE__
        vk::PhysicalDevicePortabilitySubsetFeaturesKHR{}
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
        &vku::lvalue(vma::VulkanFunctions{
            instance.getDispatcher()->vkGetInstanceProcAddr,
            device.getDispatcher()->vkGetDeviceProcAddr,
        }),
        *instance, vk::makeApiVersion(0, 1, 3, 0),
    });
}