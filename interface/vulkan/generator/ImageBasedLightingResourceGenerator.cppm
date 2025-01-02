module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.generator.ImageBasedLightingResourceGenerator;

import std;
import :helpers.functional;
export import :vulkan.pipeline.MultiplyComputer;
export import :vulkan.pipeline.PrefilteredmapComputer;
export import :vulkan.pipeline.SphericalHarmonicCoefficientsSumComputer;
export import :vulkan.pipeline.SphericalHarmonicsComputer;

namespace vk_gltf_viewer::vulkan::inline generator {
    export class ImageBasedLightingResourceGenerator {
        struct IntermediateResources {
            vku::AllocatedBuffer sphericalHarmonicsReductionBuffer;

            vk::raii::ImageView cubemapImageView;
            vk::raii::ImageView cubemapArrayImageView;
            std::variant<vk::raii::ImageView, std::vector<vk::raii::ImageView>> prefilteredmapImageViews;

            vk::raii::DescriptorPool descriptorPool;
        };

        const Gpu &gpu;

    public:
        struct Config {
            std::uint32_t prefilteredmapSize = 256;
            vk::ImageUsageFlags prefilteredmapImageUsage = {};
            vk::BufferUsageFlags sphericalHarmonicsBufferUsage = {};
        };

        struct Pipelines {
            PrefilteredmapComputer prefilteredmapComputer;
            SphericalHarmonicsComputer sphericalHarmonicsComputer;
            SphericalHarmonicCoefficientsSumComputer sphericalHarmonicCoefficientsSumComputer;
            MultiplyComputer multiplyComputer;
        };

        vku::AllocatedImage prefilteredmapImage;
        vku::MappedBuffer sphericalHarmonicsBuffer;

        ImageBasedLightingResourceGenerator(
            const Gpu &gpu [[clang::lifetimebound]],
            const Config &config
        ) : gpu { gpu },
            prefilteredmapImage { gpu.allocator, vk::ImageCreateInfo {
                vk::ImageCreateFlagBits::eCubeCompatible,
                vk::ImageType::e2D,
                vk::Format::eR32G32B32A32Sfloat,
                vk::Extent3D { config.prefilteredmapSize, config.prefilteredmapSize, 1 },
                vku::Image::maxMipLevels(config.prefilteredmapSize), 6,
                vk::SampleCountFlagBits::e1,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eStorage | config.prefilteredmapImageUsage,
            } },
            sphericalHarmonicsBuffer { gpu.allocator, vk::BufferCreateInfo {
                {},
                27 * sizeof(float),
                vk::BufferUsageFlagBits::eStorageBuffer | config.sphericalHarmonicsBufferUsage,
            } } { }

        void recordCommands(
            vk::CommandBuffer computeCommandBuffer,
            const Pipelines &pipelines [[clang::lifetimebound]],
            const vku::Image &cubemapImage
        ) {
            constexpr auto getWorkgroupTotal = [](std::span<const std::uint32_t, 3> workgroupCount) {
                return workgroupCount[0] * workgroupCount[1] * workgroupCount[2];
            };

            // --------------------
            // Allocate Vulkan resources that must not be destroyed until the command buffer execution is finished.
            // --------------------

            intermediateResources = std::make_unique<IntermediateResources>(
                vku::AllocatedBuffer { gpu.allocator, vk::BufferCreateInfo {
                    {},
                    27 * sizeof(float) * SphericalHarmonicCoefficientsSumComputer::getPingPongBufferElementCount(
                        getWorkgroupTotal(SphericalHarmonicsComputer::getWorkgroupCount(cubemapImage.extent.width))),
                    vk::BufferUsageFlagBits::eStorageBuffer,
                } },
                vk::raii::ImageView { gpu.device, cubemapImage.getViewCreateInfo(vk::ImageViewType::eCube) },
                vk::raii::ImageView { gpu.device, cubemapImage.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6 }, vk::ImageViewType::e2DArray) },
                vk::raii::ImageView { gpu.device, prefilteredmapImage.getViewCreateInfo(vk::ImageViewType::eCube) },
                vk::raii::DescriptorPool {
                    gpu.device,
                    getPoolSizes(
                        pipelines.prefilteredmapComputer.descriptorSetLayout,
                        pipelines.sphericalHarmonicsComputer.descriptorSetLayout,
                        pipelines.sphericalHarmonicCoefficientsSumComputer.descriptorSetLayout,
                        pipelines.multiplyComputer.descriptorSetLayout)
                    .getDescriptorPoolCreateInfo(),
                });
            if (!gpu.supportShaderImageLoadStoreLod) {
                intermediateResources->prefilteredmapImageViews.emplace<std::vector<vk::raii::ImageView>>(
                    std::from_range,
                    prefilteredmapImage.getMipViewCreateInfos(vk::ImageViewType::eCube)
                        | std::views::transform([this](const vk::ImageViewCreateInfo &createInfo) {
                            return vk::raii::ImageView { gpu.device, createInfo };
                        }));
            }

            // --------------------
            // Allocate descriptor sets and update them.
            // --------------------
            
