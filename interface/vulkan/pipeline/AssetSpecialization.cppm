export module vk_gltf_viewer.vulkan.pipeline.AssetSpecialization;

export import fastgltf;
import vk_gltf_viewer.helpers.ranges;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    /**
     * @brief Pipeline specialization applied for asset-wide.
     */
    export struct AssetSpecialization {
        bool useTextureTransform;

        explicit AssetSpecialization(const fastgltf::Asset &asset)
            : useTextureTransform { ranges::contains(asset.extensionsUsed, "KHR_texture_transform") } { }
    };
}