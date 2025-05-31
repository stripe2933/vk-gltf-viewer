module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.sampler.Cubemap;

#ifdef _MSC_VER
import std;
#endif
export import vulkan_hpp;

namespace vk_gltf_viewer::vulkan::sampler {
    export struct Cubemap : vk::raii::Sampler {
        explicit Cubemap(
            const vk::raii::Device &device LIFETIMEBOUND
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