module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.ag.ImGui;

import std;

export import vk_gltf_viewer.vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::ag {
    export struct ImGui {
        std::vector<vk::raii::ImageView> swapchainImageViews;

        ImGui(const Gpu &gpu LIFETIMEBOUND, std::span<const vk::Image> swapchainImages);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::ag::ImGui::ImGui(const Gpu &gpu, std::span<const vk::Image> swapchainImages)
    : swapchainImageViews { std::from_range, swapchainImages | std::views::transform([&](vk::Image image) {
        return vk::raii::ImageView { gpu.device, vk::ImageViewCreateInfo {
            {},
            image,
            vk::ImageViewType::e2D,
            gpu.supportSwapchainMutableFormat ? vk::Format::eB8G8R8A8Unorm : vk::Format::eB8G8R8A8Srgb,
            {},
            vku::fullSubresourceRange(vk::ImageAspectFlagBits::eColor),
        } };
    }) } { }