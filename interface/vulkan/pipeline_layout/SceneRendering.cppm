module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.pl.SceneRendering;

import std;
export import glm;
export import vulkan_hpp;
import vku;
export import :vulkan.dsl.Asset;
export import :vulkan.dsl.ImageBasedLighting;
export import :vulkan.dsl.Scene;

namespace vk_gltf_viewer::vulkan::pl {
    export struct SceneRendering : vk::raii::PipelineLayout {
        struct PushConstant {
            glm::mat4 projectionView;
            glm::vec3 viewPosition;
        };

        SceneRendering(
            const vk::raii::Device &device [[clang::lifetimebound]],
            std::tuple<const dsl::ImageBasedLighting&, const dsl::Asset&, const dsl::Scene&> descriptorSetLayouts [[clang::lifetimebound]]
        ) : PipelineLayout { device, vk::PipelineLayoutCreateInfo {
                {},
                vku::unsafeProxy(std::apply([](const auto &...x) { return std::array { *x... }; }, descriptorSetLayouts)),
                vku::unsafeProxy(vk::PushConstantRange {
                    vk::ShaderStageFlagBits::eAllGraphics,
                    0, sizeof(PushConstant),
                }),
            } } { }

        auto pushConstants(vk::CommandBuffer commandBuffer, const PushConstant &pushConstant) const -> void {
            commandBuffer.pushConstants<PushConstant>(**this, vk::ShaderStageFlagBits::eAllGraphics, 0, pushConstant);
        }
    };
}