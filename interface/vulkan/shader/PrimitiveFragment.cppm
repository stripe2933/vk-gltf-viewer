export module vk_gltf_viewer:vulkan.shader.PrimitiveFragment;

export import vku;

namespace vk_gltf_viewer::vulkan::shader {
    export struct PrimitiveFragment : vku::Shader {
        PrimitiveFragment() : Shader { COMPILED_SHADER_DIR "/primitive.frag.spv", vk::ShaderStageFlagBits::eFragment } { }
    };
}