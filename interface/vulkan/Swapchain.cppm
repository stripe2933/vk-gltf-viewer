module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.Swapchain;

import std;

import vk_gltf_viewer.helpers.ranges;
export import vk_gltf_viewer.vulkan.Gpu;

namespace vk_gltf_viewer::vulkan {
    export struct Swapchain final : vk::raii::SwapchainKHR {
        vk::Extent2D extent;
        std::vector<vk::Image> images;
        std::vector<vk::raii::Semaphore> imageReadySemaphores;

        Swapchain(
            const Gpu &gpu LIFETIMEBOUND,
            const vk::SurfaceKHR &surface LIFETIMEBOUND,
            vk::Extent2D extent,
            vk::SwapchainKHR oldSwapchain = {}
        ) : SwapchainKHR { [&] {
                const vk::SurfaceCapabilitiesKHR capabilities = gpu.physicalDevice.getSurfaceCapabilitiesKHR(surface);

                // Clamp the extent to the allowed range specified by the surface capabilities.
                extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
                extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

                // Calculate the optimal image count for the swapchain.
                std::uint32_t imageCount = capabilities.minImageCount + 1;
                if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
                    imageCount = capabilities.maxImageCount;
                }

                constexpr std::array viewFormats { vk::Format::eB8G8R8A8Srgb, vk::Format::eB8G8R8A8Unorm };

                vk::StructureChain createInfo {
                    vk::SwapchainCreateInfoKHR {
                        {},
                        surface,
                        imageCount,
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
                        true,
                        oldSwapchain,
                    },
                    vk::ImageFormatListCreateInfo { viewFormats },
                };

                if (gpu.supportSwapchainMutableFormat) {
                    createInfo.get().flags = vk::SwapchainCreateFlagBitsKHR::eMutableFormat;
                }
                else {
                    createInfo.unlink<vk::ImageFormatListCreateInfo>();
                }

                return SwapchainKHR { gpu.device, createInfo.get() };
            }() },
            extent { extent },
            images { getImages() },
            imageReadySemaphores { [&] {
                std::vector<vk::raii::Semaphore> result;
                result.reserve(images.size());
                for (auto _ : ranges::views::upto(images.size())) {
                    result.emplace_back(gpu.device, vk::SemaphoreCreateInfo{});
                }
                return result;
            }() } { }
    };
}