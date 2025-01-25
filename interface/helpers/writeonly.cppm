export module vk_gltf_viewer:helpers.writeonly;

import std;

/**
 * @brief Wrapper that allows only write operation (the internal value is not readable).
 *
 * This class is useful if reading operation on a value is expensive or should not be done for some reason.
 *
 * @tparam T Type of the internal value. Its copy/move assignment operation should not read the destination value.
 */
export template <typename T>
class writeonly {
    T volatile *addr;

public:
    explicit writeonly(std::uintptr_t addr)
        : addr { reinterpret_cast<T*>(addr) } { }

    void operator=(const T &t) volatile { *addr = t; }

    /**
     * @brief Address of the value.
     * @return Address of the value.
     */
    [[nodiscard]] std::uintptr_t address() const noexcept { return addr; }
};