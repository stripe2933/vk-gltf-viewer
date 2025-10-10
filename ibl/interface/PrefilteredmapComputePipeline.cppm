module;

#include <cstddef>

#include <lifetimebound.hpp>

export module ibl.PrefilteredmapComputePipeline;

import std;
export import vku;

import ibl.shader.prefilteredmap_comp;

namespace ibl {
    export class PrefilteredmapComputePipeline {
    public:
        struct SpecializationConstants {
            std::uint32_t samples = 1024;
        };

        struct Config {
            /**
             * @brief Boolean indicates whether to utilize <tt>VK_AMD_shader_image_load_store_lod</tt> extension.
             *
             * <tt>PrefilteredmapComputePipeline</tt> writes to the mip level of given roughness level, therefore
             * <tt>vk::ImageView</tt> for each mip level have to be bound to the pipeline via descriptor array. The
             * number of storage image descriptor bind count may exceed the system limit (especially in MoltenVK prior
             * to v1.2.11). If the extension is supported, compute shader can access the arbitrary mip level of the
             * storage image, and can reduce the descriptor binding count.
             */
            bool useShaderImageLoadStoreLod = false;

            SpecializationConstants specializationConstants = {};
        };

        static constexpr vk::ImageUsageFlags requiredCubemapImageUsageFlags = vk::ImageUsageFlagBits::eSampled;
        static constexpr vk::ImageUsageFlags requiredPrefilteredmapImageUsageFlags = vk::ImageUsageFlagBits::eStorage;

        PrefilteredmapComputePipeline(
            const vk::raii::Device &device LIFETIMEBOUND,
            const vku::Image &cubemapImage LIFETIMEBOUND,
            const vku::Image &prefilteredmapImage LIFETIMEBOUND,
            const Config &config = {
                .useShaderImageLoadStoreLod = false,
                .specializationConstants = {
                    .samples = 1024,
                },
            }
        );

        void setCubemapImage(const vku::Image &cubemapImage LIFETIME_CAPTURE_BY(this));
        void setPrefilteredmapImage(const vku::Image &prefilteredmapImage LIFETIME_CAPTURE_BY(this));

        void recordCommands(vk::CommandBuffer computeCommandBuffer) const;

    private:
        struct PushConstant;

        Config config;
        std::reference_wrapper<const vk::raii::Device> device;
        std::reference_wrapper<const vku::Image> prefilteredmapImage;
        vk::raii::Sampler cubemapSampler;
        vku::raii::DescriptorSetLayout<vk::DescriptorType::eCombinedImageSampler, vk::DescriptorType::eStorageImage> descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;
        vk::raii::ImageView cubemapImageView;
        std::vector<vk::raii::ImageView> prefilteredmapMipImageViews;

        [[nodiscard]] vku::raii::DescriptorSetLayout<vk::DescriptorType::eCombinedImageSampler, vk::DescriptorType::eStorageImage> createDescriptorSetLayout() const;
        [[nodiscard]] vk::raii::PipelineLayout createPipelineLayout() const;
        [[nodiscard]] vk::raii::Pipeline createPipeline() const;
        [[nodiscard]] std::vector<vk::raii::ImageView> createPrefilteredmapMipImageViews() const;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

struct ibl::PrefilteredmapComputePipeline::PushConstant {
    std::int32_t mipLevel;
    float roughness;
};

ibl::PrefilteredmapComputePipeline::PrefilteredmapComputePipeline(
    const vk::raii::Device &device LIFETIMEBOUND,
    const vku::Image &cubemapImage LIFETIMEBOUND,
    const vku::Image &prefilteredmapImage LIFETIMEBOUND,
    const Config &config
) : config { config },
    device { device },
    prefilteredmapImage { prefilteredmapImage },
    cubemapSampler { device, vk::SamplerCreateInfo { {}, vk::Filter::eLinear, vk::Filter::eLinear }.setMaxLod(vk::LodClampNone) },
    descriptorSetLayout { createDescriptorSetLayout() },
    pipelineLayout { createPipelineLayout() },
    pipeline { createPipeline() },
    cubemapImageView { device, cubemapImage.getViewCreateInfo(vk::ImageViewType::eCube) },
    prefilteredmapMipImageViews { createPrefilteredmapMipImageViews() } { }

void ibl::PrefilteredmapComputePipeline::setCubemapImage(const vku::Image &cubemapImage LIFETIME_CAPTURE_BY(this)) {
    cubemapImageView = { device, cubemapImage.getViewCreateInfo(vk::ImageViewType::eCube) };
}

void ibl::PrefilteredmapComputePipeline::setPrefilteredmapImage(const vku::Image &prefilteredmapImage LIFETIME_CAPTURE_BY(this)) {
    const bool descriptorSetLayoutChanged = !config.useShaderImageLoadStoreLod && (this->prefilteredmapImage.get().mipLevels != prefilteredmapImage.mipLevels);
    this->prefilteredmapImage = prefilteredmapImage;
    if (descriptorSetLayoutChanged) {
        descriptorSetLayout = createDescriptorSetLayout();
        pipelineLayout = createPipelineLayout();
        pipeline = createPipeline();
    }
    prefilteredmapMipImageViews = createPrefilteredmapMipImageViews();
}

void ibl::PrefilteredmapComputePipeline::recordCommands(vk::CommandBuffer computeCommandBuffer) const {
    const auto *d = device.get().getDispatcher();

    computeCommandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline, *d);
    computeCommandBuffer.pushDescriptorSetKHR(
        vk::PipelineBindPoint::eCompute, *pipelineLayout,
        0, {
            decltype(descriptorSetLayout)::getWriteDescriptorSet<0>({}, 0, vku::lvalue(vk::DescriptorImageInfo { {}, *cubemapImageView, vk::ImageLayout::eShaderReadOnlyOptimal })),
            decltype(descriptorSetLayout)::getWriteDescriptorSet<1>({}, 0, vku::lvalue(prefilteredmapMipImageViews
                | std::views::transform([](vk::ImageView view) {
                    return vk::DescriptorImageInfo { {}, view, vk::ImageLayout::eGeneral };
                })
                | std::ranges::to<std::vector>())),
        }, *d);

