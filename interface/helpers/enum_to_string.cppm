module;

#include <cstdint>

export module vk_gltf_viewer:helpers.enum_to_string;

// Forward declarations.
namespace fastgltf {
    enum class PrimitiveType : std::uint8_t;
    enum class AccessorType : std::uint16_t;
    enum class ComponentType : std::uint32_t;
}

export namespace vk_gltf_viewer {
    [[nodiscard]] auto to_string(fastgltf::PrimitiveType value) noexcept -> const char*;
    [[nodiscard]] auto to_string(fastgltf::AccessorType value) noexcept -> const char*;
    [[nodiscard]] auto to_string(fastgltf::ComponentType value) noexcept -> const char*;
}