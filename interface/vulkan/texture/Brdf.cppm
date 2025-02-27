module;

#include <ktx.h>
#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.texture.Brdf;

import std;
import :helpers.functional;
import :helpers.ranges;
export import :vulkan.Gpu;
export import :vulkan.sampler.BrdfLutSampler;

namespace vk_gltf_viewer::vulkan::texture {
    export class Brdf {
    public:
        vku::AllocatedImage image;
        vk::raii::ImageView imageView;
        BrdfLutSampler sampler;

        explicit Brdf(const Gpu &gpu [[clang::lifetimebound]])
            : image { createImage(gpu) }
            , imageView { gpu.device, image.getViewCreateInfo() }
            , sampler { gpu.device } { }

    private:
        [[nodiscard]] static vku::AllocatedImage createImage(const Gpu &gpu) {
            const vk::raii::CommandPool graphicsCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.graphicsPresent } };
            std::variant<vk::CommandPool, vk::raii::CommandPool> transferCommandPool = *graphicsCommandPool;
            if (gpu.queueFamilies.transfer != gpu.queueFamilies.graphicsPresent) {
                transferCommandPool = decltype(transferCommandPool) {
                    std::in_place_type<vk::raii::CommandPool>,
                    gpu.device,
                    vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.transfer },
                };
            }

            // Load BRDF LUT image.
            ktxTexture *texture;
            if (auto result = ktxTexture_CreateFromNamedFile("brdf.ktx2", KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture); result != KTX_SUCCESS) {
                throw std::runtime_error { std::format("Failed to load BRDF LUT image: {}", ktxErrorString(result)) };
            }

            vku::AllocatedImage result { gpu.allocator, vk::ImageCreateInfo {
                {},
                vk::ImageType::e2D,
                vk::Format::eR16G16Unorm,
                vk::Extent3D { texture->baseWidth, texture->baseHeight, 1 },
                1, texture->numLayers,
                vk::SampleCountFlagBits::e1,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            } };

            ktx_size_t offset;
            if (auto result = ktxTexture_GetImageOffset(texture, 0, 0, 0, &offset); result != KTX_SUCCESS) {
                throw std::runtime_error { std::format("Failed to get BRDF LUT image offset: {}", ktxErrorString(result)) };
            }
            const ktx_size_t size = ktxTexture_GetDataSize(texture);

            const vku::MappedBuffer stagingBuffer { gpu.allocator, std::from_range, std::span { ktxTexture_GetData(texture) + offset, size }, vk::BufferUsageFlagBits::eTransferSrc };

            ktxTexture_Destroy(texture);

            const auto [timelineSemaphores, finalWaitValues] = vku::executeHierarchicalCommands(
                gpu.device,
                std::forward_as_tuple(
                    // Transfer staging buffer data to device-local image.
                    vku::ExecutionInfo { [&](vk::CommandBuffer cb) {
                        cb.pipelineBarrier(
                            vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
                            {}, {}, {},
                            vk::ImageMemoryBarrier {
                                {}, vk::AccessFlagBits::eTransferWrite,
                                {}, vk::ImageLayout::eTransferDstOptimal,
                                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                                result, vku::fullSubresourceRange(),
                            });
                        cb.copyBufferToImage(
                            stagingBuffer, result,
                            vk::ImageLayout::eTransferDstOptimal,
                            vk::BufferImageCopy {
                                0, 0, 0,
                                vk::ImageSubresourceLayers { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
                                vk::Offset3D { 0, 0, 0 },
                                result.extent,
                            });

                        // Change image layout to SHADER_READ_ONLY_OPTIMAL and optionally transfer ownership from transfer to graphics queue family.
                        cb.pipelineBarrier(
                            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eAllCommands,
                            {}, {}, {},
                            vk::ImageMemoryBarrier {
                                vk::AccessFlagBits::eTransferWrite, {},
                                vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                                gpu.queueFamilies.transfer, gpu.queueFamilies.graphicsPresent,
                                result, vku::fullSubresourceRange(),
                            });
                    }, visit_as<vk::CommandPool>(transferCommandPool), gpu.queues.transfer }),
                std::forward_as_tuple(
                    // Transfer ownership from transfer to graphics queue family, if necessary.
                    vku::ExecutionInfo { [&](vk::CommandBuffer cb) {
                        if (gpu.queueFamilies.transfer != gpu.queueFamilies.graphicsPresent) {
                            cb.pipelineBarrier(
                                vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe,
                                {}, {}, {},
                                vk::ImageMemoryBarrier {
                                    {}, {},
                                    vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                                    gpu.queueFamilies.transfer, gpu.queueFamilies.graphicsPresent,
                                    result, vku::fullSubresourceRange(),
                                });
                        }
                    }, *graphicsCommandPool, gpu.queues.graphicsPresent }));
            std::ignore = gpu.device.waitSemaphores({
                {},
                vku::unsafeProxy(timelineSemaphores | ranges::views::deref | std::ranges::to<std::vector>()),
                finalWaitValues
            }, ~0ULL);

            return result;
        }
    };
}