    for (std::uint32_t level = 0; level < prefilteredmapImage.get().mipLevels; ++level) {
        computeCommandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, PushConstant {
            .mipLevel = static_cast<std::int32_t>(level),
            .roughness = static_cast<float>(level) / (prefilteredmapImage.get().mipLevels - 1),
        }, *d);

        const std::uint32_t groupCountXY = vku::divCeil(prefilteredmapImage.get().extent.width >> level, 16U);
        computeCommandBuffer.dispatch(groupCountXY, groupCountXY, 6, *d);
    }
}

[[nodiscard]] vku::raii::DescriptorSetLayout<vk::DescriptorType::eCombinedImageSampler, vk::DescriptorType::eStorageImage> ibl::PrefilteredmapComputePipeline::createDescriptorSetLayout() const {
    return {
        device,
        vk::DescriptorSetLayoutCreateInfo {
            vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
            vku::lvalue({
                decltype(descriptorSetLayout)::getCreateInfoBinding<0>(vk::ShaderStageFlagBits::eCompute, *cubemapSampler),
                decltype(descriptorSetLayout)::getCreateInfoBinding<1>(config.useShaderImageLoadStoreLod ? 1U : prefilteredmapImage.get().mipLevels, vk::ShaderStageFlagBits::eCompute),
            }),
        },
    };
}

[[nodiscard]] vk::raii::PipelineLayout ibl::PrefilteredmapComputePipeline::createPipelineLayout() const {
    return { device, vk::PipelineLayoutCreateInfo {
        {},
        *descriptorSetLayout,
        vku::lvalue(vk::PushConstantRange {
            vk::ShaderStageFlagBits::eCompute,
            0, sizeof(PushConstant),
        }),
    } };
}

[[nodiscard]] vk::raii::Pipeline ibl::PrefilteredmapComputePipeline::createPipeline() const {
    return { device, nullptr, vk::ComputePipelineCreateInfo {
        {},
        vk::PipelineShaderStageCreateInfo {
            {},
            vk::ShaderStageFlagBits::eCompute,
            *vku::lvalue(vk::raii::ShaderModule { device, vk::ShaderModuleCreateInfo {
                {},
                vku::lvalue(config.useShaderImageLoadStoreLod
                    ? std::span<const std::uint32_t> { shader::prefilteredmap_comp<1> }
                    : std::span<const std::uint32_t> { shader::prefilteredmap_comp<0> }),
            } }),
            "main",
            &vku::lvalue(vk::SpecializationInfo {
                vku::lvalue(vk::SpecializationMapEntry { 0, offsetof(SpecializationConstants, samples), sizeof(SpecializationConstants::samples) }),
                vk::ArrayProxyNoTemporaries<const SpecializationConstants> { config.specializationConstants },
            }),
        },
        *pipelineLayout,
    } };
}

[[nodiscard]] std::vector<vk::raii::ImageView> ibl::PrefilteredmapComputePipeline::createPrefilteredmapMipImageViews() const {
    std::vector<vk::raii::ImageView> result;
    if (config.useShaderImageLoadStoreLod) {
        result.emplace_back(device, prefilteredmapImage.get().getViewCreateInfo(vk::ImageViewType::eCube));
    }
    else {
        result.append_range(prefilteredmapImage.get().getPerMipLevelViewCreateInfos(vk::ImageViewType::eCube)
            | std::views::transform([this](const vk::ImageViewCreateInfo &createInfo) {
                return vk::raii::ImageView { device, createInfo };
            }));
    }
    return result;
}