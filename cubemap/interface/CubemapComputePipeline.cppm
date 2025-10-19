module;

#include <lifetimebound.hpp>

export module cubemap.CubemapComputePipeline;

import std;
export import vku;

import cubemap.shader.cubemap_comp;

namespace cubemap {
    /**
     * @brief Generate cubemap image from equirectangular map image using compute shader.
     */
    export class CubemapComputePipeline {
    public:
        /**
         * @brief Required image usage flags for equirectangular map image.
         */
        static constexpr vk::ImageUsageFlags requiredEqmapImageUsageFlags = vk::ImageUsageFlagBits::eSampled;

        /**
         * @brief Required image usage flags for cubemap image.
         */
        static constexpr vk::ImageUsageFlags requiredCubemapImageUsageFlags = vk::ImageUsageFlagBits::eStorage;

        CubemapComputePipeline(
            const vk::raii::Device &device LIFETIMEBOUND,
            const vku::Image &eqmapImage LIFETIMEBOUND,
            vk::Sampler eqmapSampler LIFETIMEBOUND,
            const vku::Image &cubemapImage LIFETIMEBOUND
        );

        void setEqmapImage(const vku::Image &eqmapImage LIFETIME_CAPTURE_BY(this));
        void setCubemapImage(const vku::Image &cubemapImage LIFETIME_CAPTURE_BY(this));

        void recordCommands(vk::CommandBuffer computeCommandBuffer) const;

    private:
        std::reference_wrapper<const vk::raii::Device> device;
        std::reference_wrapper<const vku::Image> cubemapImage;

        vku::raii::DescriptorSetLayout<vk::DescriptorType::eCombinedImageSampler, vk::DescriptorType::eStorageImage> descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;
        vk::raii::ImageView eqmapImageView;
        vk::raii::ImageView cubemapImageView;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

cubemap::CubemapComputePipeline::CubemapComputePipeline(
    const vk::raii::Device &device,
    const vku::Image &eqmapImage,
    vk::Sampler eqmapSampler,
    const vku::Image &cubemapImage
) : device { device },
    cubemapImage { cubemapImage },
    descriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
        vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptor,
        vku::lvalue({
            decltype(descriptorSetLayout)::getCreateInfoBinding<0>(vk::ShaderStageFlagBits::eCompute, eqmapSampler),
            decltype(descriptorSetLayout)::getCreateInfoBinding<1>(1, vk::ShaderStageFlagBits::eCompute),
        }),
    } },
    pipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        *descriptorSetLayout,
    } },
    pipeline { device, nullptr, vk::ComputePipelineCreateInfo {
        {},
        vk::PipelineShaderStageCreateInfo {
            {},
            vk::ShaderStageFlagBits::eCompute,
            *vku::lvalue(vk::raii::ShaderModule { device, vk::ShaderModuleCreateInfo {
                {},
                shader::cubemap_comp,
            } }),
            "main",
        },
        *pipelineLayout,
    } },
    eqmapImageView { device, eqmapImage.getViewCreateInfo(vk::ImageViewType::e2D, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }) },
    cubemapImageView { device, cubemapImage.getViewCreateInfo(vk::ImageViewType::eCube, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6 }) } { }

void cubemap::CubemapComputePipeline::setEqmapImage(const vku::Image &eqmapImage) {
    eqmapImageView = { device, eqmapImage.getViewCreateInfo(vk::ImageViewType::e2D, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }) };
}

void cubemap::CubemapComputePipeline::setCubemapImage(const vku::Image &cubemapImage) {
    this->cubemapImage = cubemapImage;
    cubemapImageView = { device, cubemapImage.getViewCreateInfo(vk::ImageViewType::eCube, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6 }) };
}

void cubemap::CubemapComputePipeline::recordCommands(vk::CommandBuffer computeCommandBuffer) const {
    const auto *d = device.get().getDispatcher();

    computeCommandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline, *d);
    computeCommandBuffer.pushDescriptorSetKHR(
        vk::PipelineBindPoint::eCompute, *pipelineLayout,
        0, {
            decltype(descriptorSetLayout)::getWriteDescriptorSet<0>({}, 0, vku::lvalue(vk::DescriptorImageInfo { {}, *eqmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal })),
            decltype(descriptorSetLayout)::getWriteDescriptorSet<1>({}, 0, vku::lvalue(vk::DescriptorImageInfo { {}, *cubemapImageView, vk::ImageLayout::eGeneral })),
        }, *d);
    computeCommandBuffer.dispatch(cubemapImage.get().extent.width / 16U, cubemapImage.get().extent.height / 16U, 6, *d);
}