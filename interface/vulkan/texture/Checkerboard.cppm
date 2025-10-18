module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.texture.Checkerboard;

import std;

export import vk_gltf_viewer.vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::texture {
    export struct Checkerboard {
        vku::raii::AllocatedImage image;
        vk::raii::ImageView imageView;
        vk::raii::Sampler sampler;

        explicit Checkerboard(const Gpu &gpu LIFETIMEBOUND);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

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

vk_gltf_viewer::vulkan::texture::Checkerboard::Checkerboard(const Gpu &gpu)
    : image {
        gpu.allocator,
        vk::ImageCreateInfo {
            {},
            vk::ImageType::e2D,
            vk::Format::eBc4UnormBlock,
            { 16, 16, 1 },
            1, 1,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        },
        vma::AllocationCreateInfo {
            {},
            vma::MemoryUsage::eAutoPreferDevice,
        },
    }
    , imageView { gpu.device, image.getViewCreateInfo(vk::ImageViewType::e2D).setComponents({ vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eOne }) }
    , sampler { gpu.device, vk::SamplerCreateInfo{}.setMaxLod(vk::LodClampNone) } {
    vku::raii::AllocatedBuffer stagingBuffer {
        gpu.allocator,
        vk::BufferCreateInfo {
            {},
            sizeof(data),
            vk::BufferUsageFlagBits::eTransferSrc,
        },
        vma::AllocationCreateInfo {
            vma::AllocationCreateFlagBits::eHostAccessSequentialWrite,
            vma::MemoryUsage::eAutoPreferHost,
        },
    };
    gpu.allocator.copyMemoryToAllocation(data, stagingBuffer.allocation, 0, sizeof(data));

    vk::raii::CommandPool graphicsCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.graphicsPresent } };
    vk::raii::Fence fence { gpu.device, vk::FenceCreateInfo{} };
    vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
        cb.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
            {}, {}, {},
            vk::ImageMemoryBarrier {
                {}, vk::AccessFlagBits::eTransferWrite,
                {}, vk::ImageLayout::eTransferDstOptimal,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eColor),
            });

        cb.copyBufferToImage(stagingBuffer, image, vk::ImageLayout::eTransferDstOptimal, vk::BufferImageCopy {
            0, 0, 0,
            { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
            {}, image.extent,
        });

        cb.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe,
            {}, {}, {},
            vk::ImageMemoryBarrier {
                vk::AccessFlagBits::eTransferWrite, {},
                vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eColor),
            });
    }, *fence);
    std::ignore = gpu.device.waitForFences(*fence, true, ~0ULL);
}