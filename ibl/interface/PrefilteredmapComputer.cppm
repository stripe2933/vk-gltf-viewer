module;

#include <cstddef>

#include <lifetimebound.hpp>

export module ibl:PrefilteredmapComputer;

import std;
export import vku;
import :shader.prefilteredmap_comp;

namespace ibl {
    export class PrefilteredmapComputer {
    public:
        struct SpecializationConstants {
            std::uint32_t samples;
        };

        struct Config {
            /**
             * @brief Boolean indicates whether to utilize <tt>VK_AMD_shader_image_load_store_lod</tt> extension.
             *
             * <tt>PrefilteredmapComputer</tt> writes to the mip level of given roughness level, therefore
             * <tt>vk::ImageView</tt> for each mip level have to be bound to the pipeline via descriptor array. The
             * number of storage image descriptor bind count may exceed the system limit (especially in MoltenVK prior
             * to v1.2.11). If the extension is supported, compute shader can access the arbitrary mip level of the
             * storage image, and can reduce the descriptor binding count.
             */
            bool useShaderImageLoadStoreLod;

            /**
             * @brief Use <tt>vk::ImageLayout::eGeneral</tt> layout for input cubemap image during sampling.
             *
             * If this is <tt>false</tt>, the input cubemap image layout will be supposed to
             * <tt>vk::ImageLayout::eShaderReadOnlyOptimal</tt> during compute shader dispatch.
             */
            bool useGeneralImageLayout;

            SpecializationConstants specializationConstants;
        };

        static constexpr vk::ImageUsageFlags requiredCubemapImageUsageFlags = vk::ImageUsageFlagBits::eSampled;
        static constexpr vk::ImageUsageFlags requiredPrefilteredmapImageUsageFlags = vk::ImageUsageFlagBits::eStorage;

        PrefilteredmapComputer(
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

        void setCubemapImage(const vku::Image &cubemapImage LIFETIME_CAPTURE_BY(this)) {
            cubemapImageView = { device, cubemapImage.getViewCreateInfo(vk::ImageViewType::eCube) };
        }

        void setPrefilteredmapImage(const vku::Image &prefilteredmapImage LIFETIME_CAPTURE_BY(this)) {
            const bool descriptorSetLayoutChanged = !config.useShaderImageLoadStoreLod && (this->prefilteredmapImage.get().mipLevels != prefilteredmapImage.mipLevels);
            this->prefilteredmapImage = prefilteredmapImage;
            if (descriptorSetLayoutChanged) {
                descriptorSetLayout = createDescriptorSetLayout();
                pipelineLayout = createPipelineLayout();
                pipeline = createPipeline();
            }
            prefilteredmapMipImageViews = createPrefilteredmapMipImageViews();
        }

        void recordCommands(vk::CommandBuffer computeCommandBuffer) const {
            const auto *d = device.get().getDispatcher();

            computeCommandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline, *d);
            computeCommandBuffer.pushDescriptorSetKHR(
                vk::PipelineBindPoint::eCompute, *pipelineLayout,
                0, {
                    decltype(descriptorSetLayout)::getWriteOne<0>({ {}, *cubemapImageView, config.useGeneralImageLayout ? vk::ImageLayout::eGeneral : vk::ImageLayout::eShaderReadOnlyOptimal }),
                    decltype(descriptorSetLayout)::getWrite<1>(vku::unsafeProxy(prefilteredmapMipImageViews
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

                constexpr auto divCeil = [](std::uint32_t num, std::uint32_t denom) noexcept {
                    return (num / denom) + (num % denom != 0);
                };
                const std::uint32_t groupCountXY = divCeil(prefilteredmapImage.get().extent.width >> level, 16U);
                computeCommandBuffer.dispatch(groupCountXY, groupCountXY, 6, *d);
            }

        }

    private:
        struct PushConstant {
            std::int32_t mipLevel;
            float roughness;
        };

        Config config;
        std::reference_wrapper<const vk::raii::Device> device;
        std::reference_wrapper<const vku::Image> prefilteredmapImage;
        vk::raii::Sampler cubemapSampler;
        vku::DescriptorSetLayout<vk::DescriptorType::eCombinedImageSampler, vk::DescriptorType::eStorageImage> descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;
        vk::raii::ImageView cubemapImageView;
        std::vector<vk::raii::ImageView> prefilteredmapMipImageViews;

        [[nodiscard]] vku::DescriptorSetLayout<vk::DescriptorType::eCombinedImageSampler, vk::DescriptorType::eStorageImage> createDescriptorSetLayout() const {
            return {
                device,
                vk::DescriptorSetLayoutCreateInfo {
                    vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
                    vku::unsafeProxy(decltype(descriptorSetLayout)::getBindings(
                        { 1, vk::ShaderStageFlagBits::eCompute, &*cubemapSampler },
                        { config.useShaderImageLoadStoreLod ? 1U : prefilteredmapImage.get().mipLevels, vk::ShaderStageFlagBits::eCompute })),
                },
            };
        }

        [[nodiscard]] vk::raii::PipelineLayout createPipelineLayout() const {
            return { device, vk::PipelineLayoutCreateInfo {
                {},
                *descriptorSetLayout,
                vku::unsafeProxy(vk::PushConstantRange {
                    vk::ShaderStageFlagBits::eCompute,
                    0, sizeof(PushConstant),
                }),
            } };
        }

        [[nodiscard]] vk::raii::Pipeline createPipeline() const {
            return { device, nullptr, vk::ComputePipelineCreateInfo {
                {},
                createPipelineStages(
                    device,
                    vku::Shader {
                        config.useShaderImageLoadStoreLod
                            ? std::span<const std::uint32_t> { shader::prefilteredmap_comp<1> }
                            : std::span<const std::uint32_t> { shader::prefilteredmap_comp<0> },
                        vk::ShaderStageFlagBits::eCompute,
                        vku::unsafeAddress(vk::SpecializationInfo {
                            // TODO: use vku::SpecializationMap when available.
                            vku::unsafeProxy({
                                vk::SpecializationMapEntry { 0, offsetof(SpecializationConstants, samples), sizeof(SpecializationConstants::samples) },
                            }),
                            vk::ArrayProxyNoTemporaries<const SpecializationConstants> { config.specializationConstants },
                        }),
                    }).get()[0],
                *pipelineLayout,
            } };
        }

        [[nodiscard]] std::vector<vk::raii::ImageView> createPrefilteredmapMipImageViews() const {
            std::vector<vk::raii::ImageView> result;
            if (config.useShaderImageLoadStoreLod) {
                result.emplace_back(device, prefilteredmapImage.get().getViewCreateInfo(vk::ImageViewType::eCube));
            }
            else {
                result.append_range(prefilteredmapImage.get().getMipViewCreateInfos(vk::ImageViewType::eCube)
                    | std::views::transform([this](const vk::ImageViewCreateInfo &createInfo) {
                        return vk::raii::ImageView { device, createInfo };
                    }));
            }
            return result;
        }
    };
}