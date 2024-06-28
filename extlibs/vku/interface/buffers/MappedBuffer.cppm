module;

#include <cassert>
#include <concepts>
#include <ranges>
#include <type_traits>

export module vku:buffers.MappedBuffer;

export import vk_mem_alloc_hpp;
export import vulkan_hpp;
export import :buffers.AllocatedBuffer;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

namespace vku {
    export struct MappedBuffer : AllocatedBuffer {
        void *data;

        MappedBuffer(
            vma::Allocator allocator,
            const vk::BufferCreateInfo &createInfo,
            const vma::AllocationCreateInfo &allocationCreateInfo = {
                vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped,
                vma::MemoryUsage::eAuto,
            }
        );
        template <typename T> requires
            (!std::same_as<T, std::from_range_t>)
            && std::is_standard_layout_v<T>
        MappedBuffer(
            vma::Allocator allocator,
            const T &value,
            vk::BufferUsageFlags usage,
            const vma::AllocationCreateInfo &allocationCreateInfo = {
                vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped,
                vma::MemoryUsage::eAuto,
            }
        ) : MappedBuffer { allocator, vk::BufferCreateInfo {
                {},
                sizeof(T),
                usage,
            }, allocationCreateInfo } {
            *static_cast<T*>(data) = value;
        }
        template <std::ranges::input_range R> requires
            (std::ranges::sized_range<R> && std::is_standard_layout_v<std::ranges::range_value_t<R>>)
        MappedBuffer(
            vma::Allocator allocator,
            std::from_range_t,
            R &&r,
            vk::BufferUsageFlags usage,
            const vma::AllocationCreateInfo &allocationCreateInfo = {
                vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped,
                vma::MemoryUsage::eAuto,
            }
        ) : MappedBuffer { allocator, vk::BufferCreateInfo {
                {},
                r.size() * sizeof(std::ranges::range_value_t<R>),
                usage,
            }, allocationCreateInfo } {
            std::ranges::copy(FWD(r), static_cast<std::ranges::range_value_t<R>*>(data));
        }
        MappedBuffer(const MappedBuffer&) = delete;
        MappedBuffer(MappedBuffer &&src) noexcept = default;
        auto operator=(const MappedBuffer&) -> MappedBuffer& = delete;
        auto operator=(MappedBuffer &&src) noexcept -> MappedBuffer&;
        ~MappedBuffer();

        template <typename T>
        [[nodiscard]] auto asRange(vk::DeviceSize byteOffset = 0) const -> std::span<const T> {
            assert(byteOffset <= size && "Out of bound: byteOffset > size");
            return { reinterpret_cast<const T*>(static_cast<const char*>(data) + byteOffset), (size - byteOffset) / sizeof(T) };
        }

        template <typename T>
        [[nodiscard]] auto asRange(vk::DeviceSize byteOffset = 0) -> std::span<T> {
            assert(byteOffset <= size && "Out of bound: byteOffset > size");
            return { reinterpret_cast<T*>(static_cast<char*>(data) + byteOffset), (size - byteOffset) / sizeof(T) };
        }

        template <typename T>
        [[nodiscard]] auto asValue(vk::DeviceSize byteOffset = 0) const -> const T& {
            assert(byteOffset + sizeof(T) <= size && "Out of bound: byteOffset + sizeof(T) > size");
            return *reinterpret_cast<const T*>(static_cast<const char*>(data) + byteOffset);
        }

        template <typename T>
        [[nodiscard]] auto asValue(vk::DeviceSize byteOffset = 0) -> T& {
            assert(byteOffset + sizeof(T) <= size && "Out of bound: byteOffset + sizeof(T) > size");
            return *reinterpret_cast<T*>(static_cast<char*>(data) + byteOffset);
        }
    };
}

// module:private;

vku::MappedBuffer::MappedBuffer(
    vma::Allocator allocator,
    const vk::BufferCreateInfo &createInfo,
    const vma::AllocationCreateInfo &allocationCreateInfo
) : AllocatedBuffer { allocator, createInfo, allocationCreateInfo },
    data { allocator.mapMemory(allocation) } { }

auto vku::MappedBuffer::operator=(
    MappedBuffer &&src
) noexcept -> MappedBuffer & {
    if (allocation) {
        allocator.unmapMemory(allocation);
    }
    static_cast<AllocatedBuffer &>(*this) = std::move(static_cast<AllocatedBuffer &>(src));
    data = src.data;
    return *this;
}

vku::MappedBuffer::~MappedBuffer() {
    if (allocation) {
        allocator.unmapMemory(allocation);
    }
}