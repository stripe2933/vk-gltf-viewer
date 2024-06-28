module;

#include <cassert>
#include <array>
#include <ranges>
#include <tuple>

export module vku:descriptors.DescriptorSetLayouts;

export import vulkan_hpp;
import :details.ranges;

namespace vku {
    export template <std::size_t... BindingCounts>
    struct DescriptorSetLayouts : std::array<vk::raii::DescriptorSetLayout, sizeof...(BindingCounts)> {
        /// Number of descriptor sets in the descriptor set layouts.
        static constexpr std::size_t setCount = sizeof...(BindingCounts);

        /// Number of bindings in the descriptor set at the specified index.
        template <std::size_t SetIndex>
        static constexpr std::size_t bindingCount = get<SetIndex>(std::array { BindingCounts... }); // TODO.CXX26: use pack indexing.

        std::tuple<std::array<vk::DescriptorSetLayoutBinding, BindingCounts>...> layoutBindingsPerSet;

        template <std::convertible_to<vk::DescriptorSetLayoutCreateInfo>... CreateInfos>
            requires (sizeof...(CreateInfos) == setCount)
        explicit DescriptorSetLayouts(
            const vk::raii::Device &device,
             const CreateInfos&...createInfos
        ) : std::array<vk::raii::DescriptorSetLayout, setCount> { vk::raii::DescriptorSetLayout { device, createInfos }... },
#ifdef _MSC_VER
            // Look's like MSVC C++20 module does not accept the range pipe syntax code in below.
            // TODO: remove #ifdef when the issue fixed.
            layoutBindingsPerSet{ ranges::to_array<BindingCounts>{}(std::views::counted(createInfos.pBindings, BindingCounts))... } {
#else
            layoutBindingsPerSet{ std::views::counted(createInfos.pBindings, BindingCounts) | ranges::to_array<BindingCounts>()... } {
#endif
            assert(((createInfos.bindingCount == BindingCounts) && ...) && "Binding counts mismatch!");
        }

        [[nodiscard]] auto getHandles() const noexcept -> std::array<vk::DescriptorSetLayout, setCount> {
            return std::apply([](const auto &...raiiHandles) {
                return std::array { *raiiHandles... };
            }, static_cast<const std::array<vk::raii::DescriptorSetLayout, setCount>&>(*this));
        }
    };
}