export module vk_gltf_viewer:vulkan.shader.AlphaMaskedPrimitiveFragment;

#ifdef _MSC_VER
import std;
#endif
export import vku;

namespace vk_gltf_viewer::vulkan::shader {
    export struct AlphaMaskedPrimitiveFragment : vku::Shader {
        AlphaMaskedPrimitiveFragment() : Shader { COMPILED_SHADER_DIR "/alpha_masked_primitive.frag.spv", vk::ShaderStageFlagBits::eFragment } { }
    };
}