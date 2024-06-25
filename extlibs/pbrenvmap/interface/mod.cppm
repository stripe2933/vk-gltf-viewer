module;

#include <cstdint>
#include <array>
#include <compare>
#include <numbers>
#include <ranges>
#include <vector>

#include <vulkan/vulkan_hpp_macros.hpp>

export module pbrenvmap;

export import vku;
import :details.ranges;
export import :pipelines.CubemapComputer;
export import :pipelines.MultiplyComputer;
export import :pipelines.PrefilteredmapComputer;
export import :pipelines.SphericalHarmonicsComputer;
export import :pipelines.SphericalHarmonicCoefficientsSumComputer;
export import :pipelines.SubgroupMipmapComputer;

namespace pbrenvmap {
    export class Generator {
    public:
        struct Pipelines {
            const pipelines::CubemapComputer &cubemapComputer;
            const pipelines::SubgroupMipmapComputer &subgroupMipmapComputer;
            const pipelines::SphericalHarmonicsComputer &sphericalHarmonicsComputer;
            const pipelines::SphericalHarmonicCoefficientsSumComputer &sphericalHarmonicCoefficientsSumComputer;
            const pipelines::PrefilteredmapComputer &prefilteredmapComputer;
            const pipelines::MultiplyComputer &multiplyComputer;
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

        auto recordCommands(vk::CommandBuffer computeCommandBuffer, const Pipelines &pipelines, vk::ImageView eqmapImageView) const -> void;

    private:
        const vk::raii::Device &device;
        const Config &config;

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
    }, vma::AllocationCreateInfo { {}, vma::MemoryUsage::eAutoPreferDevice, } },
    sphericalHarmonicCoefficientsBuffer { allocator, vk::BufferCreateInfo {
        {},
        sizeof(float) * 27,
        vk::BufferUsageFlagBits::eStorageBuffer // to be used as copy destination from reduction buffer
            | config.sphericalHarmonicCoefficients.usage,
    }, vma::AllocationCreateInfo {
        vma::AllocationCreateFlagBits::eHostAccessRandom | vma::AllocationCreateFlagBits::eMapped,
        vma::MemoryUsage::eAutoPreferHost,
    } },
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
    }, vma::AllocationCreateInfo { {}, vma::MemoryUsage::eAutoPreferDevice, } },
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
        sizeof(float) * 27 * pipelines::SphericalHarmonicCoefficientsSumComputer::getPingPongBufferElementCount(
            getWorkgroupTotal(pipelines::SphericalHarmonicsComputer::getWorkgroupCount(config.cubemap.size))),
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
    }, vma::AllocationCreateInfo { {}, vma::MemoryUsage::eAutoPreferDevice, } },
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
    const Pipelines &pipelines,
    vk::ImageView eqmapImageView
) const -> void {
    ////////////////////
    // Descriptor sets.
    ////////////////////

    const pipelines::CubemapComputer::DescriptorSets cubemapDescriptorSets {
        *device, *descriptorPool, pipelines.cubemapComputer.descriptorSetLayouts
    };
    const pipelines::SphericalHarmonicsComputer::DescriptorSets sphericalHarmonicsDescriptorSets {
        *device, *descriptorPool, pipelines.sphericalHarmonicsComputer.descriptorSetLayouts
    };
    const pipelines::SphericalHarmonicCoefficientsSumComputer::DescriptorSets sphericalHarmonicCoefficientsSumDescriptorSets {
        *device, *descriptorPool, pipelines.sphericalHarmonicCoefficientsSumComputer.descriptorSetLayouts
    };
    const pipelines::SubgroupMipmapComputer::DescriptorSets subgroupMipmapDescriptorSets {
        *device, *descriptorPool, pipelines.subgroupMipmapComputer.descriptorSetLayouts
    };
    const pipelines::PrefilteredmapComputer::DescriptorSets prefilteredmapDescriptorSets {
        *device, *descriptorPool, pipelines.prefilteredmapComputer.descriptorSetLayouts
    };
    const pipelines::MultiplyComputer::DescriptorSets multiplyDescriptorSets {
        *device, *descriptorPool, pipelines.multiplyComputer.descriptorSetLayouts
    };
    device.updateDescriptorSets(
        ranges::array_cat(
            cubemapDescriptorSets.getDescriptorWrites0(eqmapImageView, *cubemapMipImageViews[0]).get(),
            subgroupMipmapDescriptorSets.getDescriptorWrites0(
                cubemapMipImageViews | ranges::views::deref).get(),
            sphericalHarmonicsDescriptorSets.getDescriptorWrites0(
                *cubemapImageView,
                {
                    sphericalHarmonicsReductionBuffer,
                    0,
                    sizeof(float) * 27 * getWorkgroupTotal(
                        pipelines::SphericalHarmonicsComputer::getWorkgroupCount(config.cubemap.size)),
                }).get(),
            sphericalHarmonicCoefficientsSumDescriptorSets.getDescriptorWrites0(
                { sphericalHarmonicsReductionBuffer, 0, vk::WholeSize }),
            prefilteredmapDescriptorSets.getDescriptorWrites0(*cubemapImageView, prefilteredmapMipImageViews | ranges::views::deref).get()),
        {});

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
    pipelines.cubemapComputer.compute(computeCommandBuffer, cubemapDescriptorSets, config.cubemap.size);

    // Ensures cubemap generation finish.
    computeCommandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
        {},
        vk::MemoryBarrier {
            vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead
        },
        {}, {});

    // Compute spherical harmonic coefficients.
    pipelines.sphericalHarmonicsComputer.compute(computeCommandBuffer, sphericalHarmonicsDescriptorSets, config.cubemap.size);

    // Create cubemap mipmaps.
    pipelines.subgroupMipmapComputer.compute(
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
                pipelines::SphericalHarmonicsComputer::getWorkgroupCount(config.cubemap.size)),
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
    pipelines.prefilteredmapComputer.compute(computeCommandBuffer, prefilteredmapDescriptorSets, config.cubemap.size, config.prefilteredmap.size);

    // Compute spherical harmonic coefficients sum.
    const std::uint32_t workgroupTotal = getWorkgroupTotal(pipelines::SphericalHarmonicsComputer::getWorkgroupCount(config.cubemap.size));
    const std::uint32_t dstOffset = pipelines.sphericalHarmonicCoefficientsSumComputer.compute(
        computeCommandBuffer, sphericalHarmonicCoefficientsSumDescriptorSets,
        pipelines::SphericalHarmonicCoefficientsSumComputer::PushConstant {
            .srcOffset = 0,
            .count = workgroupTotal,
            .dstOffset = workgroupTotal,
        });

    device.updateDescriptorSets(
        multiplyDescriptorSets.getDescriptorWrites0(
            { sphericalHarmonicsReductionBuffer, sizeof(float) * 27 * dstOffset, sizeof(float) * 27 },
            { sphericalHarmonicCoefficientsBuffer, 0, vk::WholeSize }),
        {});

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
    pipelines.multiplyComputer.compute(computeCommandBuffer, multiplyDescriptorSets, pipelines::MultiplyComputer::PushConstant {
        .numCount = 27,
        .multiplier = 4.f * std::numbers::pi_v<float> / (6U * config.cubemap.size * config.cubemap.size),
    });
}