module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.imgui.PlatformResource;

import imgui.vulkan;
export import :imgui.UserData;
export import :vulkan.Gpu;
import :vulkan.texture.Checkerboard;

namespace vk_gltf_viewer::vulkan::imgui {
    export class PlatformResource final : public vk_gltf_viewer::imgui::UserData::PlatformResource {
        texture::Checkerboard checkerboardTexture;

    public:
        explicit PlatformResource(const Gpu &gpu LIFETIMEBOUND)
            : checkerboardTexture { gpu } {
            checkerboardTextureID = vku::toUint64<vk::DescriptorSet>(ImGui_ImplVulkan_AddTexture(
                *checkerboardTexture.sampler, *checkerboardTexture.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        }

        ~PlatformResource() override {
            ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<vk::DescriptorSet::CType>(checkerboardTextureID));
        }
    };
}