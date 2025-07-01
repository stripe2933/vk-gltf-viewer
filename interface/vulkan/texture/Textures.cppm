export module vk_gltf_viewer.vulkan.texture.Textures;

import std;

import vk_gltf_viewer.helpers.fastgltf;
export import vk_gltf_viewer.vulkan.image.Images;
export import vk_gltf_viewer.vulkan.sampler.Samplers;
export import vk_gltf_viewer.vulkan.texture.Fallback;

namespace vk_gltf_viewer::vulkan::texture {
    export struct Textures {
        sampler::Samplers samplers;
        image::Images images;

        std::vector<vk::DescriptorImageInfo> descriptorInfos;

        Textures(
            const fastgltf::Asset &asset,
            const std::filesystem::path &directory,
            const Gpu &gpu,
            const Fallback &fallbackTexture,
            BS::thread_pool<> &threadPool,
            const gltf::AssetExternalBuffers &adapter
        );
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::texture::Textures::Textures(
    const fastgltf::Asset &asset,
    const std::filesystem::path &directory,
    const Gpu &gpu,
    const Fallback &fallbackTexture,
    BS::thread_pool<> &threadPool,
    const gltf::AssetExternalBuffers &adapter
) : samplers { asset, gpu.device },
    images { asset, directory, gpu, threadPool, adapter } {
    descriptorInfos.reserve(asset.textures.size());
    for (const fastgltf::Texture &texture : asset.textures) {
        descriptorInfos.emplace_back(
            to_optional(texture.samplerIndex)
                .transform([&](std::size_t samplerIndex) { return *samplers[samplerIndex]; })
                .value_or(*fallbackTexture.sampler),
            *images.at(getPreferredImageIndex(texture)).view,
            vk::ImageLayout::eShaderReadOnlyOptimal);
    }
}