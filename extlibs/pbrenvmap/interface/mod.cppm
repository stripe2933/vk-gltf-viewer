module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module pbrenvmap;

import std;
export import vku;
import :details.ranges;
export import :pipeline.CubemapComputer;
export import :pipeline.MultiplyComputer;
export import :pipeline.PrefilteredmapComputer;
export import :pipeline.SphericalHarmonicsComputer;
export import :pipeline.SphericalHarmonicCoefficientsSumComputer;
export import :pipeline.SubgroupMipmapComputer;

namespace pbrenvmap {
    export class Generator {
    public:
        struct Pipelines {
            const pipeline::CubemapComputer &cubemapComputer;
            const pipeline::SubgroupMipmapComputer &subgroupMipmapComputer;
            const pipeline::SphericalHarmonicsComputer &sphericalHarmonicsComputer;
            const pipeline::SphericalHarmonicCoefficientsSumComputer &sphericalHarmonicCoefficientsSumComputer;
            const pipeline::PrefilteredmapComputer &prefilteredmapComputer;
            const pipeline::MultiplyComputer &multiplyComputer;
        };

        struct Config {
            struct CubemapProperties {
                std::uint32_t size = 1024U;
                vk::ImageUsageFlags usage = {};
            };

            struct SphericalHarmonicCoefficientsProperties {
                vk::BufferUsageFlags usage = {};
            };

            struct PrefilteredmapProperties {
                std::uint32_t size = 256U;
                std::uint32_t roughnessLevels = vku::Image::maxMipLevels(size);
                std::uint32_t samples = 1024U;
                vk::ImageUsageFlags usage = {};
            };

            CubemapProperties cubemap = {};
            SphericalHarmonicCoefficientsProperties sphericalHarmonicCoefficients = {};
            PrefilteredmapProperties prefilteredmap = {};
        };

        vku::AllocatedImage cubemapImage;
        vku::MappedBuffer sphericalHarmonicCoefficientsBuffer;
        vku::AllocatedImage prefilteredmapImage;

        Generator(const vk::raii::Device &device, vma::Allocator allocator, const Config &config);

        auto recordCommands(vk::CommandBuffer computeCommandBuffer, const Pipelines &pipeline, vk::ImageView eqmapImageView) const -> void;

    private:
        const vk::raii::Device &device;
        Config config;

        // Image views.
        vk::raii::ImageView cubemapImageView;
        std::vector<vk::raii::ImageView> cubemapMipImageViews, prefilteredmapMipImageViews;

        // Buffers.
        vku::AllocatedBuffer sphericalHarmonicsReductionBuffer;

        // Descriptor pool.
        vk::raii::DescriptorPool descriptorPool;
    };
}

// module :private;

[[nodiscard]] constexpr auto getWorkgroupTotal(const std::array<std::uint32_t, 3> &workgroupCounts) {
    return get<0>(workgroupCounts) * get<1>(workgroupCounts) * get<2>(workgroupCounts);
}

