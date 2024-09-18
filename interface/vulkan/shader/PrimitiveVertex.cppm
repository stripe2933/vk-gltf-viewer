export module vk_gltf_viewer:vulkan.shader.PrimitiveVertex;

#ifdef _MSC_VER
import std;
#endif
export import vku;

namespace vk_gltf_viewer::vulkan::shader {
    export struct PrimitiveVertex : vku::Shader {
        PrimitiveVertex() : Shader { COMPILED_SHADER_DIR "/primitive.vert.spv", vk::ShaderStageFlagBits::eVertex } { }
    };
}