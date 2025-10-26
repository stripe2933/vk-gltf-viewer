export module vk_gltf_viewer.helpers.TempStringBuffer;

import std;
export import cstring_view;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

/**
 * Temporary buffer that could be written by <tt>std::format</tt> without heap allocation.
 * @tparam CharT Character type.
 * @tparam BufferSize Maximum buffer size. Formatted output exceeding this size will be truncated.
 * @warning Thread unsafe. Also written value should be directly consumed. (It's result is temporary!)
 */
template <typename CharT, std::size_t BufferSize = 512>
    requires (BufferSize >= 1) // Buffer size must be at least 1 for storing '\0'.
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
     * @brief Append formatted output to the temporary buffer.
     *
     * Like <tt>write</tt>, but appends to the end of the buffer (the previous content is preserved).
     * If you want to clear the buffer before writing, use <tt>clear</tt> method.
     * Since the return type is <tt>TempStringBuffer</tt> itself, you can chain the method calls (like builder pattern).
     *
     * @tparam Args
     * @param fmt Format string.
     * @param args Arguments to format.
     * @return Reference to itself.
     */
    template <typename... Args>
    TempStringBuffer &append(std::format_string<Args...> fmt, Args &&...args) {
        if (size + 1 < BufferSize) {
            auto it = std::format_to_n(buffer.data() + size, BufferSize - size - 1 /* last must be '\0' */, fmt, FWD(args)...).out;
            *it = '\0';
            size = it - buffer.data();
        }
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

    /**
     * @brief Convenience method to append a single argument.
     *
     * Like <tt>write</tt>, but appends to the end of the buffer (the previous content is preserved).
     * If you want to clear the buffer before writing, use <tt>clear</tt> method.
     * Since the return type is <tt>TempStringBuffer</tt> itself, you can chain the method calls (like builder pattern).
     *
     * @tparam Arg
     * @param arg Argument to be formatted.
     * @return Reference to itself.
     */
    template <typename Arg>
    TempStringBuffer &append(Arg &&arg) {
        return append("{}", FWD(arg));
    }

    /**
     * @brief Clear the buffer content.
     *
     * This method is for <tt>append()</tt> usage, to clear the buffer before writing.
     * If you're using only <tt>write()</tt>, you don't need to call this method (each call will overwrite the previous content).
     * Since the return type is <tt>TempStringBuffer</tt> itself, you can chain the method calls (like builder pattern).
     *
     * @return Reference to itself.
     */
    TempStringBuffer &clear() noexcept {
        size = 0;
        return *this;
    }

    [[nodiscard]] bool empty() const noexcept {
        return size == 0;
    }

    [[nodiscard]] operator std::basic_string_view<CharT>() const noexcept {
        return { buffer.data(), size };
    }

    [[nodiscard]] operator cpp_util::basic_cstring_view<CharT>() const noexcept {
        return { cpp_util::basic_cstring_view<CharT>::null_terminated, buffer.data(), size };
    }

    /**
     * @brief Get sized view for null-terminated string.
     * @return Sized view. The data past the end iterator will have '\0' character.
     */
    [[nodiscard]] cpp_util::basic_cstring_view<CharT> view() const noexcept {
        return { cpp_util::basic_cstring_view<CharT>::null_terminated, buffer.data(), size };
    }

    /**
     * @brief Get mutable span to the internal buffer.
     * @warning Modifying the content may lead to inconsistent state, especially for '\0' character. Use with caution.
     */
    [[nodiscard]] std::span<CharT> mut_view() noexcept {
        return { buffer.data(), size };
    }

private:
    std::array<CharT, BufferSize> buffer;
    std::size_t size;
};

export TempStringBuffer<char> tempStringBuffer;