pbrenvmap::Generator::Generator(
    const vk::raii::Device &device,
    vma::Allocator allocator,
    const Config &config
) : cubemapImage { allocator, vk::ImageCreateInfo {
        vk::ImageCreateFlagBits::eCubeCompatible,
        vk::ImageType::e2D,
        vk::Format::eR32G32B32A32Sfloat,
        vk::Extent3D { config.cubemap.size, config.cubemap.size, 1 },
        vku::Image::maxMipLevels(config.cubemap.size), 6,
        vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eStorage // to be storage image in cubemap and mipmap generation
            | vk::ImageUsageFlagBits::eSampled // to be sampled for irradiance/prefiltered map generation
            | config.cubemap.usage,
    } },
    sphericalHarmonicCoefficientsBuffer { allocator, vk::BufferCreateInfo {
        {},
        sizeof(float) * 27,
        vk::BufferUsageFlagBits::eStorageBuffer // to be used as copy destination from reduction buffer
            | config.sphericalHarmonicCoefficients.usage,
    }, vku::allocation::hostRead },
    prefilteredmapImage { allocator, vk::ImageCreateInfo {
        vk::ImageCreateFlagBits::eCubeCompatible,
        vk::ImageType::e2D,
        vk::Format::eR32G32B32A32Sfloat,
        vk::Extent3D { config.prefilteredmap.size, config.prefilteredmap.size, 1 },
        config.prefilteredmap.roughnessLevels,
        6,
        vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eStorage // to be used as storage image in prefiltered map generation
            | config.prefilteredmap.usage,
    } },
    device { device },
    config { config },
    cubemapImageView { device, vk::ImageViewCreateInfo {
        {},
        cubemapImage,
        vk::ImageViewType::eCube,
        cubemapImage.format,
        {},
        vku::fullSubresourceRange(),
    } },
    cubemapMipImageViews { std::from_range, std::views::iota(0U, cubemapImage.mipLevels)
        | std::views::transform([&](std::uint32_t mipLevel) {
            return vk::raii::ImageView { device, vk::ImageViewCreateInfo {
                {},
                cubemapImage,
                vk::ImageViewType::eCube,
                cubemapImage.format,
                {},
                { vk::ImageAspectFlagBits::eColor, mipLevel, 1, 0, vk::RemainingArrayLayers },
            } };
        })
    },
    prefilteredmapMipImageViews { std::from_range, std::views::iota(0U, prefilteredmapImage.mipLevels)
        | std::views::transform([&](std::uint32_t mipLevel) {
            return vk::raii::ImageView { device, vk::ImageViewCreateInfo {
                {},
                prefilteredmapImage,
                vk::ImageViewType::eCube,
                prefilteredmapImage.format,
                {},
                { vk::ImageAspectFlagBits::eColor, mipLevel, 1, 0, vk::RemainingArrayLayers },
            } };
        })
    },
    sphericalHarmonicsReductionBuffer { allocator, vk::BufferCreateInfo {
        {},
        sizeof(float) * 27 * pipeline::SphericalHarmonicCoefficientsSumComputer::getPingPongBufferElementCount(
            getWorkgroupTotal(pipeline::SphericalHarmonicsComputer::getWorkgroupCount(config.cubemap.size))),
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
    } },
    descriptorPool { device, vk::DescriptorPoolCreateInfo {
        vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind,
        6,
        vku::unsafeProxy({
            vk::DescriptorPoolSize { vk::DescriptorType::eCombinedImageSampler, 2 },
            vk::DescriptorPoolSize { vk::DescriptorType::eStorageBuffer, 6 },
            vk::DescriptorPoolSize { vk::DescriptorType::eStorageImage, 128 },
        }),
    } } {

}

