export module vkgltf.sampler;

import std;
export import fastgltf;
export import vulkan_hpp;

namespace vkgltf {
    /**
     * @brief Get <tt>vk::SamplerCreateInfo</tt> for \p sampler.
     * @param sampler glTF sampler to convert to <tt>vk::SamplerCreateInfo</tt>.
     * @param maxAnisotropy Maximum anisotropy to use for the sampler. If set to 0, anisotropic sampling will be disabled.
     * @return <tt>vk::SamplerCreateInfo</tt> for the given glTF sampler.
     */
    export
    [[nodiscard]] vk::SamplerCreateInfo getSamplerCreateInfo(const fastgltf::Sampler &sampler, float maxAnisotropy = 0.f);
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

[[nodiscard]] vk::SamplerAddressMode convertSamplerAddressMode(fastgltf::Wrap wrap) noexcept {
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

// TODO: how can map OpenGL filter to Vulkan corresponds?
// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkSamplerCreateInfo.html
void applyFilter(vk::SamplerCreateInfo &createInfo, bool mag, fastgltf::Filter filter) {
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
}

vk::SamplerCreateInfo vkgltf::getSamplerCreateInfo(
    const fastgltf::Sampler &sampler,
    float maxAnisotropy
) {
    vk::SamplerCreateInfo result {
        {},
        {}, {}, {},
        convertSamplerAddressMode(sampler.wrapS), convertSamplerAddressMode(sampler.wrapT), {},
        {},
        maxAnisotropy > 0.f, maxAnisotropy,
        {}, {},
        {}, vk::LodClampNone,
    };

    if (sampler.magFilter) applyFilter(result, true, *sampler.magFilter);
    if (sampler.minFilter) applyFilter(result, false, *sampler.minFilter);

    // For best performance, all address mode should be the same.
    // https://developer.arm.com/documentation/101897/0302/Buffers-and-textures/Texture-and-sampler-descriptors
    if (result.addressModeU == result.addressModeV) {
        result.addressModeW = result.addressModeU;
    }

    return result;
}