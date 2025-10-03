module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline_layout.BloomApply;

#ifdef _MSC_VER
import std;
#endif
export import vulkan_hpp;
import vku;

export import vk_gltf_viewer.vulkan.descriptor_set_layout.BloomApply;

namespace vk_gltf_viewer::vulkan::pl {
    export struct BloomApply final : vk::raii::PipelineLayout {
        struct PushConstant {
            static constexpr vk::PushConstantRange range = {
                vk::ShaderStageFlagBits::eFragment,
                0, 4,
            };

            float factor;
        };

        BloomApply(const vk::raii::Device &device LIFETIMEBOUND, const dsl::BloomApply &descriptorSetLayout LIFETIMEBOUND);
    };
}

module :private;

vk_gltf_viewer::vulkan::pl::BloomApply::BloomApply(
    const vk::raii::Device &device,
    const dsl::BloomApply &descriptorSetLayout
) : PipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        *descriptorSetLayout,
        PushConstant::range,
    } } { }