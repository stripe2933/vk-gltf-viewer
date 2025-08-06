module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.pipeline_layout.MultiNodeMousePicking;

import std;
export import glm;
export import vulkan_hpp;
import vku;

export import vk_gltf_viewer.vulkan.descriptor_set_layout.Asset;
export import vk_gltf_viewer.vulkan.descriptor_set_layout.MultiNodeMousePicking;

namespace vk_gltf_viewer::vulkan::pl {
    export struct MultiNodeMousePicking : vk::raii::PipelineLayout {
        struct PushConstant {
            glm::mat4 projectionView;
        };

        MultiNodeMousePicking(
            const vk::raii::Device &device LIFETIMEBOUND,
            const std::tuple<const dsl::Asset&, const dsl::MultiNodeMousePicking&> &descriptorSetLayouts
        );
    };
}

module :private;

vk_gltf_viewer::vulkan::pl::MultiNodeMousePicking::MultiNodeMousePicking(
    const vk::raii::Device &device,
    const std::tuple<const dsl::Asset&, const dsl::MultiNodeMousePicking&> &descriptorSetLayouts
) : PipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        vku::unsafeProxy({ *get<0>(descriptorSetLayouts), *get<1>(descriptorSetLayouts) }),
        vku::unsafeProxy(vk::PushConstantRange {
            vk::ShaderStageFlagBits::eVertex,
            0, sizeof(PushConstant),
        }),
    } } { }