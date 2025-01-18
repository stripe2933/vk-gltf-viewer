export module vk_gltf_viewer:vulkan.rp.Composition;

#ifdef _MSC_VER
import std;
#endif
import vku;
export import vulkan_hpp;

namespace vk_gltf_viewer::vulkan::rp {
    export struct Composition final : vk::raii::RenderPass {
        explicit Composition(const vk::raii::Device &device [[clang::lifetimebound]])
            : RenderPass { device, vk::RenderPassCreateInfo {
                {},
                vku::unsafeProxy(vk::AttachmentDescription {
                    {},
                    vk::Format::eB8G8R8A8Srgb, vk::SampleCountFlagBits::e1,
                    vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore,
                    {}, {},
                    vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
                }),
                vku::unsafeProxy(vk::SubpassDescription {
                    {},
                    vk::PipelineBindPoint::eGraphics,
                    {},
                    vku::unsafeProxy(vk::AttachmentReference { 0, vk::ImageLayout::eColorAttachmentOptimal }),
                }),
                vku::unsafeProxy({
                    vk::SubpassDependency {
                        vk::SubpassExternal, 0,
                        vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        {}, vk::AccessFlagBits::eColorAttachmentWrite,
                    },
                    vk::SubpassDependency {
                        0, 0,
                        vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,
                        vk::DependencyFlagBits::eByRegion,
                    },
                }),
            } } { }
    };
}