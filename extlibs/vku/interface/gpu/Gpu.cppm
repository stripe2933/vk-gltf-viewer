module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vku:gpu.Gpu;

import std;
export import vulkan_hpp;
export import vk_mem_alloc_hpp;
import :details;

namespace vku {
    export template <typename QueueFamilies, typename Queues> requires
        std::constructible_from<Queues, vk::Device, const QueueFamilies&>
        && requires(const QueueFamilies &queueFamilies) {
            { Queues::getDeviceQueueCreateInfos(queueFamilies) } -> std::ranges::contiguous_range;
        }
    class Gpu {
    public:
        struct DefaultQueueFamiliesGetter {
            [[nodiscard]] auto operator()(
                vk::PhysicalDevice physicalDevice
            ) const -> QueueFamilies {
                return QueueFamilies { physicalDevice };
            }
        };

        struct DefaultPhysicalDeviceRater {
            std::span<const char*> requiredExtensions;
            std::variant<std::monostate, const vk::PhysicalDeviceFeatures*> physicalDeviceFeatures;
            std::function<QueueFamilies(vk::PhysicalDevice)> queueFamiliesGetter = DefaultQueueFamiliesGetter{};

            [[nodiscard]] auto operator()(
                vk::PhysicalDevice physicalDevice
            ) const -> std::uint32_t {
                // Check if given physical device supports the required device extensions and features.
                const std::vector availableExtensions = physicalDevice.enumerateDeviceExtensionProperties();
                if (!std::ranges::all_of(requiredExtensions, [&](const char *extensionName) {
                    return std::ranges::any_of(availableExtensions | std::views::transform(&vk::ExtensionProperties::extensionName), [extensionName](const auto &availableExtension) {
                        return std::strcmp(availableExtension.data(), extensionName) == 0;
                    });
                })) {
                    return 0U;
                }

                if (auto *pFeatures = get_if<const vk::PhysicalDeviceFeatures*>(&physicalDeviceFeatures); pFeatures) {
                    const vk::PhysicalDeviceFeatures availableFeatures = physicalDevice.getFeatures();

                    static_assert(sizeof(vk::PhysicalDeviceFeatures) % sizeof(vk::Bool32) == 0,
                        "vk::PhysicalDeviceFeatures must be only consisted of vk::Bool32");
                    const auto* const pFeatureEnables = reinterpret_cast<const vk::Bool32*>(&availableFeatures),
                                      *pFeatureRequests = reinterpret_cast<const vk::Bool32*>(*pFeatures);
                    if (std::ranges::any_of(std::views::iota(0UZ, sizeof(vk::PhysicalDeviceFeatures) / sizeof(vk::Bool32)), [=](std::size_t featureIndex) {
                        // Find a feature that is requested but not available.
                        return pFeatureRequests[featureIndex] && !pFeatureEnables[featureIndex];
                    })) {
                        return 0U;
                    }
                }

                // Check if given physical device has required queue families.
                try {
                    const QueueFamilies queueFamilies = queueFamiliesGetter(physicalDevice);
                    (void)queueFamilies;
                }
                catch (...) {
                    return 0U;
                }

                // Rate physical device based on its properties.
                std::uint32_t score = 0;
                const vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
                if (properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
                    score += 1000;
                }
                score += properties.limits.maxImageDimension2D;
                return score;
            }
        };

        template <concepts::tuple_like PNextsTuple = std::tuple<>>
        struct Config {
            std::vector<const char*> extensions;
            // If PNextsTuple has vk::PhysicalDeviceFeatures2 alternative, then physicalDeviceFeatures field set to std::monostate to pretend it is not exists.
            // https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkDeviceCreateInfo.html#VUID-VkDeviceCreateInfo-pNext-00373
            [[no_unique_address]] std::conditional_t<concepts::alternative_of<vk::PhysicalDeviceFeatures2, PNextsTuple>, std::monostate, vk::PhysicalDeviceFeatures> physicalDeviceFeatures;
            std::function<QueueFamilies(vk::PhysicalDevice)> queueFamiliesGetter = DefaultQueueFamiliesGetter{};
            std::function<std::uint32_t(vk::PhysicalDevice)> physicalDeviceRater = DefaultPhysicalDeviceRater { extensions, &physicalDeviceFeatures, queueFamiliesGetter };
            PNextsTuple pNexts = std::tuple{};
            vma::AllocatorCreateFlags allocatorCreateFlags = {};
        };

        vk::raii::PhysicalDevice physicalDevice;
        QueueFamilies queueFamilies;
        vk::raii::Device device;
        Queues queues { *device, queueFamilies };
        vma::Allocator allocator;

        template <typename... ConfigArgs>
        explicit Gpu(
            const vk::raii::Instance &instance,
            Config<ConfigArgs...> config = Config<>{}
        ) : physicalDevice { selectPhysicalDevice(instance, config) },
            queueFamilies { std::invoke(config.queueFamiliesGetter, *physicalDevice) },
            device { createDevice(config) },
            allocator { createAllocator(*instance, config) } {
#if VULKAN_HPP_DISPATCH_LOADER_DYNAMIC == 1
            VULKAN_HPP_DEFAULT_DISPATCHER.init(*device);
#endif
        }

        ~Gpu() {
            allocator.destroy();
        }

        template <typename... ConfigArgs>
        [[nodiscard]] auto selectPhysicalDevice(
            const vk::raii::Instance &instance,
            const Config<ConfigArgs...> &config
        ) const -> decltype(physicalDevice) {
            const std::vector physicalDevices = instance.enumeratePhysicalDevices();
            vk::raii::PhysicalDevice bestPhysicalDevice
                = *std::ranges::max_element(physicalDevices, {}, [&](const vk::raii::PhysicalDevice &physicalDevice) {
                    return std::invoke(config.physicalDeviceRater, *physicalDevice);
                });
            if (std::invoke(config.physicalDeviceRater, *bestPhysicalDevice) == 0U) {
                throw std::runtime_error { "No adequate physical device" };
            }

            return bestPhysicalDevice;
        }

        template <typename... ConfigArgs>
        [[nodiscard]] auto createDevice(
            Config<ConfigArgs...> &config
        ) const -> decltype(device) {
            const auto queueCreateInfos = Queues::getDeviceQueueCreateInfos(queueFamilies);
            return { physicalDevice, std::apply([&](const auto &...args) {
                return vk::StructureChain { vk::DeviceCreateInfo {
                    {},
                    queueCreateInfos,
                    {},
                    config.extensions,
                    [&]() -> const vk::PhysicalDeviceFeatures* {
                        return std::convertible_to<decltype(config.physicalDeviceFeatures), vk::PhysicalDeviceFeatures>
                            ? &config.physicalDeviceFeatures
                            : nullptr;
                    }()
                }, args... };
            }, config.pNexts).get() };
        }

        template <typename... ConfigArgs>
        [[nodiscard]] auto createAllocator(
            vk::Instance instance,
            Config<ConfigArgs...> &config
        ) const -> decltype(allocator) {
            return vma::createAllocator(vma::AllocatorCreateInfo {
                config.allocatorCreateFlags,
                *physicalDevice,
                *device,
                {}, {}, {}, {}, {},
                instance,
                vk::makeApiVersion(0, 1, 0, 0),
            });
        }
    };
}