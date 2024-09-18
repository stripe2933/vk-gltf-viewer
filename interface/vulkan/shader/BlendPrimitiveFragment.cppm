export module vk_gltf_viewer:vulkan.shader.BlendPrimitiveFragment;

#ifdef _MSC_VER
import std;
#endif
export import vku;

namespace vk_gltf_viewer::vulkan::shader {
    export struct BlendPrimitiveFragment : vku::Shader {
        BlendPrimitiveFragment() : Shader { COMPILED_SHADER_DIR "/blend_primitive.frag.spv", vk::ShaderStageFlagBits::eFragment } { }
    };
}