            const auto [prefilteredmapComputerSet, sphericalHarmonicsComputerSet, sphericalHarmonicCoefficientsSumComputerSet, multiplyComputerSet]
                = allocateDescriptorSets(*gpu.device, *intermediateResources->descriptorPool, std::tie(
                    pipelines.prefilteredmapComputer.descriptorSetLayout,
                    pipelines.sphericalHarmonicsComputer.descriptorSetLayout,
                    pipelines.sphericalHarmonicCoefficientsSumComputer.descriptorSetLayout,
                    pipelines.multiplyComputer.descriptorSetLayout));


            visit(multilambda {
                [&](vk::ImageView prefilteredmapImageView) {
                    gpu.device.updateDescriptorSets({
                        prefilteredmapComputerSet.getWriteOne<0>({ {}, *intermediateResources->cubemapImageView, vk::ImageLayout::eShaderReadOnlyOptimal }),
                        prefilteredmapComputerSet.getWriteOne<1>({ {}, prefilteredmapImageView, vk::ImageLayout::eGeneral }),
                        sphericalHarmonicsComputerSet.getWriteOne<0>({ {}, *intermediateResources->cubemapArrayImageView, vk::ImageLayout::eShaderReadOnlyOptimal }),
                        sphericalHarmonicsComputerSet.getWriteOne<1>({ intermediateResources->sphericalHarmonicsReductionBuffer, 0, vk::WholeSize }),
                        sphericalHarmonicCoefficientsSumComputerSet.getWriteOne<0>({ intermediateResources->sphericalHarmonicsReductionBuffer, 0, vk::WholeSize }),
                    }, {});
                },
                [&](std::span<const vk::raii::ImageView> prefilteredmapMipImageViews) {
                    gpu.device.updateDescriptorSets({
                        prefilteredmapComputerSet.getWriteOne<0>({ {}, *intermediateResources->cubemapImageView, vk::ImageLayout::eShaderReadOnlyOptimal }),
                        prefilteredmapComputerSet.getWrite<1>(vku::unsafeProxy(
                            prefilteredmapMipImageViews
                                | std::views::transform([](vk::ImageView view) {
                                    return vk::DescriptorImageInfo { {}, view, vk::ImageLayout::eGeneral };
                                })
                                | std::ranges::to<std::vector>())),
                        sphericalHarmonicsComputerSet.getWriteOne<0>({ {}, *intermediateResources->cubemapArrayImageView, vk::ImageLayout::eShaderReadOnlyOptimal }),
                        sphericalHarmonicsComputerSet.getWriteOne<1>({ intermediateResources->sphericalHarmonicsReductionBuffer, 0, vk::WholeSize }),
                        sphericalHarmonicCoefficientsSumComputerSet.getWriteOne<0>({ intermediateResources->sphericalHarmonicsReductionBuffer, 0, vk::WholeSize }),
                    }, {});
                },
            }, intermediateResources->prefilteredmapImageViews);

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
                    prefilteredmapImage, vku::fullSubresourceRange(),
                });

            // Generate prefiltered map.
            pipelines.prefilteredmapComputer.compute(computeCommandBuffer, prefilteredmapComputerSet, prefilteredmapImage.extent.width);

            // Initial spherical harmonic coefficients reduction.
            pipelines.sphericalHarmonicsComputer.compute(computeCommandBuffer, sphericalHarmonicsComputerSet, cubemapImage.extent.width);

            // Ensure initial reduction finish for the future compute shader read.
            computeCommandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                {},
                vk::MemoryBarrier { vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead },
                {}, {});

            // Calculate sum of total spherical harmonic coefficients using ping-pong buffer dispatches.
            const std::uint32_t workgroupTotal = getWorkgroupTotal(SphericalHarmonicsComputer::getWorkgroupCount(cubemapImage.extent.width));
            const std::uint32_t dstOffset = pipelines.sphericalHarmonicCoefficientsSumComputer.compute(computeCommandBuffer, sphericalHarmonicCoefficientsSumComputerSet, {
                .srcOffset = 0,
                .count = workgroupTotal,
                .dstOffset = workgroupTotal,
            });

            // Ensure reduction finish.
            computeCommandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                {},
                vk::MemoryBarrier { vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead },
                {},
                {});

            // sphericalHarmonicsReductionBuffer[dstOffset:dstOffset + 9 * sizeof(glm::vec3)] represents the total sum.
            // It has to be divided by the total cubemap texel count for the average calculation.
            gpu.device.updateDescriptorSets({
                multiplyComputerSet.getWriteOne<0>({ intermediateResources->sphericalHarmonicsReductionBuffer, sizeof(float) * 27 * dstOffset, sizeof(float) * 27 }),
                multiplyComputerSet.getWriteOne<1>({ sphericalHarmonicsBuffer, 0, vk::WholeSize }),
            }, {});

            // Copy from sphericalHarmonicsReductionBuffer to sphericalHarmonicsBuffer with normalization multiplier.
            pipelines.multiplyComputer.compute(computeCommandBuffer, multiplyComputerSet, {
                .numCount = 27,
                .multiplier = 4.f * std::numbers::pi_v<float> / (6U * cubemapImage.extent.width * cubemapImage.extent.width),
            });

            // sphericalHarmonicsBuffer have to be available to the host.
            computeCommandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eHost,
                {},
                {},
                vk::BufferMemoryBarrier {
                    vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eHostRead,
                    vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                    sphericalHarmonicsBuffer, 0, vk::WholeSize,
                },
                {});
        }

    private:
        std::unique_ptr<IntermediateResources> intermediateResources;
    };
}