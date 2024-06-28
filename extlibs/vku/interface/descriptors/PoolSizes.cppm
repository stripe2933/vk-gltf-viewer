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
        explicit PoolSizes(concepts::derived_from_value_specialization_of<DescriptorSetLayouts> auto const &layouts);
        PoolSizes(const PoolSizes&) = default;
        PoolSizes(PoolSizes&&) noexcept = default;

        // Addition/scalar multiplication operators.
        [[nodiscard]] auto operator+(PoolSizes rhs) const noexcept -> PoolSizes;
        auto operator+=(const PoolSizes &rhs) noexcept -> PoolSizes&;
        [[nodiscard]] auto operator*(std::uint32_t multiplier) const noexcept -> PoolSizes;
        friend auto operator*(std::uint32_t multiplier, const PoolSizes &rhs) noexcept -> PoolSizes;
        auto operator*=(std::uint32_t multiplier) noexcept -> PoolSizes&;

        [[nodiscard]] auto getDescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlags flags = {}) const noexcept -> RefHolder<vk::DescriptorPoolCreateInfo, std::vector<vk::DescriptorPoolSize>>;

    private:
        std::uint32_t setCount;
        std::unordered_map<vk::DescriptorType, std::uint32_t> descriptorTypeCountMap;
    };
}

[[nodiscard]] auto operator*(std::uint32_t multiplier, const vku::PoolSizes &rhs) noexcept -> vku::PoolSizes {
    return rhs * multiplier;
}

// module :private;

vku::PoolSizes::PoolSizes(
    concepts::derived_from_value_specialization_of<DescriptorSetLayouts> auto const &layouts
) : setCount { std::remove_cvref_t<decltype(layouts)>::setCount } {
    std::apply([this](const auto &...layoutBindings){
        const auto accumBindings = [this](std::span<const vk::DescriptorSetLayoutBinding> bindings) {
            for (const auto &binding : bindings) {
                descriptorTypeCountMap[binding.descriptorType] += binding.descriptorCount;
            }
        };
        (accumBindings(layoutBindings), ...);
    }, layouts.layoutBindingsPerSet);
}

auto vku::PoolSizes::operator+(PoolSizes rhs) const noexcept -> PoolSizes {
    rhs.setCount += setCount;
    for (const auto &[type, count] : descriptorTypeCountMap) {
        rhs.descriptorTypeCountMap[type] += count;
    }
    return rhs;
}

auto vku::PoolSizes::operator+=(const PoolSizes &rhs) noexcept -> PoolSizes & {
    setCount += rhs.setCount;
    for (const auto &[type, count] : rhs.descriptorTypeCountMap) {
        descriptorTypeCountMap[type] += count;
    }
    return *this;
}

auto vku::PoolSizes::operator*(std::uint32_t multiplier) const noexcept -> PoolSizes {
    PoolSizes result { *this };
    result.setCount *= multiplier;
    for (std::uint32_t &count : result.descriptorTypeCountMap | std::views::values) {
        count *= multiplier;
    }
    return result;
}

auto vku::PoolSizes::operator*=(std::uint32_t multiplier) noexcept -> PoolSizes & {
    setCount *= multiplier;
    for (std::uint32_t &count : descriptorTypeCountMap | std::views::values) {
        count *= multiplier;
    }
    return *this;
}

auto vku::PoolSizes::getDescriptorPoolCreateInfo(
    vk::DescriptorPoolCreateFlags flags
) const noexcept -> RefHolder<vk::DescriptorPoolCreateInfo, std::vector<vk::DescriptorPoolSize>> {
    return RefHolder {
        [=, this](std::span<const vk::DescriptorPoolSize> poolSizes) {
            return vk::DescriptorPoolCreateInfo {
                flags,
                setCount,
                // TODO: directly passing poolSizes (automatically converted to vk::ArrayProxyNoTemporaries) causes a compiler error for MSVC,
                // looks like C++20 module bug. Uncomment the following line when fixed.
                // poolSizes,
                static_cast<std::uint32_t>(poolSizes.size()), poolSizes.data(),
            };
        },
        descriptorTypeCountMap
            | std::views::transform([](const auto &keyValue) {
                return vk::DescriptorPoolSize { keyValue.first, keyValue.second };
            })
            | std::ranges::to<std::vector<vk::DescriptorPoolSize>>(),
    };
}
