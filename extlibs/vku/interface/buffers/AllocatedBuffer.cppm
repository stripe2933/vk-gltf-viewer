module;

#include <string_view>
#include <tuple>

export module vku:buffers.AllocatedBuffer;

export import vk_mem_alloc_hpp;
export import vulkan_hpp;
export import :buffers.Buffer;

namespace vku {
    export struct AllocatedBuffer : Buffer {
        vma::Allocator allocator;
        vma::Allocation allocation;

        AllocatedBuffer(
            vma::Allocator allocator,
            const vk::BufferCreateInfo &createInfo,
            const vma::AllocationCreateInfo &allocationCreateInfo
        );
        AllocatedBuffer(const AllocatedBuffer&) = delete;
        AllocatedBuffer(AllocatedBuffer &&src) noexcept;
        auto operator=(const AllocatedBuffer&) -> AllocatedBuffer& = delete;
        auto operator=(AllocatedBuffer &&src) noexcept -> AllocatedBuffer&;
        ~AllocatedBuffer();
    };
}

// module:private;

vku::AllocatedBuffer::AllocatedBuffer(
    vma::Allocator _allocator,
    const vk::BufferCreateInfo &createInfo,
    const vma::AllocationCreateInfo &allocationCreateInfo
) : Buffer { nullptr, createInfo.size },
    allocator { _allocator } {
    std::tie(buffer, allocation) = allocator.createBuffer(createInfo, allocationCreateInfo);
}

vku::AllocatedBuffer::AllocatedBuffer(
    AllocatedBuffer &&src
) noexcept : Buffer { static_cast<Buffer>(src) },
             allocator { src.allocator },
             allocation { std::exchange(src.allocation, nullptr) } { }

auto vku::AllocatedBuffer::operator=(
    AllocatedBuffer &&src
) noexcept -> AllocatedBuffer & {
    if (allocation) {
        allocator.destroyBuffer(buffer, allocation);
    }

    static_cast<Buffer &>(*this) = static_cast<Buffer>(src);
    allocator = src.allocator;
    buffer = std::exchange(src.buffer, nullptr);
    allocation = std::exchange(src.allocation, nullptr);
    return *this;
}

vku::AllocatedBuffer::~AllocatedBuffer() {
    if (allocation) {
        allocator.destroyBuffer(buffer, allocation);
    }
}