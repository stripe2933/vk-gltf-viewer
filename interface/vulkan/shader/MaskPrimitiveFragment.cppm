export module vk_gltf_viewer:vulkan.shader.MaskPrimitiveFragment;

#ifdef _MSC_VER
import std;
#endif
export import vku;

namespace vk_gltf_viewer::vulkan::shader {
    export struct MaskPrimitiveFragment : vku::Shader {
        MaskPrimitiveFragment() : Shader { COMPILED_SHADER_DIR "/mask_primitive.frag.spv", vk::ShaderStageFlagBits::eFragment } { }
    };
}