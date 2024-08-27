module;

#include <fastgltf/types.hpp>

export module vk_gltf_viewer:helpers.enum_to_string;

export import cstring_view;

export namespace vk_gltf_viewer {
    [[nodiscard]] auto to_string(fastgltf::PrimitiveType value) noexcept -> cstring_view;
    [[nodiscard]] auto to_string(fastgltf::AccessorType value) noexcept -> cstring_view;
    [[nodiscard]] auto to_string(fastgltf::ComponentType value) noexcept -> cstring_view;
    [[nodiscard]] auto to_string(fastgltf::BufferTarget target) noexcept -> cstring_view;
    [[nodiscard]] auto to_string(fastgltf::MimeType mime) noexcept -> cstring_view;
    [[nodiscard]] auto to_string(fastgltf::AlphaMode alphaMode) noexcept -> cstring_view;
    [[nodiscard]] auto to_string(fastgltf::Filter filter) noexcept -> cstring_view;
    [[nodiscard]] auto to_string(fastgltf::Wrap wrap) noexcept -> cstring_view;
}