module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.Gpu;

import std;
export import vku;

namespace vk_gltf_viewer::vulkan {
    export struct QueueFamilies {
        std::uint32_t compute, graphicsPresent, transfer;

        QueueFamilies(vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface) {
            const std::vector queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
            compute = vku::getComputeSpecializedQueueFamily(queueFamilyProperties)
                .or_else([&]() { return vku::getComputeQueueFamily(queueFamilyProperties); })
                .value();
            graphicsPresent = vku::getGraphicsPresentQueueFamily(physicalDevice, surface, queueFamilyProperties).value();
            transfer = vku::getTransferSpecializedQueueFamily(queueFamilyProperties).value_or(compute);
        }

        [[nodiscard]] auto getUniqueIndices() const noexcept -> std::vector<std::uint32_t> {
            std::vector indices { compute, graphicsPresent, transfer };
            std::ranges::sort(indices);

            const auto ret = std::ranges::unique(indices);
            indices.erase(ret.begin(), ret.end());

            return indices;
        }
    };

    export struct Queues {
        vk::Queue compute, graphicsPresent, transfer;

        Queues(vk::Device device, const QueueFamilies& queueFamilies) noexcept
            : compute { device.getQueue(queueFamilies.compute, 0) }
            , graphicsPresent{ device.getQueue(queueFamilies.graphicsPresent, 0) }
            , transfer { device.getQueue(queueFamilies.transfer, 0) } { }

        [[nodiscard]] static auto getCreateInfos(
            vk::PhysicalDevice,
            const QueueFamilies &queueFamilies
        ) noexcept -> vku::RefHolder<std::vector<vk::DeviceQueueCreateInfo>> {
            return { [&]() {
                static constexpr std::array priorities { 1.f };
                return queueFamilies.getUniqueIndices()
                    | std::views::transform([=](std::uint32_t queueFamilyIndex) {
                        return vk::DeviceQueueCreateInfo {
                            {},
                            queueFamilyIndex,
                            priorities,
                        };
                    })
                    | std::ranges::to<std::vector>();
            } };
        }
    };

    export class Gpu {
    public:
        vk::raii::PhysicalDevice physicalDevice;
        QueueFamilies queueFamilies;
        vk::raii::Device device = createDevice();
        Queues queues { *device, queueFamilies };
        vma::Allocator allocator;

        bool supportSwapchainMutableFormat;
        bool supportTessellationShader;
        bool supportUint8Index;
        std::uint32_t subgroupSize;

        Gpu(const vk::raii::Instance &instance [[clang::lifetimebound]], vk::SurfaceKHR surface);
        ~Gpu();

    private:
        [[nodiscard]] auto selectPhysicalDevice(const vk::raii::Instance &instance, vk::SurfaceKHR surface) const -> vk::raii::PhysicalDevice;
        [[nodiscard]] auto createDevice() -> vk::raii::Device;
        [[nodiscard]] auto createAllocator(const vk::raii::Instance &instance) const -> vma::Allocator;
    };
}