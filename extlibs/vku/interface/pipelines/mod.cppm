export module vku:pipelines;

import std;
export import vulkan_hpp;
export import :pipelines.Shader;
import :utils.RefHolder;

#define INDEX_SEQ(Is, N, ...) [&]<std::size_t ...Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define ARRAY_OF(N, ...) INDEX_SEQ(Is, N, { return std::array { ((void)Is, __VA_ARGS__)... }; })

namespace vku {
    export template <std::convertible_to<Shader>... Shaders>
    [[nodiscard]] auto createPipelineStages(
        const vk::raii::Device &device,
        const Shaders &...shaders
    ) {
        return RefHolder {
            [&](const auto &...shaderModules) {
                return std::array { vk::PipelineShaderStageCreateInfo {
                    {},
                    shaders.stage,
                    *shaderModules,
                    shaders.entryPoint,
                }... };
            },
            vk::raii::ShaderModule { device, vk::ShaderModuleCreateInfo {
                {},
                shaders.code,
            } }...
        };
    }

    export
    [[nodiscard]] auto getDefaultGraphicsPipelineCreateInfo(
        vk::ArrayProxyNoTemporaries<const vk::PipelineShaderStageCreateInfo> stages,
        vk::PipelineLayout layout,
        std::uint32_t colorAttachmentCount = 0,
        bool hasDepthStencilAttachemnt = false,
        vk::SampleCountFlagBits multisample = vk::SampleCountFlagBits::e1
    ) -> vk::GraphicsPipelineCreateInfo;
}

// module:private;

auto vku::getDefaultGraphicsPipelineCreateInfo(
    vk::ArrayProxyNoTemporaries<const vk::PipelineShaderStageCreateInfo> stages,
    vk::PipelineLayout layout,
    uint32_t colorAttachmentCount,
    bool hasDepthStencilAttachemnt,
    vk::SampleCountFlagBits multisample
) -> vk::GraphicsPipelineCreateInfo {
    static constexpr vk::PipelineVertexInputStateCreateInfo vertexInputState {};

    static constexpr vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState {
        {},
        vk::PrimitiveTopology::eTriangleList,
    };

    static constexpr vk::PipelineViewportStateCreateInfo viewportState {
        {},
        1, {},
        1, {},
    };

    static constexpr vk::PipelineRasterizationStateCreateInfo rasterizationState {
        {},
        {}, {},
        vk::PolygonMode::eFill,
        vk::CullModeFlagBits::eBack, {},
        {}, {}, {}, {},
        1.f,
    };

    static constexpr std::array multisampleStates = std::apply([](auto... args) {
        return std::array { vk::PipelineMultisampleStateCreateInfo { {}, args }... };
    }, std::array {
        vk::SampleCountFlagBits::e1,
        vk::SampleCountFlagBits::e2,
        vk::SampleCountFlagBits::e4,
        vk::SampleCountFlagBits::e8,
        vk::SampleCountFlagBits::e16,
        vk::SampleCountFlagBits::e32,
        vk::SampleCountFlagBits::e64,
    });

    static constexpr vk::PipelineDepthStencilStateCreateInfo depthStencilState {};

    constexpr std::uint32_t MAX_COLOR_ATTACHMENT_COUNT = 8;
    if (colorAttachmentCount > MAX_COLOR_ATTACHMENT_COUNT) {
        throw std::runtime_error { "Color attachment count exceeds maximum" };
    }
    static constexpr std::array colorBlendAttachments
        = ARRAY_OF(MAX_COLOR_ATTACHMENT_COUNT + 1, vk::PipelineColorBlendAttachmentState {
            {},
            {}, {}, {},
            {}, {}, {},
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
        });
    static constexpr std::array colorBlendStates
        = INDEX_SEQ(Is, MAX_COLOR_ATTACHMENT_COUNT + 1, {
            return std::array {
                vk::PipelineColorBlendStateCreateInfo {
                    {},
                    {}, {},
                    Is, colorBlendAttachments.data(),
                }...
            };
        });

    static constexpr std::array dynamicStates {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
    };
    static constexpr vk::PipelineDynamicStateCreateInfo dynamicState {
        {},
        dynamicStates.size(), dynamicStates.data(),
    };

    return {
        {},
        stages,
        &vertexInputState,
        &inputAssemblyState,
        {},
        &viewportState,
        &rasterizationState,
        &multisampleStates[std::countr_zero(static_cast<std::underlying_type_t<vk::SampleCountFlagBits>>(multisample))],
        hasDepthStencilAttachemnt ? &depthStencilState : nullptr,
        &colorBlendStates[colorAttachmentCount],
        &dynamicState,
        layout,
    };
}