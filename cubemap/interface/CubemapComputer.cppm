module;

#include <lifetimebound.hpp>

export module cubemap.CubemapComputer;

import std;
export import vku;

import cubemap.shader.cubemap_comp;

namespace cubemap {
    /**
     * @brief Generate cubemap image from equirectangular map image using compute shader.
     */
    export class CubemapComputer {
    public:
        /**
         * @brief Required image usage flags for equirectangular map image.
         */
        static constexpr vk::ImageUsageFlags requiredEqmapImageUsageFlags = vk::ImageUsageFlagBits::eSampled;

        /**
         * @brief Required image usage flags for cubemap image.
         */
        static constexpr vk::ImageUsageFlags requiredCubemapImageUsageFlags = vk::ImageUsageFlagBits::eStorage;

        CubemapComputer(
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

        vku::DescriptorSetLayout<vk::DescriptorType::eCombinedImageSampler, vk::DescriptorType::eStorageImage> descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;
        vk::raii::ImageView eqmapImageView;
        vk::raii::ImageView cubemapImageView;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

cubemap::CubemapComputer::CubemapComputer(
    const vk::raii::Device &device,
    const vku::Image &eqmapImage,
    vk::Sampler eqmapSampler,
    const vku::Image &cubemapImage
) : device { device },
    cubemapImage { cubemapImage },
    descriptorSetLayout { device, vk::DescriptorSetLayoutCreateInfo {
        vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptor,
        vku::unsafeProxy(decltype(descriptorSetLayout)::getBindings(
            { 1, vk::ShaderStageFlagBits::eCompute, &eqmapSampler },
            { 1, vk::ShaderStageFlagBits::eCompute })),
    } },
    pipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        *descriptorSetLayout,
    } },
    pipeline { device, nullptr, vk::ComputePipelineCreateInfo {
        {},
        createPipelineStages(
            device,
            vku::Shader { shader::cubemap_comp, vk::ShaderStageFlagBits::eCompute }).get()[0],
        *pipelineLayout,
    } },
    eqmapImageView { device, eqmapImage.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }) },
    cubemapImageView { device, cubemapImage.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6 }, vk::ImageViewType::eCube) } { }

void cubemap::CubemapComputer::setEqmapImage(const vku::Image &eqmapImage) {
    eqmapImageView = { device, eqmapImage.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }) };
}

void cubemap::CubemapComputer::setCubemapImage(const vku::Image &cubemapImage) {
    this->cubemapImage = cubemapImage;
    cubemapImageView = { device, cubemapImage.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6 }, vk::ImageViewType::eCube) };
}

void cubemap::CubemapComputer::recordCommands(vk::CommandBuffer computeCommandBuffer) const {
    const auto *d = device.get().getDispatcher();

    computeCommandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline, *d);
    computeCommandBuffer.pushDescriptorSetKHR(
        vk::PipelineBindPoint::eCompute, *pipelineLayout,
        0, {
            decltype(descriptorSetLayout)::getWriteOne<0>({ {}, *eqmapImageView, vk::ImageLayout::eShaderReadOnlyOptimal }),
            decltype(descriptorSetLayout)::getWriteOne<1>({ {}, *cubemapImageView, vk::ImageLayout::eGeneral }),
        }, *d);
    computeCommandBuffer.dispatch(cubemapImage.get().extent.width / 16U, cubemapImage.get().extent.height / 16U, 6, *d);
}