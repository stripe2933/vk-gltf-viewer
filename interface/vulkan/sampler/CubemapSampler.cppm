export module vk_gltf_viewer:vulkan.sampler.CubemapSampler;

#ifdef _MSC_VER
import std;
#endif
export import vulkan_hpp;

namespace vk_gltf_viewer::vulkan::inline sampler {
    export struct CubemapSampler : vk::raii::Sampler {
        explicit CubemapSampler(
            const vk::raii::Device &device [[clang::lifetimebound]]
        ) : Sampler { device, vk::SamplerCreateInfo {
                {},
                vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
                {}, {}, {},
                {},
                false, {},
                {}, {},
                {}, vk::LodClampNone,
            } } {}
    };
}