auto pbrenvmap::Generator::recordCommands(
    vk::CommandBuffer computeCommandBuffer,
    const Pipelines &pipeline,
    vk::ImageView eqmapImageView
) const -> void {
    ////////////////////
    // Descriptor sets.
    ////////////////////

    const auto [cubemapDescriptorSets, sphericalHarmonicsDescriptorSets, sphericalHarmonicCoefficientsSumDescriptorSets,
        subgroupMipmapDescriptorSets, prefilteredmapDescriptorSets, multiplyDescriptorSets]
        = vku::allocateDescriptorSets(*device, *descriptorPool, std::tie(
            pipeline.cubemapComputer.descriptorSetLayout,
            pipeline.sphericalHarmonicsComputer.descriptorSetLayout,
            pipeline.sphericalHarmonicCoefficientsSumComputer.descriptorSetLayout,
            pipeline.subgroupMipmapComputer.descriptorSetLayout,
            pipeline.prefilteredmapComputer.descriptorSetLayout,
            pipeline.multiplyComputer.descriptorSetLayout
        ));
    device.updateDescriptorSets({
        cubemapDescriptorSets.getWrite<0>(vku::unsafeProxy(vk::DescriptorImageInfo { {}, eqmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal })),
        cubemapDescriptorSets.getWrite<1>(vku::unsafeProxy(vk::DescriptorImageInfo { {}, *cubemapMipImageViews[0], vk::ImageLayout::eGeneral })),
        subgroupMipmapDescriptorSets.getWrite<0>(vku::unsafeProxy(
            cubemapMipImageViews
                | ranges::views::deref
                | std::views::transform([](vk::ImageView imageView) {
                    return vk::DescriptorImageInfo { {}, imageView, vk::ImageLayout::eGeneral };
                })
                | std::ranges::to<std::vector>())),
        sphericalHarmonicsDescriptorSets.getWrite<0>(vku::unsafeProxy(vk::DescriptorImageInfo { {}, *cubemapImageView, vk::ImageLayout::eGeneral })),
        sphericalHarmonicsDescriptorSets.getWrite<1>(vku::unsafeProxy(vk::DescriptorBufferInfo { sphericalHarmonicsReductionBuffer, 0, vk::WholeSize })),
        sphericalHarmonicCoefficientsSumDescriptorSets.getWrite<0>(vku::unsafeProxy(vk::DescriptorBufferInfo { sphericalHarmonicsReductionBuffer, 0, vk::WholeSize })),
        prefilteredmapDescriptorSets.getWrite<0>(vku::unsafeProxy(vk::DescriptorImageInfo { {}, *cubemapImageView, vk::ImageLayout::eShaderReadOnlyOptimal })),
        prefilteredmapDescriptorSets.getWrite<1>(vku::unsafeProxy(
            prefilteredmapMipImageViews
                | ranges::views::deref
                | std::views::transform([](vk::ImageView imageView) {
                    return vk::DescriptorImageInfo { {}, imageView, vk::ImageLayout::eGeneral };
                })
                | std::ranges::to<std::vector>())),
    }, {});

    ////////////////////
    // Command recordings.
    ////////////////////

    // Layout transition for compute cubemap.
    computeCommandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
        {}, {}, {},
        vk::ImageMemoryBarrier {
            {}, vk::AccessFlagBits::eShaderWrite,
            {}, vk::ImageLayout::eGeneral,
            vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
            cubemapImage,
            vku::fullSubresourceRange(),
        });

    // Compute cubemap.
    pipeline.cubemapComputer.compute(computeCommandBuffer, cubemapDescriptorSets, config.cubemap.size);

    // Ensures cubemap generation finish.
    computeCommandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
        {},
        vk::MemoryBarrier {
            vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead
        },
        {}, {});

    // Compute spherical harmonic coefficients.
    pipeline.sphericalHarmonicsComputer.compute(computeCommandBuffer, sphericalHarmonicsDescriptorSets, config.cubemap.size);

    // Create cubemap mipmaps.
    pipeline.subgroupMipmapComputer.compute(
        computeCommandBuffer, subgroupMipmapDescriptorSets, vku::toExtent2D(cubemapImage.extent), cubemapImage.mipLevels);

    // Layout transition for prefiltered map computation and irradiance buffer reduction.
    computeCommandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
        {}, {},
        vk::BufferMemoryBarrier {
            vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead,
            vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
            sphericalHarmonicsReductionBuffer,
            0,
            sizeof(float) * 27 * getWorkgroupTotal(
                pipeline::SphericalHarmonicsComputer::getWorkgroupCount(config.cubemap.size)),
        },
        {
            vk::ImageMemoryBarrier {
                vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead,
                vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                cubemapImage,
                vku::fullSubresourceRange(),
            },
            vk::ImageMemoryBarrier {
                {}, vk::AccessFlagBits::eShaderWrite,
                {}, vk::ImageLayout::eGeneral,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                prefilteredmapImage,
                vku::fullSubresourceRange(),
            },
        });

    // Compute prefiltered map.
    pipeline.prefilteredmapComputer.compute(computeCommandBuffer, prefilteredmapDescriptorSets, config.cubemap.size, config.prefilteredmap.size);

    // Compute spherical harmonic coefficients sum.
    const std::uint32_t workgroupTotal = getWorkgroupTotal(pipeline::SphericalHarmonicsComputer::getWorkgroupCount(config.cubemap.size));
    const std::uint32_t dstOffset = pipeline.sphericalHarmonicCoefficientsSumComputer.compute(
        computeCommandBuffer, sphericalHarmonicCoefficientsSumDescriptorSets,
        pipeline::SphericalHarmonicCoefficientsSumComputer::PushConstant {
            .srcOffset = 0,
            .count = workgroupTotal,
            .dstOffset = workgroupTotal,
        });

    device.updateDescriptorSets({
        multiplyDescriptorSets.getWrite<0>(vku::unsafeProxy(vk::DescriptorBufferInfo { sphericalHarmonicsReductionBuffer, sizeof(float) * 27 * dstOffset, sizeof(float) * 27 })),
        multiplyDescriptorSets.getWrite<1>(vku::unsafeProxy(vk::DescriptorBufferInfo { sphericalHarmonicCoefficientsBuffer, 0, vk::WholeSize })),
    }, {});

    // Ensure reduction finish.
    computeCommandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTransfer,
        {}, {},
        vk::BufferMemoryBarrier {
            vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eTransferRead,
            vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
            sphericalHarmonicsReductionBuffer,
            sizeof(float) * 27 * dstOffset, sizeof(float) * 27,
        },
        {});

    // Copy from sphericalHarmonicsReductionBuffer to sphericalHarmonicCoefficientsBuffer with normalization multiplier.
    pipeline.multiplyComputer.compute(computeCommandBuffer, multiplyDescriptorSets, pipeline::MultiplyComputer::PushConstant {
        .numCount = 27,
        .multiplier = 4.f * std::numbers::pi_v<float> / (6U * config.cubemap.size * config.cubemap.size),
    });
}