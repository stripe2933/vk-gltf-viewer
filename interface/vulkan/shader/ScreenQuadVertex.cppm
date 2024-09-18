export module vk_gltf_viewer:vulkan.shader.ScreenQuadVertex;

#ifdef _MSC_VER
import std;
#endif
export import vku;

namespace vk_gltf_viewer::vulkan::shader {
    export struct ScreenQuadVertex : vku::Shader {
        ScreenQuadVertex() : Shader { COMPILED_SHADER_DIR "/screen_quad.vert.spv", vk::ShaderStageFlagBits::eVertex } { }
    };
}