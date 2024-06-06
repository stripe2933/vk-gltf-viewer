module;

#include <ranges>
#include <vector>

export module vku:descriptors.DescriptorSets;

export import vulkan_hpp;
export import :descriptors.DescriptorSetLayouts;
import :details.concepts;
import :details.ranges;

#define INDEX_SEQ(Is, N, ...) [&]<std::size_t ...Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define ARRAY_OF(N, ...) INDEX_SEQ(Is, N, { return std::array { (Is, __VA_ARGS__)... }; })
#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

namespace vku {
    export template <concepts::derived_from_value_specialization_of<DescriptorSetLayouts> Layouts>
    class DescriptorSets : public std::array<vk::DescriptorSet, Layouts::setCount> {
    public:
        DescriptorSets(
            vk::Device device,
            vk::DescriptorPool descriptorPool,
            const Layouts &layouts
        ) : std::array<vk::DescriptorSet, Layouts::setCount> { device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo {
                descriptorPool,
                layouts,
            }) | ranges::to_array<Layouts::setCount>() },
            layouts { layouts } { }

        // For push descriptor usage.
        explicit DescriptorSets(
            const Layouts &layouts
        ) : layouts { layouts } { }

        template <typename Self>
        [[nodiscard]] static auto allocateMultiple(
            vk::Device device,
            vk::DescriptorPool descriptorPool,
            const Layouts &descriptorSetLayouts,
            std::size_t n
        ) -> std::vector<Self> {
            const std::vector multipleSetLayouts
                // TODO: 1. Why pipe syntax between std::span and std::views::repeat not works? 2. Why compile-time sized span not works?
                = std::views::repeat(std::span<const vk::DescriptorSetLayout> { descriptorSetLayouts }, n)
                | std::views::join
                | std::ranges::to<std::vector>();

            const std::vector descriptorSets = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo {
                descriptorPool,
                multipleSetLayouts,
            });
            // TODO.CXX23: use std::views::chunk instead, like:
            // return descriptorSets
            //     | std::views::chunk(Layouts::setCount)
            //     | std::views::transform([&](const auto &sets) {
            //         return Self { descriptorSetLayouts, sets | ranges::to_array<Layouts::setCount>() };
            //     })
            //     | std::ranges::to<std::vector>();
            return std::views::iota(0UZ, n)
                | std::views::transform([&](std::size_t i) {
                    return Self {
                        descriptorSetLayouts,
                        std::views::counted(descriptorSets.data() + i * Layouts::setCount, Layouts::setCount)
                            | ranges::to_array<Layouts::setCount>(),
                    };
                })
                | std::ranges::to<std::vector>();
        }

    protected:
        template <std::uint32_t Set, std::uint32_t Binding>
        [[nodiscard]] auto getDescriptorWrite() const -> vk::WriteDescriptorSet {
            return {
                get<Set>(*this), // Error in here: you specify set index that exceeds the number of descriptor set layouts.
                Binding,
                0,
                {},
                get<Binding>(get<Set>(layouts.setLayouts)).descriptorType, // Error in here: you specify binding index that exceeds the number of layout bindings in the set.
            };
        }

    private:
        const Layouts &layouts;

        // For allocateMultiple.
        DescriptorSets(
            const Layouts &layouts,
            const std::array<vk::DescriptorSet, Layouts::setCount> &descriptorSets
        ) : std::array<vk::DescriptorSet, Layouts::setCount> { descriptorSets },
            layouts { layouts } { }
    };
}