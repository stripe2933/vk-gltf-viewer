export module vku:descriptors.DescriptorSets;

import std;
export import vulkan_hpp;
export import :descriptors.DescriptorSetLayouts;
import :details.concepts;
import :details.ranges;
import :utils;

namespace vku {
    export template <concepts::derived_from_value_specialization_of<DescriptorSetLayouts> Layouts>
    class DescriptorSets : public std::array<vk::DescriptorSet, Layouts::setCount> {
    public:
        DescriptorSets(
            vk::Device device,
            vk::DescriptorPool descriptorPool,
            const Layouts &layouts [[clang::lifetimebound]]
        ) : std::array<vk::DescriptorSet, Layouts::setCount> { device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo {
                descriptorPool,
                vku::unsafeProxy(layouts.getHandles()),
            }) | ranges::to_array<Layouts::setCount>() },
            _layouts { std::cref(layouts) } { }
        DescriptorSets(const DescriptorSets&) = delete;
        DescriptorSets(DescriptorSets&&) noexcept = default;
        DescriptorSets& operator=(const DescriptorSets&) = delete;
        DescriptorSets& operator=(DescriptorSets&&) noexcept = default;

        template <std::uint32_t Set, std::uint32_t Binding>
        [[nodiscard]] auto getDescriptorWrite() const -> vk::WriteDescriptorSet {
            return {
                get<Set>(*this), // Error in here: you specify set index that exceeds the number of descriptor set layouts.
                Binding,
                0,
                {},
                get<Binding>(get<Set>(_layouts.get().layoutBindingsPerSet)).descriptorType, // Error in here: you specify binding index that exceeds the number of layout bindings in the set.
            };
        }

    private:
        std::reference_wrapper<const Layouts> _layouts;
    };
}