module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.sampler.Samplers;

import std;
export import fastgltf;
import vku;
export import vulkan_hpp;

[[nodiscard]] constexpr vk::SamplerAddressMode convertSamplerAddressMode(fastgltf::Wrap wrap) noexcept {
    switch (wrap) {
        case fastgltf::Wrap::ClampToEdge:
            return vk::SamplerAddressMode::eClampToEdge;
        case fastgltf::Wrap::MirroredRepeat:
            return vk::SamplerAddressMode::eMirroredRepeat;
        case fastgltf::Wrap::Repeat:
            return vk::SamplerAddressMode::eRepeat;
    }
    std::unreachable();
}

namespace vk_gltf_viewer::vulkan::sampler {
    export struct Samplers : std::vector<vk::raii::Sampler> {
        Samplers(const fastgltf::Asset &asset, const vk::raii::Device &device LIFETIMEBOUND) {
            reserve(asset.samplers.size());
            for (const fastgltf::Sampler &sampler : asset.samplers) {
                vk::SamplerCreateInfo createInfo {
                    {},
                    {}, {}, {},
                    convertSamplerAddressMode(sampler.wrapS), convertSamplerAddressMode(sampler.wrapT), {},
                    {},
                    true, 16.f,
                    {}, {},
                    {}, vk::LodClampNone,
                };

                // TODO: how can map OpenGL filter to Vulkan corresponds?
                // https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkSamplerCreateInfo.html
                const auto applyFilter = [&](bool mag, fastgltf::Filter filter) -> void {
                    switch (filter) {
                    case fastgltf::Filter::Nearest:
                        (mag ? createInfo.magFilter : createInfo.minFilter) = vk::Filter::eNearest;
                        break;
                    case fastgltf::Filter::Linear:
                        (mag ? createInfo.magFilter : createInfo.minFilter) = vk::Filter::eLinear;
                        break;
                    case fastgltf::Filter::NearestMipMapNearest:
                        (mag ? createInfo.magFilter : createInfo.minFilter) = vk::Filter::eNearest;
                        createInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
                        break;
                    case fastgltf::Filter::LinearMipMapNearest:
                        (mag ? createInfo.magFilter : createInfo.minFilter) = vk::Filter::eLinear;
                        createInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
                        break;
                    case fastgltf::Filter::NearestMipMapLinear:
                        (mag ? createInfo.magFilter : createInfo.minFilter) = vk::Filter::eNearest;
                        createInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
                        break;
                    case fastgltf::Filter::LinearMipMapLinear:
                        (mag ? createInfo.magFilter : createInfo.minFilter) = vk::Filter::eLinear;
                        createInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
                        break;
                    }
                };
                if (sampler.magFilter) applyFilter(true, *sampler.magFilter);
                if (sampler.minFilter) applyFilter(false, *sampler.minFilter);

                // For best performance, all address mode should be the same.
                // https://developer.arm.com/documentation/101897/0302/Buffers-and-textures/Texture-and-sampler-descriptors
                if (createInfo.addressModeU == createInfo.addressModeV) {
                    createInfo.addressModeW = createInfo.addressModeU;
                }

                emplace_back(device, createInfo);
            }
        }
    };
}