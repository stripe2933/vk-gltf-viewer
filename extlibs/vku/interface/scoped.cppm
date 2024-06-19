module;

#include <concepts>

export module vku:scoped;

export import vulkan_hpp;

namespace vku {
    export template <std::invocable F>
    auto renderPassScoped(
        vk::CommandBuffer commandBuffer,
        const vk::RenderPassBeginInfo &renderPassBeginInfo,
        vk::SubpassContents subpassContents,
        F &&inner
    ) -> void {
        commandBuffer.beginRenderPass(renderPassBeginInfo, subpassContents);
        inner();
        commandBuffer.endRenderPass();
    }

    export template <std::invocable F>
    auto renderingScoped(
        vk::CommandBuffer commandBuffer,
        const vk::RenderingInfo &renderingInfo,
        F &&inner
    ) -> void {
        commandBuffer.beginRenderingKHR(renderingInfo);
        inner();
        commandBuffer.endRenderingKHR();
    }
}