module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.imgui.PlatformResource;

import imgui.vulkan;

export import vk_gltf_viewer.imgui.UserData;
export import vk_gltf_viewer.vulkan.Gpu;
import vk_gltf_viewer.vulkan.texture.Checkerboard;

namespace vk_gltf_viewer::vulkan::imgui {
    export class PlatformResource final : public vk_gltf_viewer::imgui::UserData::PlatformResource {
        texture::Checkerboard checkerboardTexture;

    public:
        explicit PlatformResource(const Gpu &gpu LIFETIMEBOUND);
        ~PlatformResource() override;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::imgui::PlatformResource::PlatformResource(const Gpu &gpu): checkerboardTexture { gpu } {
    checkerboardTextureID = vku::toUint64<vk::DescriptorSet>(ImGui_ImplVulkan_AddTexture(
        *checkerboardTexture.sampler, *checkerboardTexture.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
}

vk_gltf_viewer::vulkan::imgui::PlatformResource::~PlatformResource() {
    ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<vk::DescriptorSet::CType>(checkerboardTextureID));
}
