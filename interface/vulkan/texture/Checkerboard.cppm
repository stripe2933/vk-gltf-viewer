module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.texture.Checkerboard;

import std;
export import :vulkan.Gpu;

constexpr unsigned char data[] = {
    0x52, 0x52, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x52, 0x52, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xb4, 0xb4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xb4, 0xb4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x52, 0x52, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x52, 0x52, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xb4, 0xb4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb4, 0xb4, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xb4, 0xb4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xb4, 0xb4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x52, 0x52, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x52, 0x52, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xb4, 0xb4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb4, 0xb4, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x52, 0x52, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x52, 0x52, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

namespace vk_gltf_viewer::vulkan::texture {
    export struct Checkerboard {
        vku::AllocatedImage image;
        vk::raii::ImageView imageView;
        vk::raii::Sampler sampler;

        explicit Checkerboard(const Gpu &gpu LIFETIMEBOUND)
            : image { gpu.allocator, vk::ImageCreateInfo {
                {},
                vk::ImageType::e2D,
                vk::Format::eBc4UnormBlock,
                { 16, 16, 1 },
                1, 1,
                vk::SampleCountFlagBits::e1,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            } }
            , imageView { gpu.device, image.getViewCreateInfo().setComponents({ vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eOne }) }
            , sampler { gpu.device, vk::SamplerCreateInfo{}.setMaxLod(vk::LodClampNone) } {
            const vku::MappedBuffer stagingBuffer {
                gpu.allocator,
                std::from_range, std::span { data },
                vk::BufferUsageFlagBits::eTransferSrc,
            };

            vk::raii::CommandPool graphicsCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.graphicsPresent } };
            vk::raii::Fence fence { gpu.device, vk::FenceCreateInfo{} };
            vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
                cb.pipelineBarrier(
                    vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
                    {}, {}, {},
                    vk::ImageMemoryBarrier {
                        {}, vk::AccessFlagBits::eTransferWrite,
                        {}, gpu.workaround.generalOr(vk::ImageLayout::eTransferDstOptimal),
                        vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                        image, vku::fullSubresourceRange(),
                    });

                cb.copyBufferToImage(stagingBuffer, image, gpu.workaround.generalOr(vk::ImageLayout::eTransferDstOptimal), vk::BufferImageCopy {
                    0, 0, 0,
                    { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
                    {}, image.extent,
                });

                if (!gpu.workaround.noImageLayoutAndQueueFamilyOwnership) {
                    cb.pipelineBarrier(
                        vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe,
                        {}, {}, {},
                        vk::ImageMemoryBarrier {
                            vk::AccessFlagBits::eTransferWrite, {},
                            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                            vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                            image, vku::fullSubresourceRange(),
                        });
                }
            }, *fence);
            std::ignore = gpu.device.waitForFences(*fence, true, ~0ULL);
        }
    };
}