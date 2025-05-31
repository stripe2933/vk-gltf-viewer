export module vk_gltf_viewer.vulkan.buffer.CubeIndices;

import std;
export import vku;

namespace vk_gltf_viewer::vulkan::buffer {
    /**
     * A standard index buffer for a counter-clockwise winding ordered cube. The reference cube vertices are:
     * (-1.0, -1.0, -1.0), (-1.0, -1.0,  1.0), (-1.0,  1.0, -1.0), (-1.0,  1.0,  1.0)
     * ( 1.0, -1.0, -1.0), ( 1.0, -1.0,  1.0), ( 1.0,  1.0, -1.0), ( 1.0,  1.0,  1.0)
     */
    export struct CubeIndices : vku::AllocatedBuffer {
        explicit CubeIndices(
            vma::Allocator allocator
        ) : AllocatedBuffer { vku::MappedBuffer { allocator, std::from_range, std::array<std::uint16_t, 36> {
                2, 6, 7, 2, 3, 7, 0, 4, 5, 0, 1, 5, 0, 2, 6, 0, 4, 6,
                1, 3, 7, 1, 5, 7, 0, 2, 3, 0, 1, 3, 4, 6, 7, 4, 5, 7,
            }, vk::BufferUsageFlagBits::eIndexBuffer }.unmap() } { }
    };
}