module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer:gltf.data_structure.MaterialVariantsMapping;

import std;
export import fastgltf;
import :helpers.ranges;

namespace vk_gltf_viewer::gltf::ds {
    /**
     * @brief Associative data structure of mappings for KHR_materials_variants.
     *
     * KHR_materials_variants extension defines the material variants for each primitive. For each variant index, you can call `at` to get the list of primitives and their material indices that use the corresponding material variant.
     */
    export struct MaterialVariantsMapping : std::unordered_map<std::size_t, std::vector<std::pair<fastgltf::Primitive*, std::size_t>>> {
        explicit MaterialVariantsMapping(fastgltf::Asset &asset LIFETIMEBOUND) noexcept {
            for (fastgltf::Mesh &mesh : asset.meshes) {
                for (fastgltf::Primitive &primitive : mesh.primitives) {
                    for (const auto &[i, mapping] : primitive.mappings | ranges::views::enumerate) {
                        this->operator[](i).emplace_back(&primitive, mapping.value_or(primitive.materialIndex.value()));
                    }
                }
            }
        }
    };
}