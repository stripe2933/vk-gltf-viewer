#include <ktx.h>
#include <vulkan/vulkan_hpp_macros.hpp>

import std;
import vku;

struct QueueFamilies {
    std::uint32_t compute;

    explicit QueueFamilies(vk::PhysicalDevice physicalDevice)
        : compute { vku::getComputeQueueFamily(physicalDevice.getQueueFamilyProperties()).value() } { }
};

struct Queues {
    vk::Queue compute;

    Queues(vk::Device device, const QueueFamilies& queueFamilies) noexcept
        : compute { device.getQueue(queueFamilies.compute, 0) } { }

    [[nodiscard]] static vku::RefHolder<vk::DeviceQueueCreateInfo> getCreateInfos(
        vk::PhysicalDevice,
        const QueueFamilies &queueFamilies
    ) noexcept {
        return vku::RefHolder { [&]() {
            static constexpr float priority = 1.f;
            return vk::DeviceQueueCreateInfo {
                {},
                queueFamilies.compute,
                vk::ArrayProxyNoTemporaries<const float> { priority },
            };
        } };
    }
};

int main(int argc, char **argv) {
    if (argc != 2) {
        std::println(std::cerr, "Usage: ./brdfgen <output.ktx2>");
        return 1;
    }

    VULKAN_HPP_DEFAULT_DISPATCHER.init();

    const vk::raii::Context context;

    const vk::raii::Instance instance { context, vk::InstanceCreateInfo{
#if __APPLE__
        vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR,
#else
        {},
#endif
        vku::unsafeAddress(vk::ApplicationInfo {
            "brdfgen", 0,
            nullptr, 0,
            vk::makeApiVersion(0, 1, 2, 0),
        }),
        {},
#if __APPLE__
        vk::KHRPortabilityEnumerationExtensionName,
#endif
    } };
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);

    const vku::Gpu<QueueFamilies, Queues> gpu { instance, {
        .deviceExtensions = {
            vk::KHRPushDescriptorExtensionName,
#if __APPLE__
            vk::KHRPortabilitySubsetExtensionName,
#endif
        },
    } };

    using DescriptorSetLayout = vku::DescriptorSetLayout<vk::DescriptorType::eStorageImage>;
    const DescriptorSetLayout descriptorSetLayout {
        gpu.device,
        vk::DescriptorSetLayoutCreateInfo {
            vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
            vku::unsafeProxy(DescriptorSetLayout::getBindings({ 1, vk::ShaderStageFlagBits::eCompute })),
        },
    };

    const vk::raii::PipelineLayout pipelineLayout { gpu.device, vk::PipelineLayoutCreateInfo {
        {},
        *descriptorSetLayout,
    } };

    const vk::raii::Pipeline pipeline { gpu.device, nullptr, vk::ComputePipelineCreateInfo {
        {},
        createPipelineStages(
            gpu.device,
            vku::Shader::fromSpirvFile(COMPILED_SHADER_DIR "/brdfmap.comp.spv", vk::ShaderStageFlagBits::eCompute)).get()[0],
        *pipelineLayout,
    } };

    const vku::AllocatedImage outputImage { gpu.allocator, vk::ImageCreateInfo {
        {},
        vk::ImageType::e2D,
        vk::Format::eR16G16Unorm,
        vk::Extent3D { 128, 128, 1 },
        1, 1,
        vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc,
    } };
    const vk::raii::ImageView outputImageView { gpu.device, outputImage.getViewCreateInfo() };

    const vku::MappedBuffer destagingBuffer { gpu.allocator, vk::BufferCreateInfo {
        {},
        vk::blockSize(outputImage.format) * outputImage.extent.width * outputImage.extent.height,
        vk::BufferUsageFlagBits::eTransferDst,
    }, vku::allocation::hostRead };

    const vk::raii::CommandPool computeCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.compute } };
    vku::executeSingleCommand(*gpu.device, *computeCommandPool, gpu.queues.compute, [&](vk::CommandBuffer cb) {
        cb.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
            {}, {}, {},
            vk::ImageMemoryBarrier {
                {}, vk::AccessFlagBits::eShaderWrite,
                {}, vk::ImageLayout::eGeneral,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                outputImage, vku::fullSubresourceRange(),
            });

        cb.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
        cb.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0,
            vku::DescriptorSet<DescriptorSetLayout>{}.getWriteOne<0>({ {}, *outputImageView, vk::ImageLayout::eGeneral }));
        cb.dispatch(outputImage.extent.width / 16, outputImage.extent.height / 16, 1);

        cb.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTransfer,
            {}, {}, {},
            vk::ImageMemoryBarrier {
                vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eTransferRead,
                vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                outputImage, vku::fullSubresourceRange(),
            });

        cb.copyImageToBuffer(
            outputImage, vk::ImageLayout::eTransferSrcOptimal,
            destagingBuffer,
            vk::BufferImageCopy {
                0, 0, 0,
                { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
                { 0, 0, 0 },
                outputImage.extent,
            });

        cb.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost,
            {}, {},
            vk::BufferMemoryBarrier {
                vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eHostRead,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                destagingBuffer, 0, vk::WholeSize,
            }, {});
    });
    gpu.device.waitIdle();

    ktxTexture2 *texture;
    ktxTextureCreateInfo createInfo {
        .vkFormat = VK_FORMAT_R16G16_UNORM,
        .baseWidth = outputImage.extent.width,
        .baseHeight = outputImage.extent.height,
        .baseDepth = 1,
        .numDimensions = 2,
        .numLevels = 1,
        .numLayers = 1,
        .numFaces = 1,
        .isArray = false,
        .generateMipmaps = false,
    };
    if (auto result = ktxTexture2_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture); result != KTX_SUCCESS) {
        std::println("Failed to create KTX texture: {}", ktxErrorString(result));
        return 1;
    }

    if (auto result = ktxTexture_SetImageFromMemory(ktxTexture(texture), 0, 0, 0, static_cast<const ktx_uint8_t*>(destagingBuffer.data), destagingBuffer.size); result != KTX_SUCCESS) {
        std::println("Failed to set image data to KTX texture: {}", ktxErrorString(result));
        return 1;
    }

    ktxTexture_WriteToNamedFile(ktxTexture(texture), argv[1]);
    ktxTexture_Destroy(ktxTexture(texture));
}