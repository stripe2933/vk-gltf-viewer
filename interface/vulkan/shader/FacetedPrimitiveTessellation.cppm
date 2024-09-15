export module vk_gltf_viewer:vulkan.shader.FacetedPrimitiveTessellation;

export import vku;

namespace vk_gltf_viewer::vulkan::shader {
    export struct FacetedPrimitiveTessellation {
        vku::Shader control { COMPILED_SHADER_DIR "/faceted_primitive.tesc.spv", vk::ShaderStageFlagBits::eTessellationControl };
        vku::Shader evaluation { COMPILED_SHADER_DIR "/faceted_primitive.tese.spv", vk::ShaderStageFlagBits::eTessellationEvaluation };
    };
}