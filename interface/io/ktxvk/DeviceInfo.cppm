module;

#include <format>
#include <stdexcept>

#include <vulkan/vulkan.h>
#include <ktxvulkan.h>

export module vk_gltf_viewer:io.ktxvk.DeviceInfo;

export import vulkan_hpp;

namespace vk_gltf_viewer::io::ktxvk {
    export class DeviceInfo : public ktxVulkanDeviceInfo {
    public:
        DeviceInfo(vk::PhysicalDevice physicalDevice, vk::Device device, vk::Queue transferQueue, vk::CommandPool transferCommandPool);
        DeviceInfo(const DeviceInfo&) = delete;
        DeviceInfo(DeviceInfo&&) = delete;
        auto operator=(const DeviceInfo&) -> DeviceInfo& = delete;
        auto operator=(DeviceInfo&&) -> DeviceInfo& = delete;
        ~DeviceInfo();
    };
}

// module :private;

vk_gltf_viewer::io::ktxvk::DeviceInfo::DeviceInfo(
    vk::PhysicalDevice physicalDevice,
    vk::Device device,
    vk::Queue transferQueue,
    vk::CommandPool transferCommandPool
) {
    auto result = ktxVulkanDeviceInfo_Construct(this, physicalDevice, device, transferQueue, transferCommandPool, nullptr);
    if (result != KTX_SUCCESS) {
        throw std::runtime_error { std::format("Failed to construct ktxvk::DeviceInfo: {}", ktxErrorString(result)) };
    }
}

vk_gltf_viewer::io::ktxvk::DeviceInfo::~DeviceInfo() {
    ktxVulkanDeviceInfo_Destruct(this);
}