module;

#include <stb_image.h>

export module vk_gltf_viewer:io.StbDecoder;

import std;

namespace vk_gltf_viewer::io {
    export template <typename T>
        requires (std::same_as<T, std::uint8_t> || std::same_as<T, std::uint16_t> || std::same_as<T, float>)
    class StbDecoder {
    public:
        struct DecodeResult {
            std::uint32_t width, height, channels;
            std::unique_ptr<T[]> data;

            [[nodiscard]] auto asSpan() const noexcept -> std::span<const T> {
                return { data.get(), static_cast<std::size_t>(width * height * channels) };
            }

            [[nodiscard]] auto asSpan() noexcept -> std::span<T> {
                return { data.get(), static_cast<std::size_t>(width * height * channels) };
            }

            [[nodiscard]] auto asMdspan() const noexcept -> std::mdspan<const T, std::dextents<std::uint32_t, 3>> {
                return { data.get(), width, height, channels };
            }

            [[nodiscard]] auto asMdspan() noexcept -> std::mdspan<T, std::dextents<std::uint32_t, 3>> {
                return { data.get(), width, height, channels };
            }
        };

        [[nodiscard]] static auto fromFile(const char *path, int desiredChannels = 0) -> DecodeResult {
            constexpr auto loadFunc = []() consteval {
                if constexpr (std::same_as<T, stbi_uc>) return &stbi_load;
                if constexpr (std::same_as<T, stbi_us>) return &stbi_load_16;
                if constexpr (std::same_as<T, float>)   return &stbi_loadf;
            }();

            int width, height, channels;
            DecodeResult result;
            result.data.reset(loadFunc(path, &width, &height, &channels, desiredChannels));
            checkError(result.data.get());

            result.width = width;
            result.height = height;
            result.channels = desiredChannels ? desiredChannels : channels;
            return result;
        }

        template <typename U>
        [[nodiscard]] static auto fromMemory(std::span<const U> memory, int desiredChannels = 0) -> DecodeResult {
            constexpr auto loadFunc = []() consteval {
                if constexpr (std::same_as<T, stbi_uc>) return &stbi_load_from_memory;
                if constexpr (std::same_as<T, stbi_us>) return &stbi_load_16_from_memory;
                if constexpr (std::same_as<T, float>)   return &stbi_loadf_from_memory;
            }();

            int width, height, channels;
            DecodeResult result;
            result.data.reset(loadFunc(
                reinterpret_cast<const stbi_uc*>(memory.data()), memory.size_bytes(),
                &width, &height, &channels, desiredChannels));
            checkError(result.data.get());

            result.width = width;
            result.height = height;
            result.channels = desiredChannels ? desiredChannels : channels;
            return result;
        }

    private:
        static auto checkError(void *data) -> void {
            if (!data) {
                throw std::runtime_error { std::format("Failed to load image: {}", stbi_failure_reason()) };
            }
        }
    };
}