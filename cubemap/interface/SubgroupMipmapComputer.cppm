module;

#include <lifetimebound.hpp>

export module cubemap:SubgroupMipmapComputer;

import std;
export import vku;
import :shader.subgroup_mipmap_comp;

namespace cubemap {
    export class SubgroupMipmapComputer {
    public:
        struct Config {
            /**
             * @brief Subgroup size of <tt>vk::PhysicalDevice</tt>. Must be either one of 16, 32 or 64.
             */
            std::uint32_t subgroupSize;

            /**
             * @brief Boolean indicates whether to utilize <tt>VK_AMD_shader_image_load_store_lod</tt> extension.
             *
             * <tt>SubgroupMipmapComputer</tt> writes 5 mip levels of a storage image at once, therefore
             * <tt>vk::ImageView</tt> for each mip level have to be bound to the pipeline via descriptor array. The
             * number of storage image descriptor bind count may exceed the system limit (especially in MoltenVK prior
             * to v1.2.11). If the extension is supported, compute shader can access the arbitrary mip level of the
             * storage image, and can reduce the descriptor binding count.
             */
            bool useShaderImageLoadStoreLod;
        };

        static constexpr vk::ImageUsageFlags requiredImageUsageFlags = vk::ImageUsageFlagBits::eStorage;

        SubgroupMipmapComputer(
            const vk::raii::Device &device LIFETIMEBOUND,
            const vku::Image &image LIFETIMEBOUND,
            const Config &config
        ) : config { config },
            device { device },
            image { image },
            descriptorSetLayout { createDescriptorSetLayout() },
            pipelineLayout { createPipelineLayout() },
            pipeline { createPipeline() },
            mipImageViews { createMipImageViews() } { }

        void setImage(const vku::Image &image LIFETIME_CAPTURE_BY(this)) {
            const bool descriptorSetLayoutChanged = !config.useShaderImageLoadStoreLod && (this->image.get().mipLevels != image.mipLevels);
            this->image = image;
            if (descriptorSetLayoutChanged) {
                descriptorSetLayout = createDescriptorSetLayout();
                pipelineLayout = createPipelineLayout();
                pipeline = createPipeline();
            }
            mipImageViews = createMipImageViews();
        }

        void recordCommands(vk::CommandBuffer computeCommandBuffer) const {
            const auto *d = device.get().getDispatcher();

            // Base image size must be greater than or equal to 32. Therefore, the first execution may process less than 5 mip levels.
            // For example, if base extent is 4096x4096 (mipLevels=13),
            // Step 0 (4096 -> 1024)
            // Step 1 (1024 -> 32)
            // Step 2 (32 -> 1) (full processing required)

#if __cpp_lib_ranges_chunk >= 202202L
            const std::vector indexChunks
                = std::views::iota(1, static_cast<std::int32_t>(image.get().mipLevels))   // [1, 2, ..., 11, 12]
                | std::views::reverse                                                     // [12, 11, ..., 2, 1]
                | std::views::chunk(5)                                                    // [[12, 11, 10, 9, 8], [7, 6, 5, 4, 3], [2, 1]]
                | std::views::transform([](auto &&chunk) {
                     return chunk | std::views::reverse | std::ranges::to<std::vector>();
                })                                                                        // [[8, 9, 10, 11, 12], [3, 4, 5, 6, 7], [1, 2]]
                | std::views::reverse                                                     // [[1, 2], [3, 4, 5, 6, 7], [8, 9, 10, 11, 12]]
                | std::ranges::to<std::vector>();
#else
            std::vector<std::vector<std::int32_t>> indexChunks;
            for (int endMipLevel = image.get().mipLevels; endMipLevel > 1; endMipLevel -= 5) {
                indexChunks.emplace_back(std::from_range, std::views::iota(std::max(1, endMipLevel - 5), endMipLevel));
            }
            std::ranges::reverse(indexChunks);
#endif

            computeCommandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline, *d);
            computeCommandBuffer.pushDescriptorSetKHR(
                vk::PipelineBindPoint::eCompute, *pipelineLayout,
                0, decltype(descriptorSetLayout)::getWrite<0>(vku::unsafeProxy(
                    mipImageViews
                        | std::views::transform([](vk::ImageView view) {
                            return vk::DescriptorImageInfo { {}, view, vk::ImageLayout::eGeneral };
                        })
                        | std::ranges::to<std::vector>())), *d);

            // TODO.CXX23: use std::views::enumerate.
            for (int idx = 0; std::span mipIndices : indexChunks) {
                if (idx != 0) {
                    computeCommandBuffer.pipelineBarrier(
                        vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                        {},
                        vk::MemoryBarrier {
                            vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead,
                        },
                        {}, {}, *d);
                }

                computeCommandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, PushConstant {
                    mipIndices.front() - 1,
                    static_cast<std::uint32_t>(mipIndices.size()),
                }, *d);
                computeCommandBuffer.dispatch(
                    (image.get().extent.width >> mipIndices.front()) / 16U,
                    (image.get().extent.height >> mipIndices.front()) / 16U,
                    image.get().arrayLayers,
                    *d);

                ++idx;
            }
        }

    private:
        struct PushConstant {
            std::int32_t baseLevel;
            std::uint32_t remainingMipLevels;
        };

        Config config;

        std::reference_wrapper<const vk::raii::Device> device;
        std::reference_wrapper<const vku::Image> image;

        vku::DescriptorSetLayout<vk::DescriptorType::eStorageImage> descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;
        std::vector<vk::raii::ImageView> mipImageViews;

        [[nodiscard]] vku::DescriptorSetLayout<vk::DescriptorType::eStorageImage> createDescriptorSetLayout() const {
            return { device, vk::DescriptorSetLayoutCreateInfo {
                vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
                vku::unsafeProxy(decltype(descriptorSetLayout)::getBindings(
                    { config.useShaderImageLoadStoreLod ? 1U : image.get().mipLevels, vk::ShaderStageFlagBits::eCompute })),
            } };
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
                            ? std::span<const std::uint32_t> { shader::subgroup_mipmap_comp<1> }
                            : std::span<const std::uint32_t> { shader::subgroup_mipmap_comp<0> },
                        vk::ShaderStageFlagBits::eCompute,
                        vku::unsafeAddress(vk::SpecializationInfo {
                            vku::unsafeProxy(vk::SpecializationMapEntry {
                                0, 0, sizeof(std::uint32_t),
                            }),
                            vk::ArrayProxyNoTemporaries<const std::uint32_t> { config.subgroupSize },
                        }),
                    }).get()[0],
                *pipelineLayout,
            } };
        }

        [[nodiscard]] std::vector<vk::raii::ImageView> createMipImageViews() const {
            std::vector<vk::raii::ImageView> result;
            if (config.useShaderImageLoadStoreLod) {
                result.emplace_back(device, image.get().getViewCreateInfo(vk::ImageViewType::e2DArray));
            }
            else {
                result.append_range(image.get().getMipViewCreateInfos(vk::ImageViewType::e2DArray)
                    | std::views::transform([this](const vk::ImageViewCreateInfo &createInfo) {
                        return vk::raii::ImageView { device, createInfo };
                    }));
            }
            return result;
        }
    };
}