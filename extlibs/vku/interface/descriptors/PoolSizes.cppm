module;

#include <cstdint>
#include <ranges>
#include <span>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>

export module vku:descriptors.PoolSizes;

export import vulkan_hpp;
import :descriptors.DescriptorSetLayouts;
import :details.concepts;
import :utils.RefHolder;

namespace vku {
    export class PoolSizes {
    public:
        PoolSizes() = default;
        explicit PoolSizes(concepts::derived_from_value_specialization_of<DescriptorSetLayouts> auto const &layouts)
            : setCount { std::remove_cvref_t<decltype(layouts)>::setCount } {
            std::apply([this](const auto &...layoutBindings){
                const auto accumBindings = [this](std::span<const vk::DescriptorSetLayoutBinding> bindings) {
                    for (const auto &binding : bindings) {
                        map[binding.descriptorType] += binding.descriptorCount;
                    }
                };
                (accumBindings(layoutBindings), ...);
            }, layouts.layoutBindingsPerSet);
        }
        PoolSizes(const PoolSizes&) = default;
        PoolSizes(PoolSizes&&) noexcept = default;

        [[nodiscard]] auto operator+(PoolSizes rhs) const noexcept -> PoolSizes {
            rhs.setCount += setCount;
            for (const auto &[type, count] : map) {
                rhs.map[type] += count;
            }
            return rhs;
        }
        auto operator+=(const PoolSizes &rhs) noexcept -> PoolSizes & {
            setCount += rhs.setCount;
            for (const auto &[type, count] : rhs.map) {
                map[type] += count;
            }
            return *this;
        }
        [[nodiscard]] auto operator*(std::uint32_t multiplier) const noexcept -> PoolSizes {
            PoolSizes result { *this };
            result.setCount *= multiplier;
            for (std::uint32_t &count : result.map | std::views::values) {
                count *= multiplier;
            }
            return result;
        }
        [[nodiscard]] friend auto operator*(std::uint32_t multiplier, PoolSizes rhs) noexcept -> PoolSizes {
            rhs.setCount *= multiplier;
            for (std::uint32_t &count : rhs.map | std::views::values) {
                count *= multiplier;
            }
            return rhs;
        }
        auto operator*=(std::uint32_t multiplier) noexcept -> PoolSizes & {
            setCount *= multiplier;
            for (std::uint32_t &count : map | std::views::values) {
                count *= multiplier;
            }
            return *this;
        }

        [[nodiscard]] auto asVector() const noexcept -> std::pair<std::uint32_t, std::vector<vk::DescriptorPoolSize>> {
            return std::pair {
                setCount,
                std::vector { std::from_range, map | std::views::transform([](const auto &keyValue) {
                    return vk::DescriptorPoolSize { keyValue.first, keyValue.second };
                }) },
            };
        }

        [[nodiscard]] auto getDescriptorPoolCreateInfo(
            vk::DescriptorPoolCreateFlags flags = {}
        ) const noexcept -> RefHolder<vk::DescriptorPoolCreateInfo, std::vector<vk::DescriptorPoolSize>> {
            auto [setCount, poolSizes] = asVector();
            return RefHolder {
                [flags, setCount = setCount](std::span<const vk::DescriptorPoolSize> poolSizes) {
                    return vk::DescriptorPoolCreateInfo {
                        flags,
                        setCount,
                        // TODO: directly passing poolSizes (automatically converted to vk::ArrayProxyNoTemporaries) causes a compiler error for MSVC,
                        // looks like C++20 module bug. Uncomment the following line when fixed.
                        // poolSizes,
                        static_cast<std::uint32_t>(poolSizes.size()), poolSizes.data(),
                    };
                },
                std::move(poolSizes),
            };
        }

    private:
        std::uint32_t setCount;
        std::unordered_map<vk::DescriptorType, std::uint32_t> map;
    };
}