module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.ag.ImGui;

import std;

export import vk_gltf_viewer.vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::ag {
    export struct ImGui {
        std::vector<vk::raii::ImageView> swapchainImageViews;

        ImGui(const Gpu &gpu LIFETIMEBOUND, std::span<const vk::Image> swapchainImages) {
            swapchainImageViews.reserve(swapchainImages.size());
            for (vk::Image image : swapchainImages) {
                swapchainImageViews.emplace_back(gpu.device, vk::ImageViewCreateInfo {
                    {},
                    image,
                    vk::ImageViewType::e2D,
                    gpu.supportSwapchainMutableFormat ? vk::Format::eB8G8R8A8Unorm : vk::Format::eB8G8R8A8Srgb,
                    {},
                    vku::fullSubresourceRange(vk::ImageAspectFlagBits::eColor),
                });
            }
        }
    };
}