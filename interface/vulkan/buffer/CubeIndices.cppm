export module vk_gltf_viewer.vulkan.buffer.CubeIndices;

import std;
export import vku;

namespace vk_gltf_viewer::vulkan::buffer {
    /**
     * A standard index buffer for a counter-clockwise winding ordered cube. The reference cube vertices are:
     * (-1.0, -1.0, -1.0), (-1.0, -1.0,  1.0), (-1.0,  1.0, -1.0), (-1.0,  1.0,  1.0)
     * ( 1.0, -1.0, -1.0), ( 1.0, -1.0,  1.0), ( 1.0,  1.0, -1.0), ( 1.0,  1.0,  1.0)
     */
    export struct CubeIndices final : vku::raii::AllocatedBuffer {
        explicit CubeIndices(vma::Allocator allocator);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

constexpr std::uint16_t data[] = {
    2, 6, 7, 2, 3, 7, 0, 4, 5, 0, 1, 5, 0, 2, 6, 0, 4, 6,
    1, 3, 7, 1, 5, 7, 0, 2, 3, 0, 1, 3, 4, 6, 7, 4, 5, 7,
};

vk_gltf_viewer::vulkan::buffer::CubeIndices::CubeIndices(vma::Allocator allocator)
    : AllocatedBuffer {
        allocator,
        vk::BufferCreateInfo {
            {},
            sizeof(data),
            vk::BufferUsageFlagBits::eIndexBuffer,
        },
        vma::AllocationCreateInfo {
            vma::AllocationCreateFlagBits::eHostAccessSequentialWrite,
            vma::MemoryUsage::eAutoPreferDevice,
        },
    } {
    allocator.copyMemoryToAllocation(data, allocation, 0, sizeof(data));
}