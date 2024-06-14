module;

#include <chrono>
#include <format>
#include <print>
#include <source_location>
#include <thread>
#include <type_traits>

export module vk_gltf_viewer:io.logger;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

namespace vk_gltf_viewer::io::logger {
    template <typename CharT, typename... Args>
    struct tagged_basic_format_string : std::basic_format_string<CharT, Args...> {
        std::source_location source_location;

        template <typename T>
        consteval tagged_basic_format_string(
            const T &str,
            std::source_location source_location = std::source_location::current()
        ) : std::basic_format_string<CharT, Args...> { str },
            source_location { source_location } { }
    };

    template <typename... Args>
    using tagged_format_string = tagged_basic_format_string<char, std::type_identity_t<Args>...>;

    export template <bool ShowThreadId = false, typename... Args>
    auto debug(tagged_format_string<Args...> fmt, Args &&...args) -> void {
#ifndef NDEBUG
        if constexpr (ShowThreadId) {
            std::println("[{}] [debug] {} ({}:{}:{}, thread_id={})",
                std::chrono::system_clock::now(),
                std::format(fmt, FWD(args)...),
                fmt.source_location.file_name(), fmt.source_location.line(), fmt.source_location.column(),
                std::this_thread::get_id());
        }
        else {
            std::println("[{}] [debug] {} ({}:{}:{})",
                std::chrono::system_clock::now(),
                std::format(fmt, FWD(args)...),
                fmt.source_location.file_name(), fmt.source_location.line(), fmt.source_location.column());
        }
#endif
    }

    export template <typename... Args>
    auto info(std::format_string<Args...> fmt, Args &&...args) -> void {
        std::println("[{}] [info] {}", std::chrono::system_clock::now(), std::format(fmt, FWD(args)...));
    }

    export template <typename... Args>
    auto warning(std::format_string<Args...> fmt, Args &&...args) -> void {
        std::println("[{}] \033[33m[warning]\033[m {}", std::chrono::system_clock::now(), std::format(fmt, FWD(args)...));
    }

    export template <typename... Args>
    auto error(std::format_string<Args...> fmt, Args &&...args) -> void {
        std::println("[{}] \033[31m[error]\033[m {}", std::chrono::system_clock::now(), std::format(fmt, FWD(args)...));
    }
}