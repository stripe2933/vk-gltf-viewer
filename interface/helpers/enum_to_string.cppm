module;

#include <fastgltf/types.hpp>

export module vk_gltf_viewer:helpers.enum_to_string;

export namespace vk_gltf_viewer {
    [[nodiscard]] auto to_string(fastgltf::PrimitiveType value) noexcept -> const char*;
    [[nodiscard]] auto to_string(fastgltf::AccessorType value) noexcept -> const char*;
    [[nodiscard]] auto to_string(fastgltf::ComponentType value) noexcept -> const char*;
}