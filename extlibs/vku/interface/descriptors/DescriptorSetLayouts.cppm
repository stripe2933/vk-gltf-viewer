module;

#include <array>
#include <tuple>

export module vku:descriptors.DescriptorSetLayouts;

export import vulkan_hpp;
import :details.nofield;
import :details.type_traits;

namespace vku {
    export template <std::size_t... BindingCounts>
    class DescriptorSetLayouts : public std::array<vk::DescriptorSetLayout, sizeof...(BindingCounts)> {
    public:
        template <std::size_t BindingCount, bool HasBindingFlags = false>
        struct LayoutBindings {
            vk::DescriptorSetLayoutCreateFlags flags = {};
            std::array<vk::DescriptorSetLayoutBinding, BindingCount> bindings;
#ifndef _MSC_VER
            [[no_unique_address]]
#endif
            std::conditional_t<HasBindingFlags, std::array<vk::DescriptorBindingFlags, BindingCount>, nofield> bindingFlags;
        };

        // Type deductions.
        // TODO: tricky deduction, might be ill-formed...
        template <typename... Ts>
        LayoutBindings(vk::DescriptorSetLayoutCreateFlags, Ts...) -> LayoutBindings<
            type_traits::leading_n<vk::DescriptorSetLayoutBinding, Ts...>,
            std::convertible_to<
                type_traits::last_type<Ts...>,
                std::array<vk::DescriptorBindingFlags, type_traits::leading_n<vk::DescriptorSetLayoutBinding, Ts...>>>>;

        /// Number of descriptor sets in the descriptor set layouts.
        static constexpr std::size_t setCount = sizeof...(BindingCounts);

        /// Number of bindings in the descriptor set at the specified index.
        template <std::size_t SetIndex>
        static constexpr std::size_t bindingCount = get<SetIndex>(std::array { BindingCounts... }); // TODO.CXX26: use pack indexing.

        std::tuple<std::array<vk::DescriptorSetLayoutBinding, BindingCounts>...> setLayouts;

        explicit DescriptorSetLayouts(
            const vk::raii::Device &device,
            const auto &...layoutBindings
        ) : setLayouts { layoutBindings.bindings... },
            raiiLayouts { vk::raii::DescriptorSetLayout { device, getDescriptorSetLayoutCreateInfo(layoutBindings).get() }... } {
            static_cast<std::array<vk::DescriptorSetLayout, setCount>&>(*this)
                = std::apply([&](const auto &...x) { return std::array { *x... }; }, raiiLayouts);
        }

    private:
        std::array<vk::raii::DescriptorSetLayout, setCount> raiiLayouts;

        template <std::size_t BindingCount, bool HasBindingFlags>
        [[nodiscard]] static auto getDescriptorSetLayoutCreateInfo(
            const LayoutBindings<BindingCount, HasBindingFlags> &layoutBindings
        ) noexcept {
            if constexpr (HasBindingFlags) {
                return vk::StructureChain {
                    vk::DescriptorSetLayoutCreateInfo { layoutBindings.flags, layoutBindings.bindings },
                    vk::DescriptorSetLayoutBindingFlagsCreateInfo { layoutBindings.bindingFlags },
                };
            }
            else {
                return vk::StructureChain {
                    vk::DescriptorSetLayoutCreateInfo { layoutBindings.flags, layoutBindings.bindings },
                };
            }
        }
    };
}