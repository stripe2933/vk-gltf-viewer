module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.texture.ImGuiColorSpaceAndUsageCorrectedTextures;

import std;
export import imgui.vulkan;
import :helpers.fastgltf;
import :helpers.functional;
import :helpers.ranges;
import :helpers.vulkan;
export import :imgui.ColorSpaceAndUsageCorrectedTextures;
export import :vulkan.texture.Textures;

namespace vk_gltf_viewer::vulkan::texture {
    export class ImGuiColorSpaceAndUsageCorrectedTextures final : public imgui::ColorSpaceAndUsageCorrectedTextures {
        std::deque<vk::raii::ImageView> imageViews;
        std::vector<vk::DescriptorSet> textureDescriptorSets;
        std::vector<std::array<vk::DescriptorSet, 5>> materialTextureDescriptorSets; // [0] -> metallic, [1] -> roughness, [2] -> normal, [3] -> occlusion, [4] -> emissive

    public:
        ImGuiColorSpaceAndUsageCorrectedTextures(
            const fastgltf::Asset &asset LIFETIMEBOUND,
            const Textures &textures LIFETIMEBOUND,
            const Gpu &gpu LIFETIMEBOUND
        ) : ColorSpaceAndUsageCorrectedTextures { !gpu.supportSwapchainMutableFormat } {
            textureDescriptorSets
                = asset.textures
                | ranges::views::enumerate
                | std::views::transform(decomposer([&](std::size_t textureIndex, const fastgltf::Texture &texture) -> vk::DescriptorSet {
                    auto [sampler, imageView, _] = textures.descriptorInfos[textureIndex];

                    const vku::Image &image = textures.images.images.at(getPreferredImageIndex(texture));
                    if (srgbColorAttachment != isSrgbFormat(image.format)) {
                        // Image view format is incompatible, need to be regenerated.
                        const vk::ComponentMapping components = [&]() -> vk::ComponentMapping {
                            switch (componentCount(image.format)) {
                                case 1:
                                    // Grayscale: red channel have to be propagated to green/blue channels.
                                    return { vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eOne };
                                case 2:
                                    // Grayscale \w alpha: red channel have to be propagated to green/blue channels, and alpha channel uses given green value.
                                    return { vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG };
                                case 4:
                                    // RGB or RGBA.
                                    return {};
                                default:
                                    std::unreachable();
                            }
                        }();
                        imageView = *imageViews.emplace_back(
                            gpu.device,
                            image.getViewCreateInfo().setFormat(convertSrgb(image.format)).setComponents(components));
                    }

                    return ImGui_ImplVulkan_AddTexture(sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }))
                | std::ranges::to<std::vector>();

            const auto getImage = [&](std::size_t textureIndex) -> const vku::Image& {
                const std::size_t imageIndex = getPreferredImageIndex(asset.textures[textureIndex]);
                return textures.images.images.at(imageIndex);
            };

            materialTextureDescriptorSets.resize(asset.materials.size());
            for (const auto &[materialIndex, material] : asset.materials | ranges::views::enumerate) {
                if (const auto &textureInfo = material.pbrData.metallicRoughnessTexture) {
                    const vku::Image &image = getImage(textureInfo->textureIndex);

                    // Metallic/roughness texture is non-sRGB by default, therefore image view format must be mutated
                    // if color space is sRGB.
                    vk::Format colorSpaceCompatibleFormat = image.format;
                    if (srgbColorAttachment) {
                        colorSpaceCompatibleFormat = convertSrgb(image.format);
                    }

                    // Metallic.
                    get<0>(materialTextureDescriptorSets[materialIndex]) = ImGui_ImplVulkan_AddTexture(
                        textures.descriptorInfos[textureInfo->textureIndex].sampler,
                        *imageViews.emplace_back(gpu.device, image.getViewCreateInfo()
                            .setFormat(colorSpaceCompatibleFormat)
                            .setComponents({ vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eOne })),
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

                    // Roughness.
                    get<1>(materialTextureDescriptorSets[materialIndex]) = ImGui_ImplVulkan_AddTexture(
                        textures.descriptorInfos[textureInfo->textureIndex].sampler,
                        *imageViews.emplace_back(gpu.device, image.getViewCreateInfo()
                            .setFormat(colorSpaceCompatibleFormat)
                            .setComponents({ vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eOne })),
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }

                if (const auto &textureInfo = material.normalTexture) {
                    const vku::Image &image = getImage(textureInfo->textureIndex);

                    // Normal texture is non-sRGB by default, therefore image view format must be mutated if color space is sRGB.
                    vk::Format colorSpaceCompatibleFormat = image.format;
                    if (srgbColorAttachment) {
                        colorSpaceCompatibleFormat = convertSrgb(image.format);
                    }

                    get<2>(materialTextureDescriptorSets[materialIndex]) = ImGui_ImplVulkan_AddTexture(
                        textures.descriptorInfos[textureInfo->textureIndex].sampler,
                        *imageViews.emplace_back(gpu.device, image.getViewCreateInfo()
                            .setFormat(colorSpaceCompatibleFormat)
                            .setComponents({ {}, {}, {}, vk::ComponentSwizzle::eOne })),
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }

                if (const auto &textureInfo = material.occlusionTexture) {
                    const vku::Image &image = getImage(textureInfo->textureIndex);

                    // Occlusion texture is non-sRGB by default, therefore image view format must be mutated if color space is sRGB.
                    vk::Format colorSpaceCompatibleFormat = image.format;
                    if (srgbColorAttachment) {
                        colorSpaceCompatibleFormat = convertSrgb(image.format);
                    }

                    get<3>(materialTextureDescriptorSets[materialIndex]) = ImGui_ImplVulkan_AddTexture(
                        textures.descriptorInfos[textureInfo->textureIndex].sampler,
                        *imageViews.emplace_back(gpu.device, image.getViewCreateInfo()
                            .setFormat(colorSpaceCompatibleFormat)
                            .setComponents({ vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eOne })),
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }

                if (const auto &textureInfo = material.emissiveTexture) {
                    const vku::Image &image = getImage(textureInfo->textureIndex);

                    // Emissive texture is sRGB by default, therefore image view format must be mutated if color space is not sRGB.
                    vk::Format colorSpaceCompatibleFormat = image.format;
                    if (!srgbColorAttachment) {
                        colorSpaceCompatibleFormat = convertSrgb(image.format);
                    }

                    const vk::ComponentMapping components = [&]() -> vk::ComponentMapping {
                        switch (componentCount(image.format)) {
                            case 1:
                            case 2:
                                // Grayscale: red channel have to be propagated to green/blue channels. Alpha channel is ignored.
                                return { vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eOne };
                            case 4:
                                // RGB or RGBA.
                                return {};
                            default:
                                std::unreachable();
                        }
                    }();

                    get<4>(materialTextureDescriptorSets[materialIndex]) = ImGui_ImplVulkan_AddTexture(
                        textures.descriptorInfos[textureInfo->textureIndex].sampler,
                        *imageViews.emplace_back(gpu.device, image.getViewCreateInfo().setFormat(colorSpaceCompatibleFormat).setComponents(components)),
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }
            }
        }

        ~ImGuiColorSpaceAndUsageCorrectedTextures() override {
            for (vk::DescriptorSet descriptorSet : materialTextureDescriptorSets | std::views::join) {
                if (descriptorSet) {
                    ImGui_ImplVulkan_RemoveTexture(descriptorSet);
                }
            }
            for (vk::DescriptorSet descriptorSet : textureDescriptorSets) {
                ImGui_ImplVulkan_RemoveTexture(descriptorSet);
            }
        }

        [[nodiscard]] ImTextureID getTextureID(std::size_t textureIndex) const override {
            return vku::toUint64(textureDescriptorSets[textureIndex]);
        }

        [[nodiscard]] ImTextureID getMetallicTextureID(std::size_t materialIndex) const override {
            return vku::toUint64(get<0>(materialTextureDescriptorSets[materialIndex]));
        }

        [[nodiscard]] ImTextureID getRoughnessTextureID(std::size_t materialIndex) const override {
            return vku::toUint64(get<1>(materialTextureDescriptorSets[materialIndex]));
        }

        [[nodiscard]] ImTextureID getNormalTextureID(std::size_t materialIndex) const override {
            return vku::toUint64(get<2>(materialTextureDescriptorSets[materialIndex]));
        }

        [[nodiscard]] ImTextureID getOcclusionTextureID(std::size_t materialIndex) const override {
            return vku::toUint64(get<3>(materialTextureDescriptorSets[materialIndex]));
        }

        [[nodiscard]] ImTextureID getEmissiveTextureID(std::size_t materialIndex) const override {
            return vku::toUint64(get<4>(materialTextureDescriptorSets[materialIndex]));
        }
    };
}