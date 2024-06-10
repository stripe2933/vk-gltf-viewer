module;

#include <compare>
#include <filesystem>
#include <span>
#include <stdexcept>

#include <vulkan/vulkan.h>
#include <ktxvulkan.h>

export module vk_gltf_viewer:io.ktxvk.Texture;

export import vulkan_hpp;
export import :io.ktxvk.DeviceInfo;

namespace vk_gltf_viewer::io::ktxvk {
    export class Texture : public ktxVulkanTexture {
    public:
        Texture(
            const std::filesystem::path& path,
            DeviceInfo& deviceInfo,
            vk::ImageTiling tiling = vk::ImageTiling::eOptimal,
            vk::ImageUsageFlags usageFlags = vk::ImageUsageFlagBits::eSampled,
            vk::ImageLayout finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        );
        Texture(
            std::span<const std::byte> data,
            DeviceInfo& deviceInfo,
            vk::ImageTiling tiling = vk::ImageTiling::eOptimal,
            vk::ImageUsageFlags usageFlags = vk::ImageUsageFlagBits::eSampled,
            vk::ImageLayout finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        );
        Texture(const Texture&) = delete;
        Texture(Texture&& src) noexcept;
        auto operator=(const Texture&) -> Texture& = delete;
        auto operator=(Texture&& src) noexcept -> Texture&;
        ~Texture();

        [[nodiscard]] auto getImageViewCreateInfo(
            const vk::ImageSubresourceRange& subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, vk::RemainingMipLevels, 0, vk::RemainingArrayLayers }
        ) const -> vk::ImageViewCreateInfo;

    private:
        std::reference_wrapper<DeviceInfo> deviceInfo;
        bool dangling = false; // Flag that the object is moved into another object. If it is true, the destructor should not call ktxVulkanTexture_Destruct.

        [[nodiscard]] auto upload(
            ktxTexture2* texture,
            vk::ImageTiling tiling,
            vk::ImageUsageFlags usageFlags,
            vk::ImageLayout finalLayout
        ) -> ktx_error_code_e;
    };
}

// module :private;

vk_gltf_viewer::io::ktxvk::Texture::Texture(
    const std::filesystem::path& path,
    DeviceInfo& deviceInfo,
    vk::ImageTiling tiling,
    vk::ImageUsageFlags usageFlags,
    vk::ImageLayout finalLayout
) : deviceInfo { std::ref(deviceInfo) } {
    ktxTexture2* kTexture;
    if (auto result = ktxTexture2_CreateFromNamedFile(path.string().c_str(), KTX_TEXTURE_CREATE_NO_FLAGS, &kTexture); result != KTX_SUCCESS) {
        throw std::runtime_error { std::format("Failed to load KTX texture: {}", ktxErrorString(result)) };
    } else if (result = upload(kTexture, tiling, usageFlags, finalLayout); result != KTX_SUCCESS) {
        ktxTexture_Destroy(ktxTexture(kTexture));
        throw std::runtime_error { std::format("Failed to construct ktxvk::Texture: {}", ktxErrorString(result)) };
    }
    ktxTexture_Destroy(ktxTexture(kTexture));
}

vk_gltf_viewer::io::ktxvk::Texture::Texture(
    std::span<const std::byte> data,
    DeviceInfo& deviceInfo,
    vk::ImageTiling tiling,
    vk::ImageUsageFlags usageFlags,
    vk::ImageLayout finalLayout
) : deviceInfo { std::ref(deviceInfo) } {
    ktxTexture2* kTexture;
    auto result = ktxTexture2_CreateFromMemory(
        reinterpret_cast<const ktx_uint8_t*>(data.data()), data.size(), KTX_TEXTURE_CREATE_NO_FLAGS, &kTexture);
    if (result != KTX_SUCCESS) {
        throw std::runtime_error { std::format("Failed to load KTX texture: {}", ktxErrorString(result)) };
    }
    if (result = upload(kTexture, tiling, usageFlags, finalLayout); result != KTX_SUCCESS) {
        ktxTexture_Destroy(ktxTexture(kTexture));
        throw std::runtime_error { std::format("Failed to construct ktxvk::Texture: {}", ktxErrorString(result)) };
    }
    ktxTexture_Destroy(ktxTexture(kTexture));
}

vk_gltf_viewer::io::ktxvk::Texture::Texture(Texture&& src) noexcept
    : ktxVulkanTexture { src },
      deviceInfo { src.deviceInfo },
      dangling { std::exchange(src.dangling, true) } { }

auto vk_gltf_viewer::io::ktxvk::Texture::operator=(Texture&& src) noexcept -> Texture& {
    if (!dangling) {
        ktxVulkanTexture_Destruct(this, deviceInfo.get().device, nullptr);
    }

    static_cast<ktxVulkanTexture&>(*this) = src;
    deviceInfo = src.deviceInfo;
    dangling = std::exchange(src.dangling, true);
    return *this;
}

vk_gltf_viewer::io::ktxvk::Texture::~Texture() {
    if (!dangling) {
        ktxVulkanTexture_Destruct(this, deviceInfo.get().device, nullptr);
    }
}

auto vk_gltf_viewer::io::ktxvk::Texture::getImageViewCreateInfo(
    const vk::ImageSubresourceRange& subresourceRange
) const -> vk::ImageViewCreateInfo {
    return {
        {},
        static_cast<vk::Image>(image),
        static_cast<vk::ImageViewType>(viewType),
        static_cast<vk::Format>(imageFormat),
        {},
        subresourceRange,
    };
}

auto vk_gltf_viewer::io::ktxvk::Texture::upload(
    ktxTexture2* texture,
    vk::ImageTiling tiling,
    vk::ImageUsageFlags usageFlags,
    vk::ImageLayout finalLayout
) -> ktx_error_code_e {
    return ktxTexture2_VkUploadEx(
        texture, &deviceInfo.get(), this,
        static_cast<VkImageTiling>(tiling), static_cast<VkImageUsageFlags>(usageFlags), static_cast<VkImageLayout>(finalLayout)
    );
}