module;

#include <cstddef>

#include <lifetimebound.hpp>

export module ibl.SphericalHarmonicCoefficientComputePipeline;

import std;
export import vku;

import ibl.shader.spherical_harmonic_coefficient_image_to_buffer_comp;
import ibl.shader.spherical_harmonic_coefficient_buffer_to_buffer_comp;

[[nodiscard]] constexpr std::uint32_t square(std::uint32_t num) noexcept {
    return num * num;
}

[[nodiscard]] constexpr std::uint32_t divCeil(std::uint32_t num, std::uint32_t denom) noexcept {
    return (num / denom) + (num % denom != 0);
}

namespace ibl {
    export class SphericalHarmonicCoefficientComputePipeline {
    public:
        struct Config {
            std::uint32_t sampleMipLevel;
            std::uint32_t subgroupSize;
        };

        static constexpr vk::ImageUsageFlags requiredCubemapImageUsageFlags = vk::ImageUsageFlagBits::eSampled;
        static constexpr vk::DeviceSize requiredResultBufferSize = sizeof(float[27]);
        static constexpr vk::BufferUsageFlags requiredResultBufferUsageFlags = vk::BufferUsageFlagBits::eTransferDst;

        SphericalHarmonicCoefficientComputePipeline(
            const vk::raii::Device &device LIFETIMEBOUND,
            vma::Allocator allocator LIFETIMEBOUND,
            const vku::Image &cubemapImage LIFETIMEBOUND,
            const vku::Buffer &resultBuffer LIFETIMEBOUND,
            const Config &config
        );

        void setCubemapImage(const vku::Image &cubemapImage LIFETIME_CAPTURE_BY(this));
        void setSampleMipLevel(std::uint32_t level);
        void setResultBuffer(const vku::Buffer &buffer LIFETIME_CAPTURE_BY(this));

        void recordCommands(vk::CommandBuffer computeCommandBuffer) const;

    private:
        struct BufferToBufferPipelinePushConstant;

        Config config;
        std::reference_wrapper<const vk::raii::Device> device;
        vma::Allocator allocator;
        std::reference_wrapper<const vku::Image> cubemapImage;
        std::reference_wrapper<const vku::Buffer> resultBuffer;
        vk::raii::Sampler cubemapLinearSampler;
        vku::DescriptorSetLayout<vk::DescriptorType::eCombinedImageSampler, vk::DescriptorType::eStorageBuffer> imageToBufferPipelineDescriptorSetLayout;
        vk::raii::PipelineLayout imageToBufferPipelineLayout;
        vk::raii::Pipeline imageToBufferPipeline;
        vku::DescriptorSetLayout<vk::DescriptorType::eStorageBuffer> bufferToBufferPipelineDescriptorSetLayout;
        vk::raii::PipelineLayout bufferToBufferPipelineLayout;
        vk::raii::Pipeline bufferToBufferPipeline;
        vk::raii::ImageView cubemapImageView;
        vku::AllocatedBuffer reductionBuffer;

        [[nodiscard]] std::uint32_t getCubemapMipSize() const;

        [[nodiscard]] vku::AllocatedBuffer createReductionBuffer() const;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

struct ibl::SphericalHarmonicCoefficientComputePipeline::BufferToBufferPipelinePushConstant {
    std::uint32_t srcOffset;
    std::uint32_t count;
    std::uint32_t dstOffset;
};

ibl::SphericalHarmonicCoefficientComputePipeline::SphericalHarmonicCoefficientComputePipeline(
    const vk::raii::Device &device,
    vma::Allocator allocator,
    const vku::Image &cubemapImage,
    const vku::Buffer &resultBuffer,
    const Config &config
) : config { config },
    device { device },
    allocator { allocator },
    cubemapImage { cubemapImage },
    resultBuffer { resultBuffer },
    cubemapLinearSampler { device, vk::SamplerCreateInfo { {}, vk::Filter::eLinear, vk::Filter::eLinear }.setMaxLod(vk::LodClampNone) },
    imageToBufferPipelineDescriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
        vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptor,
        vku::unsafeProxy(decltype(imageToBufferPipelineDescriptorSetLayout)::getBindings(
            { 1, vk::ShaderStageFlagBits::eCompute, &*cubemapLinearSampler },
            { 1, vk::ShaderStageFlagBits::eCompute })),
    } },
    imageToBufferPipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        *imageToBufferPipelineDescriptorSetLayout,
    } },
    imageToBufferPipeline { device, nullptr, vk::ComputePipelineCreateInfo {
        {},
        createPipelineStages(
            device,
            vku::Shader {
                shader::spherical_harmonic_coefficient_image_to_buffer_comp,
                vk::ShaderStageFlagBits::eCompute,
                // TODO: use vku::SpecializationMap when available.
                vku::unsafeAddress(vk::SpecializationInfo {
                    vku::unsafeProxy(vk::SpecializationMapEntry { 0, 0, sizeof(std::uint32_t) }),
                    vk::ArrayProxyNoTemporaries<const std::uint32_t> { config.subgroupSize },
                }),
            }).get()[0],
        *imageToBufferPipelineLayout,
    } },
    bufferToBufferPipelineDescriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
        vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptor,
        vku::unsafeProxy(decltype(bufferToBufferPipelineDescriptorSetLayout)::getBindings({ 1, vk::ShaderStageFlagBits::eCompute })),
    } },
    bufferToBufferPipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        *bufferToBufferPipelineDescriptorSetLayout,
        vku::unsafeProxy(vk::PushConstantRange {
            vk::ShaderStageFlagBits::eCompute,
            0, sizeof(BufferToBufferPipelinePushConstant),
        }),
    } },
    bufferToBufferPipeline { device, nullptr, vk::ComputePipelineCreateInfo {
        {},
        createPipelineStages(
            device,
            vku::Shader {
                shader::spherical_harmonic_coefficient_buffer_to_buffer_comp,
                vk::ShaderStageFlagBits::eCompute,
                // TODO: use vku::SpecializationMap when available.
                vku::unsafeAddress(vk::SpecializationInfo {
                    vku::unsafeProxy(vk::SpecializationMapEntry { 0, 0, sizeof(std::uint32_t) }),
                    vk::ArrayProxyNoTemporaries<const std::uint32_t> { config.subgroupSize },
                }),
            }).get()[0],
        *bufferToBufferPipelineLayout,
    } },
    cubemapImageView { device, cubemapImage.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, config.sampleMipLevel, 1, 0, 6 }, vk::ImageViewType::e2DArray) },
    reductionBuffer { createReductionBuffer() } { }

