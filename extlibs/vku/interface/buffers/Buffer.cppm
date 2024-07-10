export module vku:buffers.Buffer;

import std;
export import vulkan_hpp;

namespace vku {
    export struct Buffer {
        vk::Buffer buffer;
        vk::DeviceSize size;

        [[nodiscard]] operator vk::Buffer() const noexcept {
            return buffer;
        }
    };
}