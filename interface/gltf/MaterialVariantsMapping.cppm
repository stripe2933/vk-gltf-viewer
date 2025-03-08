module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer:gltf.MaterialVariantsMapping;

import std;
export import fastgltf;
import :helpers.ranges;

namespace vk_gltf_viewer::gltf {
    /**
     * @brief Associative data structure of mappings for KHR_materials_variants.
     *
     * KHR_materials_variants extension defines the material variants for each primitive. For each variant index, you can call `at` to get the list of primitives and their material indices that use the corresponding material variant.
     */
    class MaterialVariantsMapping {
    public:
        struct VariantPrimitive {
            fastgltf::Primitive *pPrimitive;
            std::uint32_t materialIndex;
        };

        explicit MaterialVariantsMapping(fastgltf::Asset &asset LIFETIMEBOUND) noexcept {
            for (fastgltf::Primitive &primitive : asset.meshes | std::views::transform(&fastgltf::Mesh::primitives) | std::views::join) {
                for (const auto &[materialVariantIndex, mapping] : primitive.mappings | ranges::views::enumerate) {
                    if (mapping) {
                        data[materialVariantIndex].emplace_back(&primitive, *mapping);
                    }
                }
            }
        }

        [[nodiscard]] std::span<const VariantPrimitive> at(std::size_t materialVariantIndex) const {
            return data.at(materialVariantIndex);
        }

    private:
        std::unordered_map<std::size_t, std::vector<VariantPrimitive>> data;
    };
}