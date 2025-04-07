export module vk_gltf_viewer:vulkan.trait.PostTransferObject;

import std;
export import :vulkan.buffer.StagingBufferStorage;

namespace vk_gltf_viewer::vulkan::trait {
    export struct PostTransferObject {
        std::reference_wrapper<buffer::StagingBufferStorage> stagingBufferStorage;
    };
}