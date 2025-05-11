module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.Gpu;

import std;
export import vku;

namespace vk_gltf_viewer::vulkan {
    export struct QueueFamilies {
        std::uint32_t compute, graphicsPresent, transfer;
        std::vector<std::uint32_t> uniqueIndices;

        QueueFamilies(vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface);
    };

    export struct Queues {
        vk::Queue compute, graphicsPresent, transfer;

        Queues(vk::Device device, const QueueFamilies& queueFamilies) noexcept;

        [[nodiscard]] static vku::RefHolder<std::vector<vk::DeviceQueueCreateInfo>> getCreateInfos(
            vk::PhysicalDevice,
            const QueueFamilies &queueFamilies
        ) noexcept;
    };

    export class Gpu {
    public:
        struct Workaround {
            /**
             * @brief <tt>true</tt> if attachment-less render pass is not supported, <tt>false</tt> otherwise.
             *
             * Reported crash in MoltenVK and Intel GPU driver.
             */
            bool attachmentLessRenderPass;
        };

        vk::raii::PhysicalDevice physicalDevice;
        QueueFamilies queueFamilies;
        vk::raii::Device device = createDevice();
        Queues queues { *device, queueFamilies };
        vma::Allocator allocator;

        bool isUmaDevice;
        bool supportSwapchainMutableFormat;
        bool supportDrawIndirectCount;
        bool supportUint8Index;
        std::uint32_t subgroupSize;
        std::uint32_t maxPerStageDescriptorUpdateAfterBindSamplers;
        bool supportShaderImageLoadStoreLod;
        bool supportShaderTrinaryMinMax;
        bool supportVariableDescriptorCount;
        bool supportR8SrgbImageFormat;
        bool supportR8G8SrgbImageFormat;

        Workaround workaround;

        Gpu(const vk::raii::Instance &instance LIFETIMEBOUND, vk::SurfaceKHR surface);
        ~Gpu();

    private:
        [[nodiscard]] vk::raii::PhysicalDevice selectPhysicalDevice(const vk::raii::Instance &instance, vk::SurfaceKHR surface) const;
        [[nodiscard]] vk::raii::Device createDevice();
        [[nodiscard]] vma::Allocator createAllocator(const vk::raii::Instance &instance) const;
    };
}