module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.texture.ImGuiColorSpaceAndUsageCorrectedTextures;

import std;
export import imgui.vulkan;

import vk_gltf_viewer.helpers.fastgltf;
import vk_gltf_viewer.helpers.functional;
import vk_gltf_viewer.helpers.ranges;
import vk_gltf_viewer.helpers.vulkan;
export import vk_gltf_viewer.imgui.ColorSpaceAndUsageCorrectedTextures;
export import vk_gltf_viewer.vulkan.texture.Textures;

namespace vk_gltf_viewer::vulkan::texture {
    export class ImGuiColorSpaceAndUsageCorrectedTextures final : public imgui::ColorSpaceAndUsageCorrectedTextures {
        std::deque<vk::raii::ImageView> imageViews;
        std::vector<vk::DescriptorSet> textureDescriptorSets;
        std::vector<std::array<vk::DescriptorSet, 5>> materialTextureDescriptorSets; // [0] -> metallic, [1] -> roughness, [2] -> normal, [3] -> occlusion, [4] -> emissive

    public:
        ImGuiColorSpaceAndUsageCorrectedTextures(const fastgltf::Asset &asset LIFETIMEBOUND, const Textures &textures LIFETIMEBOUND, const Gpu &gpu LIFETIMEBOUND);
        ~ImGuiColorSpaceAndUsageCorrectedTextures() override;

