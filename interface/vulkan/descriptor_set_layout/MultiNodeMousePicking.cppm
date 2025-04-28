module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.dsl.MultiNodeMousePicking;

#ifdef _MSC_VER
import std;
#endif
export import vku;

namespace vk_gltf_viewer::vulkan::dsl {
    export struct MultiNodeMousePicking : vku::DescriptorSetLayout<vk::DescriptorType::eStorageBuffer> {
        explicit MultiNodeMousePicking(const vk::raii::Device &device LIFETIMEBOUND)
            : DescriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
                {},
                vku::unsafeProxy(getBindings({ 1, vk::ShaderStageFlagBits::eFragment })),
            } } { }
    };
}