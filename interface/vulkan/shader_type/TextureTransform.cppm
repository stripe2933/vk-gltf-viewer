export module vk_gltf_viewer:vulkan.shader_type.TextureTransform;

import std;

namespace vk_gltf_viewer::vulkan::shader_type {
    export enum class TextureTransform : std::uint8_t {
        /**
         * @brief No texture transform is performed.
         *
         * When the primitive's material textures don't have any KHR_texture_transform extensions, use this value to
         * avoid the unnecessary matrix-vector multiplication in the fragment shader.
         */
        None = 0,
        /**
         * @brief Texture transform has scale and offset component.
         *
         * This is more efficient than <tt>All</tt> because this transform can be done by multiply-and-add (MAD) operation
         * in fragment shader.
         */
        ScaleAndOffset = 1,
        /**
         * @brief Texture transform has rotation component.
         *
         * Avoid it if possible, because it takes more instructions than <tt>ScaleAndOffset</tt>.
         */
        All = 2,
    };
}