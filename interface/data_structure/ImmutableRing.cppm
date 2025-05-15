export module vk_gltf_viewer.data_structure.ImmutableRing;

import std;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

namespace vk_gltf_viewer::ds {
    export template <typename T, typename Allocator = std::vector<T>>
    class ImmutableRing {
        Allocator data;
        std::size_t cursor = 0;

    public:
        ImmutableRing(std::from_range_t, std::ranges::input_range auto &&r)
            : data { std::from_range, FWD(r) } {
            if (data.empty()) {
                throw std::invalid_argument { "ImmutableRing: empty range" };
            }
        }

        [[nodiscard]] T &current() noexcept {
            return data[cursor];
        }

        [[nodiscard]] const T &current() const noexcept {
            return data[cursor];
        }

        void advance() noexcept {
            cursor = (cursor + 1) % data.size();
        }
    };
}