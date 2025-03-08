module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.sampler.BrdfLut;

#ifdef _MSC_VER
import std;
#endif
export import vulkan_hpp;

namespace vk_gltf_viewer::vulkan::sampler {
    export struct BrdfLut : vk::raii::Sampler {
        explicit BrdfLut(
            const vk::raii::Device &device LIFETIMEBOUND
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