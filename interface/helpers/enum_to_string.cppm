module;

#include <fastgltf/types.hpp>

export module vk_gltf_viewer:helpers.enum_to_string;

import std;

export namespace vk_gltf_viewer {
    [[nodiscard]] auto to_string(fastgltf::PrimitiveType value) noexcept -> const char*;
    [[nodiscard]] auto to_string(fastgltf::AccessorType value) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(fastgltf::ComponentType value) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(fastgltf::BufferTarget target) noexcept -> const char*;
    [[nodiscard]] auto to_string(fastgltf::MimeType mime) noexcept -> const char*;
    [[nodiscard]] auto to_string(fastgltf::AlphaMode alphaMode) noexcept -> const char*;
    [[nodiscard]] auto to_string(fastgltf::Filter filter) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(fastgltf::Wrap wrap) noexcept -> std::string_view;
}