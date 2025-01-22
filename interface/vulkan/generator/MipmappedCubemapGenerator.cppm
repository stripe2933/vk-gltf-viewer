module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.generator.MipmappedCubemapGenerator;

import std;
import :helpers.functional;
export import :vulkan.pipeline.CubemapComputer;
export import :vulkan.pipeline.SubgroupMipmapComputer;

namespace vk_gltf_viewer::vulkan::inline generator {
    export class MipmappedCubemapGenerator {
        struct IntermediateResources {
            vk::raii::ImageView eqmapImageView;
            std::vector<vk::raii::ImageView> cubemapMipImageViews; // 0th image view has the full image resource, i-th (i=1..) image view has i-th mipmap.
        };

        const Gpu &gpu;

    public:
        struct Config {
            std::uint32_t cubemapSize = 1024;
            vk::ImageUsageFlags cubemapUsage = {};
        };

        struct Pipelines {
            CubemapComputer cubemapComputer;
            SubgroupMipmapComputer subgroupMipmapComputer;
        };

        vku::AllocatedImage cubemapImage;

        MipmappedCubemapGenerator(
            const Gpu &gpu [[clang::lifetimebound]],
            const Config &config
        ) : gpu { gpu },
            cubemapImage { gpu.allocator, vk::ImageCreateInfo {
                vk::ImageCreateFlagBits::eCubeCompatible,
                vk::ImageType::e2D,
                vk::Format::eR32G32B32A32Sfloat,
                vk::Extent3D { config.cubemapSize, config.cubemapSize, 1 },
                vku::Image::maxMipLevels(config.cubemapSize), 6,
                vk::SampleCountFlagBits::e1,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eStorage | config.cubemapUsage,
            } } { }

        void recordCommands(
            vk::CommandBuffer computeCommandBuffer,
            const Pipelines &pipelines [[clang::lifetimebound]],
            const vku::Image &eqmapImage
        ) {
            // --------------------
            // Allocate Vulkan resources that must not be destroyed until the command buffer execution is finished.
            // --------------------

            intermediateResources = std::make_unique<IntermediateResources>(
                vk::raii::ImageView { gpu.device, eqmapImage.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }) });
            if (gpu.supportShaderImageLoadStoreLod) {
                intermediateResources->cubemapMipImageViews.emplace_back(
                    gpu.device,
                    cubemapImage.getViewCreateInfo(vk::ImageViewType::eCube));
            }
            else {
                intermediateResources->cubemapMipImageViews
                    = cubemapImage.getMipViewCreateInfos(vk::ImageViewType::eCube)
                    | std::views::transform([this](const vk::ImageViewCreateInfo &createInfo) {
                        return vk::raii::ImageView { gpu.device, createInfo };
                    })
                    | std::ranges::to<std::vector>();
            }

            // --------------------
            // Record commands to the command buffer.
            // --------------------

            computeCommandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
                {}, {}, {},
                vk::ImageMemoryBarrier {
                    {}, vk::AccessFlagBits::eShaderWrite,
                    {}, vk::ImageLayout::eGeneral,
                    vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                    cubemapImage, vku::fullSubresourceRange(),
                });

            // Generate cubemap from eqmapImage.
            vku::DescriptorSet<CubemapComputer::DescriptorSetLayout> cubemapComputerSet;
            pipelines.cubemapComputer.compute(
                computeCommandBuffer,
                {
                    cubemapComputerSet.getWriteOne<0>({ {}, *intermediateResources->eqmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal }),
                    cubemapComputerSet.getWriteOne<1>({ {}, *intermediateResources->cubemapMipImageViews[0], vk::ImageLayout::eGeneral}),
                },
                cubemapImage.extent.width);

            computeCommandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                {},
                // Ensure eqmap to cubemap projection finish before generating mipmaps.
                vk::MemoryBarrier { vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead },
                {}, {});

            // Generate cubemapImage mipmaps.
            vku::DescriptorSet<SubgroupMipmapComputer::DescriptorSetLayout> subgroupMipmapComputerSet;
            pipelines.subgroupMipmapComputer.compute(
                computeCommandBuffer,
                subgroupMipmapComputerSet.getWrite<0>(vku::unsafeProxy(
                    intermediateResources->cubemapMipImageViews
                        | std::views::transform([](vk::ImageView view) {
                            return vk::DescriptorImageInfo { {}, view, vk::ImageLayout::eGeneral };
                        })
                        | std::ranges::to<std::vector>())),
                vku::toExtent2D(cubemapImage.extent),
                cubemapImage.mipLevels);
        }

    private:
        std::unique_ptr<IntermediateResources> intermediateResources;
    };
}