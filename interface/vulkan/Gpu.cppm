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

            /**
             * @brief <tt>true</tt> If different format for multisampled depth/stencil attachment image and
             * its resolve image is not supported, <tt>false</tt> otherwise.
             * 
             * For example, it is valid that resolving only stencil component of D32_SFLOAT_S8_UINT attachment
             * image into S8_UINT (as the latter have a stencil component with the same number of bits and
             * numeric format) in Vulkan, but some implementation does not support it.
             * 
             * Reported in MoltenVK.
             */
            bool depthStencilResolveDifferentFormat;
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
        bool supportAttachmentFeedbackLoopLayout;
        std::uint32_t subgroupSize;
        std::uint32_t maxPerStageDescriptorUpdateAfterBindSamplers;
        bool supportShaderImageLoadStoreLod;
        bool supportShaderTrinaryMinMax;
        bool supportShaderStencilExport;
        bool supportVariableDescriptorCount;
        bool supportR8SrgbImageFormat;
        bool supportR8G8SrgbImageFormat;
        bool supportS8UintDepthStencilAttachment;

        Workaround workaround;

        Gpu(const vk::raii::Instance &instance LIFETIMEBOUND, vk::SurfaceKHR surface);
        ~Gpu();

    private:
        [[nodiscard]] vk::raii::PhysicalDevice selectPhysicalDevice(const vk::raii::Instance &instance, vk::SurfaceKHR surface) const;
        [[nodiscard]] vk::raii::Device createDevice();
        [[nodiscard]] vma::Allocator createAllocator(const vk::raii::Instance &instance) const;
    };
}