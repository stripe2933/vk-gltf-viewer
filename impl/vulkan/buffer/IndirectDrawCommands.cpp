module vk_gltf_viewer;
import :vulkan.buffer.IndirectDrawCommands;

namespace vk_gltf_viewer::vulkan::buffer {
    // --------------------
    // Template instantiations.
    // --------------------

    extern template struct IndirectDrawCommands<false>;
    extern template struct IndirectDrawCommands<true>;
}
