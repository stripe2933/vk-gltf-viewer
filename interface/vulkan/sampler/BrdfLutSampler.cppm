export module vk_gltf_viewer:vulkan.sampler.BrdfLutSampler;

export import vulkan_hpp;

namespace vk_gltf_viewer::vulkan::inline sampler {
    export struct BrdfLutSampler : vk::raii::Sampler {
        explicit BrdfLutSampler(
            const vk::raii::Device &device [[clang::lifetimebound]]
        ) : Sampler { device, vk::SamplerCreateInfo {
                {},
                vk::Filter::eLinear, vk::Filter::eLinear, {},
                vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge,
                {},
                false, {},
                {}, {},
                {}, vk::LodClampNone,
            } } {}
    };
}