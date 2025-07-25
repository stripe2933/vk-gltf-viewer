module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.Swapchain;

import std;

import vk_gltf_viewer.helpers.ranges;
export import vk_gltf_viewer.vulkan.Gpu;

namespace vk_gltf_viewer::vulkan {
    export class Swapchain {
        std::reference_wrapper<const Gpu> gpu;
        vk::SurfaceKHR surface;
        
    public:
        vk::Extent2D extent;
        vk::raii::SwapchainKHR swapchain;
        std::vector<vk::Image> images;
        std::vector<vk::raii::ImageView> imageViews;

        std::vector<vk::raii::Semaphore> imageReadySemaphores;

        Swapchain(const Gpu &gpu LIFETIMEBOUND, vk::SurfaceKHR surface LIFETIMEBOUND, const vk::Extent2D &extent);

        [[nodiscard]] explicit operator const vk::SwapchainKHR&() const & noexcept;

        void setExtent(const vk::Extent2D &extent);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

constexpr vk::SemaphoreCreateInfo semaphoreCreateInfo{};

[[nodiscard]] std::uint32_t getImageCount(const vk::SurfaceCapabilitiesKHR &capabilities) noexcept {
    std::uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    return imageCount;
}

[[nodiscard]] vk::raii::SwapchainKHR createSwapchain(
    const vk::raii::Device &device,
    vk::SurfaceKHR surface,
    const vk::SurfaceCapabilitiesKHR &capabilities,
    const vk::Extent2D &extent,
    bool useMutableFormat,
    vk::SwapchainKHR oldSwapchain = {}
) {
    const auto viewFormats = { vk::Format::eB8G8R8A8Srgb, vk::Format::eB8G8R8A8Unorm };
    vk::StructureChain createInfo {
        vk::SwapchainCreateInfoKHR {
            {},
            surface,
            getImageCount(capabilities),
            vk::Format::eB8G8R8A8Srgb,
            vk::ColorSpaceKHR::eSrgbNonlinear,
            extent,
            1,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eColorAttachment,
            vk::SharingMode::eExclusive,
            {},
            capabilities.currentTransform,
            vk::CompositeAlphaFlagBitsKHR::eOpaque,
            vk::PresentModeKHR::eFifo,
            false,
            oldSwapchain,
        },
        vk::ImageFormatListCreateInfo { viewFormats },
    };

    if (useMutableFormat) {
        createInfo.get().flags = vk::SwapchainCreateFlagBitsKHR::eMutableFormat;
    }
    else {
        createInfo.unlink<vk::ImageFormatListCreateInfo>();
    }

    return { device, createInfo.get() };
}

vk_gltf_viewer::vulkan::Swapchain::Swapchain(const Gpu &gpu, vk::SurfaceKHR surface, const vk::Extent2D &extent)
    : gpu { gpu }
    , surface { surface }
    , extent { extent }
    , swapchain { createSwapchain(gpu.device, surface, gpu.physicalDevice.getSurfaceCapabilitiesKHR(surface), extent, gpu.supportSwapchainMutableFormat) }
    , images { swapchain.getImages() } {
    imageReadySemaphores.reserve(images.size());
    for (auto _ : ranges::views::upto(images.size())) {
        imageReadySemaphores.emplace_back(gpu.device, semaphoreCreateInfo);
    }
}

vk_gltf_viewer::vulkan::Swapchain::operator const vk::SwapchainKHR&() const & noexcept {
    return *swapchain;
}

void vk_gltf_viewer::vulkan::Swapchain::setExtent(const vk::Extent2D &extent) {
    if (this->extent == extent) return;

    this->extent = extent;
    swapchain = createSwapchain(
        gpu.get().device,
        surface,
        gpu.get().physicalDevice.getSurfaceCapabilitiesKHR(surface),
        extent,
        gpu.get().supportSwapchainMutableFormat,
        *swapchain);
    images = swapchain.getImages();

    imageReadySemaphores.clear();
    imageReadySemaphores.reserve(images.size());
    for (auto _ : ranges::views::upto(images.size())) {
        imageReadySemaphores.emplace_back(gpu.get().device, semaphoreCreateInfo);
    }
}