void ibl::SphericalHarmonicCoefficientComputePipeline::setCubemapImage(const vku::Image &cubemapImage) {
    this->cubemapImage = cubemapImage;
    cubemapImageView = { device, cubemapImage.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, config.sampleMipLevel, 1, 0, 6 }, vk::ImageViewType::e2DArray) };
    reductionBuffer = createReductionBuffer();
}

void ibl::SphericalHarmonicCoefficientComputePipeline::setSampleMipLevel(std::uint32_t level) {
    config.sampleMipLevel = level;
    cubemapImageView = { device, cubemapImage.get().getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, config.sampleMipLevel, 1, 0, 6 }, vk::ImageViewType::e2DArray) };
    reductionBuffer = createReductionBuffer();
}

void ibl::SphericalHarmonicCoefficientComputePipeline::setResultBuffer(const vku::Buffer &buffer) {
    resultBuffer = buffer;
}

void ibl::SphericalHarmonicCoefficientComputePipeline::recordCommands(vk::CommandBuffer computeCommandBuffer) const {
    const auto *d = device.get().getDispatcher();

    const auto memoryBarrier = [&]() {
        computeCommandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
            {},
            vk::MemoryBarrier {
                vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead,
            },
            {}, {}, *d);
    };

    // Image -> Buffer reduction.
    computeCommandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *imageToBufferPipeline, *d);
    const std::uint32_t dispatchCountXY = getCubemapMipSize() / 32;
    computeCommandBuffer.pushDescriptorSetKHR(
        vk::PipelineBindPoint::eCompute, *imageToBufferPipelineLayout,
        0, {
            decltype(imageToBufferPipelineDescriptorSetLayout)::getWriteOne<0>({ {}, cubemapImageView, vk::ImageLayout::eShaderReadOnlyOptimal }),
            decltype(imageToBufferPipelineDescriptorSetLayout)::getWriteOne<1>({ reductionBuffer, 0, sizeof(float[27]) * square(dispatchCountXY) }),
        }, *d);
    computeCommandBuffer.dispatch(dispatchCountXY, dispatchCountXY, 1, *d);

    memoryBarrier();

    computeCommandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *bufferToBufferPipeline, *d);
    computeCommandBuffer.pushDescriptorSetKHR(
        vk::PipelineBindPoint::eCompute, *bufferToBufferPipelineLayout,
        0, decltype(bufferToBufferPipelineDescriptorSetLayout)::getWriteOne<0>({ reductionBuffer, 0, vk::WholeSize }), *d);

    // Buffer -> Buffer reduction.
    BufferToBufferPipelinePushConstant pushConstant {
        .srcOffset = 0,
        .count = square(dispatchCountXY),
        .dstOffset = square(dispatchCountXY),
    };
    while (pushConstant.count > 1) {
        computeCommandBuffer.pushConstants<BufferToBufferPipelinePushConstant>(*bufferToBufferPipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, pushConstant, *d);
        const std::uint32_t dispatchCount = divCeil(pushConstant.count, 256);
        computeCommandBuffer.dispatch(dispatchCount, 1, 1, *d);
        memoryBarrier();

        std::swap(pushConstant.srcOffset, pushConstant.dstOffset);
        pushConstant.count = dispatchCount;
    }

    computeCommandBuffer.copyBuffer(reductionBuffer, resultBuffer.get(), vk::BufferCopy {
        sizeof(float[27]) * pushConstant.srcOffset, 0, sizeof(float[27]),
    }, *d);
}

std::uint32_t ibl::SphericalHarmonicCoefficientComputePipeline::getCubemapMipSize() const {
    return cubemapImage.get().extent.width >> config.sampleMipLevel;
}

vku::AllocatedBuffer ibl::SphericalHarmonicCoefficientComputePipeline::createReductionBuffer() const {
    // Image -> Buffer: 32x32 texels will be reduced to a single 2nd-order spherical harmonic coefficients set (sizeof(float[27]).
    vk::DeviceSize coefficientSetCount = square(getCubemapMipSize() / 32);
    // Buffer -> Buffer: 256 2nd-order spherical harmonic coefficients sets will be reduced to a single set.
    coefficientSetCount += divCeil(coefficientSetCount, 256);

    return {
        allocator,
        vk::BufferCreateInfo {
            {},
            sizeof(float[27]) * coefficientSetCount,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
        },
        vma::AllocationCreateInfo {
            {},
            vma::MemoryUsage::eAutoPreferDevice,
        },
    };
}