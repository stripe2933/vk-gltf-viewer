module;

#include <lifetimebound.hpp>

export module bloom;

import std;
export import vku;

import bloom.shader.downsample_comp;
import bloom.shader.upsample_comp;

namespace bloom {
    export class BloomComputePipeline {
        std::reference_wrapper<const vk::raii::Device> device;
        vk::raii::Sampler linearSampler;

    public:
        struct Config {
            bool useAMDShaderImageLoadStoreLod = false;
        };

        using DescriptorSetLayout = vku::DescriptorSetLayout<vk::DescriptorType::eCombinedImageSampler, vk::DescriptorType::eStorageImage>;

        static constexpr vk::ImageUsageFlags requiredImageUsageFlags = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage;

        DescriptorSetLayout descriptorSetLayout;

        explicit BloomComputePipeline(
            const vk::raii::Device &device LIFETIMEBOUND,
            const Config &config = {
                .useAMDShaderImageLoadStoreLod = false,
            }
        );

        void compute(
            vk::CommandBuffer computeCommandBuffer,
            vku::DescriptorSet<DescriptorSetLayout> descriptorSet,
            const vk::Extent2D &imageExtent,
            std::uint32_t imageMipLevels
        ) const;

    private:
        struct PushConstant;

        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline downsamplePipeline;
        vk::raii::Pipeline upsamplePipeline;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

struct bloom::BloomComputePipeline::PushConstant {
    std::int32_t srcMipLevel;
    std::int32_t dstMipLevel;
};

bloom::BloomComputePipeline::BloomComputePipeline(
    const vk::raii::Device &device,
    const Config &config
) : device { device },
    linearSampler { device, vk::SamplerCreateInfo {
        {},
        vk::Filter::eLinear, vk::Filter::eLinear, {},
        vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge,
    }.setMaxLod(vk::LodClampNone) },
    descriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
        {},
        vku::unsafeProxy(DescriptorSetLayout::getBindings(
            { 1, vk::ShaderStageFlagBits::eCompute, &*linearSampler },
            // TODO: use variable descriptor count or partially bounding
            { config.useAMDShaderImageLoadStoreLod ? 1U : 16U, vk::ShaderStageFlagBits::eCompute })),
    } },
    pipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        *descriptorSetLayout,
        vku::unsafeProxy(vk::PushConstantRange {
            vk::ShaderStageFlagBits::eCompute,
            0, sizeof(PushConstant),
        })
    } },
    downsamplePipeline { device, nullptr, vk::ComputePipelineCreateInfo {
        {},
        createPipelineStages(
            device,
            vku::Shader {
                config.useAMDShaderImageLoadStoreLod
                    ? std::span<const std::uint32_t> { shader::downsample_comp<1> }
                    : std::span<const std::uint32_t> { shader::downsample_comp<0> },
                vk::ShaderStageFlagBits::eCompute,
            }).get()[0],
        *pipelineLayout,
    } },
    upsamplePipeline { device, nullptr, vk::ComputePipelineCreateInfo {
        {},
        createPipelineStages(
            device,
            vku::Shader {
                config.useAMDShaderImageLoadStoreLod
                    ? std::span<const std::uint32_t> { shader::upsample_comp<1> }
                    : std::span<const std::uint32_t> { shader::upsample_comp<0> },
                vk::ShaderStageFlagBits::eCompute,
            }).get()[0],
        *pipelineLayout,
    } } { }

void bloom::BloomComputePipeline::compute(vk::CommandBuffer computeCommandBuffer, vku::DescriptorSet<DescriptorSetLayout> descriptorSet, const vk::Extent2D &imageExtent, std::uint32_t imageMipLevels) const {
    constexpr auto divCeil = [](std::uint32_t num, std::uint32_t denom) noexcept {
        return (num / denom) + (num % denom != 0);
    };

    const auto *d = device.get().getDispatcher();
    computeCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, descriptorSet, {}, *d);

    computeCommandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *downsamplePipeline, *d);
    for (std::int32_t dstMipLevel = 1; dstMipLevel < imageMipLevels; ++dstMipLevel) {
        computeCommandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eCompute,
            0, PushConstant {
                .srcMipLevel = dstMipLevel - 1,
                .dstMipLevel = dstMipLevel,
            }, *d);
        const vk::Extent2D mipExtent = vku::Image::mipExtent(imageExtent, dstMipLevel);
        computeCommandBuffer.dispatch(
            divCeil(mipExtent.width, 16U),
            divCeil(mipExtent.height, 16U),
            1,
            *d);

        computeCommandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
            {}, vk::MemoryBarrier {
                vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead,
            }, {}, {}, *d);
    }

    computeCommandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *upsamplePipeline, *d);
    for (std::int32_t srcMipLevel = imageMipLevels - 1; srcMipLevel >= 1; --srcMipLevel) {
        computeCommandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eCompute,
            0, PushConstant {
                .srcMipLevel = srcMipLevel,
                .dstMipLevel = srcMipLevel - 1,
            }, *d);
        const vk::Extent2D mipExtent = vku::Image::mipExtent(imageExtent, srcMipLevel - 1);
        computeCommandBuffer.dispatch(
            divCeil(mipExtent.width, 16U),
            divCeil(mipExtent.height, 16U),
            1,
            *d);

        if (srcMipLevel != 1) {
            computeCommandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                {}, vk::MemoryBarrier {
                    vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead,
                }, {}, {}, *d);
        }
    }
}