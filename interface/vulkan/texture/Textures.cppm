export module vk_gltf_viewer:vulkan.texture.Textures;

import std;
import :helpers.fastgltf;
export import :vulkan.image.Images;
export import :vulkan.sampler.Samplers;
export import :vulkan.texture.Fallback;

namespace vk_gltf_viewer::vulkan::texture {
    export struct Textures {
        sampler::Samplers samplers;
        image::Images images;

        std::vector<vk::DescriptorImageInfo> descriptorInfos;

        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        Textures(
            const fastgltf::Asset &asset,
            const std::filesystem::path &directory,
            const Gpu &gpu,
            const Fallback &fallbackTexture,
            BS::thread_pool<> &threadPool,
            const BufferDataAdapter &adapter = {}
        ) : samplers { asset, gpu.device },
            images { asset, directory, gpu, threadPool, adapter },
            descriptorInfos { std::from_range, asset.textures | std::views::transform([&](const fastgltf::Texture &texture) {
                return vk::DescriptorImageInfo {
                    to_optional(texture.samplerIndex)
                        .transform([&](std::size_t samplerIndex) { return *samplers[samplerIndex]; })
                        .value_or(*fallbackTexture.sampler),
                    *get<1>(images.at(getPreferredImageIndex(texture))),
                    gpu.workaround.generalOr(vk::ImageLayout::eShaderReadOnlyOptimal),
                };
            }) } { }
    };
}
