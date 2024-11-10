export module vk_gltf_viewer:helpers.TempStringBuffer;

import std;
export import :helpers.cstring_view;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

/**
 * Temporary buffer that could be written by <tt>std::format</tt> without heap allocation.
 * @tparam CharT Character type.
 * @tparam BufferSize Maximum buffer size. Formatted output exceeding this size will be truncated.
 * @warning Thread unsafe. Also written value should be directly consumed. (It's result is temporary!)
 */
template <typename CharT, std::size_t BufferSize = 512>
class TempStringBuffer {
public:
    /**
     * @brief Write formatted output to the temporary buffer.
     *
     * Since the return type is <tt>TempStringBuffer</tt> itself, you can chain the method calls (like builder pattern).
     *
     * @tparam Args
     * @param fmt Format string.
     * @param args Arguments to format.
     * @return Reference to itself.
     */
    template <typename... Args>
    TempStringBuffer &write(std::format_string<Args...> fmt, Args &&...args) {
        auto it = std::format_to_n(buffer.data(), BufferSize - 1 /* last must be '\0' */, fmt, FWD(args)...).out;
        *it = '\0';
        size = it - buffer.data();
        return *this;
    }

    /**
     * @brief Convenience method to write a single argument.
     *
     * Since the return type is <tt>TempStringBuffer</tt> itself, you can chain the method calls (like builder pattern).
     *
     * @tparam Arg
     * @param arg Argument to be formatted.
     * @return Reference to itself.
     */
    template <typename Arg>
    TempStringBuffer &write(Arg &&arg) {
        return write("{}", FWD(arg));
    }

    [[nodiscard]] operator std::basic_string_view<CharT>() const noexcept {
        return { buffer.data(), size };
    }

    [[nodiscard]] operator basic_cstring_view<CharT>() const noexcept {
        return basic_cstring_view<CharT>::unsafeFrom(buffer.data(), size);
    }

    /**
     * @brief Get sized view for null-terminated string.
     * @return Sized view. The data past the end iterator will have '\0' character.
     */
    [[nodiscard]] basic_cstring_view<CharT> view() const noexcept {
        return basic_cstring_view<CharT>::unsafeFrom(buffer.data(), size);
    }

private:
    std::array<CharT, BufferSize> buffer;
    std::size_t size;
};

export TempStringBuffer<char> tempStringBuffer;