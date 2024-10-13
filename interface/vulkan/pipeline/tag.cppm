export module vk_gltf_viewer:vulkan.pipeline.tag;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    struct use_tessellation_t { explicit use_tessellation_t() = default; };
    export constexpr use_tessellation_t use_tessellation;
}