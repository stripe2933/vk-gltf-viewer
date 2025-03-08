module;

#include <cassert>

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.ag.Swapchain;

import std;
export import vku;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::ag {
    export struct Swapchain final : vku::AttachmentGroup {
        Swapchain(
            const Gpu &gpu LIFETIMEBOUND,
            const vk::Extent2D &extent,
            std::span<const vk::Image> swapchainImages,
            vk::Format format = vk::Format::eB8G8R8A8Srgb
        ) : AttachmentGroup { extent } {
            assert(format == vk::Format::eB8G8R8A8Srgb || gpu.supportSwapchainMutableFormat
                && "Swapchain Attachment Group with mutable format requested, but not supported by GPU");

            addSwapchainAttachment(gpu.device, swapchainImages, format);
        }
    };
}