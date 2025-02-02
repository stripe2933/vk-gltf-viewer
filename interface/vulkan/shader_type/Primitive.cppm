export module vk_gltf_viewer:vulkan.shader_type.Primitive;

import std;
export import vulkan_hpp;

namespace vk_gltf_viewer::vulkan::shader_type {
    export struct Primitive {
        vk::DeviceAddress pPositionBuffer;
        vk::DeviceAddress pNormalBuffer;
        vk::DeviceAddress pTangentBuffer;
        vk::DeviceAddress pTexcoordAttributeMappingInfoBuffer;
        vk::DeviceAddress pColorBuffer;
        std::uint8_t positionByteStride;
        std::uint8_t normalByteStride;
        std::uint8_t tangentByteStride;
        std::uint8_t colorByteStride;
        std::uint32_t materialIndex;
    };
}