        [[nodiscard]] ImTextureID getTextureID(std::size_t textureIndex) const override;
        [[nodiscard]] ImTextureID getMetallicTextureID(std::size_t materialIndex) const override;
        [[nodiscard]] ImTextureID getRoughnessTextureID(std::size_t materialIndex) const override;
        [[nodiscard]] ImTextureID getNormalTextureID(std::size_t materialIndex) const override;
        [[nodiscard]] ImTextureID getOcclusionTextureID(std::size_t materialIndex) const override;
        [[nodiscard]] ImTextureID getEmissiveTextureID(std::size_t materialIndex) const override;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::texture::ImGuiColorSpaceAndUsageCorrectedTextures::ImGuiColorSpaceAndUsageCorrectedTextures(
    const fastgltf::Asset &asset,
    const Textures &textures,
    const Gpu &gpu
) {
    textureDescriptorSets
        = ranges::views::enumerate(asset.textures)
        | std::views::transform(decomposer([&](std::size_t textureIndex, const fastgltf::Texture &texture) -> vk::DescriptorSet {
            auto [sampler, imageView, _] = textures.descriptorInfos[textureIndex];
            const vku::Image &image = textures.images.at(getPreferredImageIndex(texture)).image;
            if (gpu.supportSwapchainMutableFormat == isSrgbFormat(image.format)) {
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

    materialTextureDescriptorSets.resize(asset.materials.size());
    for (const auto &[materialIndex, material] : ranges::views::enumerate(asset.materials)) {
        if (const auto &textureInfo = material.pbrData.metallicRoughnessTexture) {
            const vku::Image &image = textures.images.at(getPreferredImageIndex(asset.textures[textureInfo->textureIndex])).image;
            if (componentCount(image.format) == 1) {
                // Texture is grayscale, channel propagating swizzling is unnecessary.
                get<0>(materialTextureDescriptorSets[materialIndex]) = textureDescriptorSets[textureInfo->textureIndex];
                get<1>(materialTextureDescriptorSets[materialIndex]) = textureDescriptorSets[textureInfo->textureIndex];
            }
            else {
                vk::Format colorSpaceCompatibleFormat = image.format;
                if (gpu.supportSwapchainMutableFormat == isSrgbFormat(image.format)) {
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
        }

        if (const auto &textureInfo = material.normalTexture) {
            get<2>(materialTextureDescriptorSets[materialIndex]) = [&]() -> vk::DescriptorSet {
                const auto &[image, _, alphaChannelPadded] = textures.images.at(getPreferredImageIndex(asset.textures[textureInfo->textureIndex]));
                if (componentCount(image.format) == 1 || alphaChannelPadded) {
                    // Alpha channel is sampled as 1, therefore can use the texture as is.
                    return textureDescriptorSets[textureInfo->textureIndex];
                }
                else {
                    vk::Format colorSpaceCompatibleFormat = image.format;
                    if (gpu.supportSwapchainMutableFormat == isSrgbFormat(image.format)) {
                        colorSpaceCompatibleFormat = convertSrgb(image.format);
                    }

                    return ImGui_ImplVulkan_AddTexture(
                        textures.descriptorInfos[textureInfo->textureIndex].sampler,
                        *imageViews.emplace_back(gpu.device, image.getViewCreateInfo()
                            .setFormat(colorSpaceCompatibleFormat)
                            .setComponents({ {}, {}, {}, vk::ComponentSwizzle::eOne })),
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }
            }();
        }

        if (const auto &textureInfo = material.occlusionTexture) {
            get<3>(materialTextureDescriptorSets[materialIndex]) = [&]() -> vk::DescriptorSet {
                const vku::Image &image = textures.images.at(getPreferredImageIndex(asset.textures[textureInfo->textureIndex])).image;
                if (componentCount(image.format) == 1) {
                    // Texture is grayscale, channel propagating swizzling is unnecessary.
                    return textureDescriptorSets[textureInfo->textureIndex];
                }
                else {
                    vk::Format colorSpaceCompatibleFormat = image.format;
                    if (gpu.supportSwapchainMutableFormat == isSrgbFormat(image.format)) {
                        colorSpaceCompatibleFormat = convertSrgb(image.format);
                    }

                    return ImGui_ImplVulkan_AddTexture(
                        textures.descriptorInfos[textureInfo->textureIndex].sampler,
                        *imageViews.emplace_back(gpu.device, image.getViewCreateInfo()
                            .setFormat(colorSpaceCompatibleFormat)
                            .setComponents({ vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eOne })),
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }
            }();
        }

        if (const auto &textureInfo = material.emissiveTexture) {
            get<4>(materialTextureDescriptorSets[materialIndex]) = [&]() -> vk::DescriptorSet {
                const auto &[image, _, alphaChannelPadded] = textures.images.at(getPreferredImageIndex(asset.textures[textureInfo->textureIndex]));
                if (componentCount(image.format) == 1 || alphaChannelPadded) {
                    // Alpha channel is sampled as 1, therefore can use the texture as is.
                    return textureDescriptorSets[textureInfo->textureIndex];
                }
                else {
                    // Emissive texture must be sRGB encoded, therefore image view format must be mutated if color space
                    // is not sRGB.
                    vk::Format colorSpaceCompatibleFormat = image.format;
                    if (gpu.supportSwapchainMutableFormat) {
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

                    return ImGui_ImplVulkan_AddTexture(
                        textures.descriptorInfos[textureInfo->textureIndex].sampler,
                        *imageViews.emplace_back(gpu.device, image.getViewCreateInfo().setFormat(colorSpaceCompatibleFormat).setComponents(components)),
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }
            }();
        }
    }
}

vk_gltf_viewer::vulkan::texture::ImGuiColorSpaceAndUsageCorrectedTextures::~ImGuiColorSpaceAndUsageCorrectedTextures() {
    std::vector<vk::DescriptorSet> uniqueDescriptorSets;
    for (vk::DescriptorSet descriptorSet : materialTextureDescriptorSets | std::views::join) {
        if (descriptorSet) {
            uniqueDescriptorSets.push_back(descriptorSet);
        }
    }
    uniqueDescriptorSets.append_range(textureDescriptorSets);

    // Remove duplicates.
    std::ranges::sort(uniqueDescriptorSets);
    const auto [begin, end] = std::ranges::unique(uniqueDescriptorSets);
    uniqueDescriptorSets.erase(begin, end);

    std::ranges::for_each(uniqueDescriptorSets, ImGui_ImplVulkan_RemoveTexture);
}

[[nodiscard]] ImTextureID vk_gltf_viewer::vulkan::texture::ImGuiColorSpaceAndUsageCorrectedTextures::getTextureID(std::size_t textureIndex) const {
    return vku::toUint64(textureDescriptorSets[textureIndex]);
}

[[nodiscard]] ImTextureID vk_gltf_viewer::vulkan::texture::ImGuiColorSpaceAndUsageCorrectedTextures::getMetallicTextureID(std::size_t materialIndex) const {
    return vku::toUint64(get<0>(materialTextureDescriptorSets[materialIndex]));
}

[[nodiscard]] ImTextureID vk_gltf_viewer::vulkan::texture::ImGuiColorSpaceAndUsageCorrectedTextures::getRoughnessTextureID(std::size_t materialIndex) const {
    return vku::toUint64(get<1>(materialTextureDescriptorSets[materialIndex]));
}

[[nodiscard]] ImTextureID vk_gltf_viewer::vulkan::texture::ImGuiColorSpaceAndUsageCorrectedTextures::getNormalTextureID(std::size_t materialIndex) const {
    return vku::toUint64(get<2>(materialTextureDescriptorSets[materialIndex]));
}

[[nodiscard]] ImTextureID vk_gltf_viewer::vulkan::texture::ImGuiColorSpaceAndUsageCorrectedTextures::getOcclusionTextureID(std::size_t materialIndex) const {
    return vku::toUint64(get<3>(materialTextureDescriptorSets[materialIndex]));
}

[[nodiscard]] ImTextureID vk_gltf_viewer::vulkan::texture::ImGuiColorSpaceAndUsageCorrectedTextures::getEmissiveTextureID(std::size_t materialIndex) const {
    return vku::toUint64(get<4>(materialTextureDescriptorSets[materialIndex]));
}