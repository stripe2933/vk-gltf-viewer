export module vk_gltf_viewer:vulkan.generator.MipmappedCubemapGenerator;

import std;
export import vku;
export import :vulkan.Gpu;
export import :vulkan.pipeline.CubemapComputer;
export import :vulkan.pipeline.SubgroupMipmapComputer;

namespace vk_gltf_viewer::vulkan::inline generator {
    export class MipmappedCubemapGenerator {
        struct IntermediateResources {
            vk::raii::ImageView eqmapImageView;
            std::vector<vk::raii::ImageView> cubemapMipImageViews;

            vk::raii::DescriptorPool descriptorPool;
        };

        const Gpu &gpu;

    public:
        struct Config {
            std::uint32_t cubemapSize = 1024;
            vk::ImageUsageFlags cubemapUsage = {};
        };

        struct Pipelines {
            pipeline::CubemapComputer cubemapComputer;
            pipeline::SubgroupMipmapComputer subgroupMipmapComputer;
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

        auto recordCommands(
            vk::CommandBuffer computeCommandBuffer,
            const Pipelines &pipelines [[clang::lifetimebound]],
            const vku::Image &eqmapImage
        ) -> void {
            constexpr auto getWorkgroupTotal = [](std::span<const std::uint32_t, 3> workgroupCount) {
                return workgroupCount[0] * workgroupCount[1] * workgroupCount[2];
            };

            // --------------------
            // Allocate Vulkan resources that must not be destroyed until the command buffer execution is finished.
            // --------------------

            intermediateResources = std::make_unique<IntermediateResources>(
                vk::raii::ImageView { gpu.device, eqmapImage.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }) },
                std::views::iota(0U, cubemapImage.mipLevels)
                    | std::views::transform([&](std::uint32_t level) -> vk::raii::ImageView {
                        return { gpu.device, cubemapImage.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, level, 1, 0, 6 }, vk::ImageViewType::eCube) };
                    })
                    | std::ranges::to<std::vector>(),
                vk::raii::DescriptorPool {
                    gpu.device,
                    getPoolSizes(pipelines.cubemapComputer.descriptorSetLayout, pipelines.subgroupMipmapComputer.descriptorSetLayout)
                        .getDescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind),
                });

            // --------------------
            // Allocate descriptor sets and update them.
            // --------------------

            const auto [cubemapComputerSet, subgroupMipmapComputerSet]
                = allocateDescriptorSets(*gpu.device, *intermediateResources->descriptorPool, std::tie(
                    pipelines.cubemapComputer.descriptorSetLayout,
                    pipelines.subgroupMipmapComputer.descriptorSetLayout));

            gpu.device.updateDescriptorSets({
                cubemapComputerSet.getWrite<0>(vku::unsafeProxy(vk::DescriptorImageInfo { {}, *intermediateResources->eqmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal })),
                cubemapComputerSet.getWrite<1>(vku::unsafeProxy(vk::DescriptorImageInfo { {}, *intermediateResources->cubemapMipImageViews[0], vk::ImageLayout::eGeneral })),
                subgroupMipmapComputerSet.getWrite<0>(vku::unsafeProxy(
                    intermediateResources->cubemapMipImageViews
                        | std::views::transform([](vk::ImageView view) {
                            return vk::DescriptorImageInfo { {}, view, vk::ImageLayout::eGeneral };
                        })
                        | std::ranges::to<std::vector>())),
            }, {});

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
                    cubemapImage, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6 },
                });

            // Generate cubemap from eqmapImage.
            pipelines.cubemapComputer.compute(computeCommandBuffer, cubemapComputerSet, cubemapImage.extent.width);

            computeCommandBuffer.pipelineBarrier2KHR({
                {},
                // Ensure cubemapImage[mipLevel=0] generation finish for the future compute shader read.
                vku::unsafeProxy(vk::MemoryBarrier2 {
                    vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite,
                    vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageRead,
                }),
                {},
                // Change cubemapImage[mipLevel=1..] layout to General.
                vku::unsafeProxy(vk::ImageMemoryBarrier2 {
                    {}, {},
                    vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite,
                    {}, vk::ImageLayout::eGeneral,
                    vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                    cubemapImage, { vk::ImageAspectFlagBits::eColor, 1, vk::RemainingMipLevels, 0, 6 },
                }),
            });

            // Generate cubemapImage mipmaps.
            pipelines.subgroupMipmapComputer.compute(computeCommandBuffer, subgroupMipmapComputerSet, vku::toExtent2D(cubemapImage.extent), cubemapImage.mipLevels);
        }

    private:
        std::unique_ptr<IntermediateResources> intermediateResources;
    };
}