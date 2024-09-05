export module vk_gltf_viewer:vulkan.shader.FacetedPrimitiveVertex;

export import vku;

namespace vk_gltf_viewer::vulkan::shader {
    export struct FacetedPrimitiveVertex : vku::Shader {
        FacetedPrimitiveVertex() : Shader { COMPILED_SHADER_DIR "/faceted_primitive.vert.spv", vk::ShaderStageFlagBits::eVertex } { }
    };
}