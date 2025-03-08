module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer:gltf.OrderedPrimitives;

import std;
export import fastgltf;
import :helpers.functional;
import :helpers.ranges;

namespace vk_gltf_viewer::gltf {
    /**
     * @brief Ordered asset primitives.
     *
     * glTF asset primitives are stored inside the mesh, and they have no asset-wide ordering. This vector is constructed by traversing the asset meshes in order and collecting their primitives.
     */
    export class OrderedPrimitives : public std::vector<const fastgltf::Primitive*> {
    public:
        explicit OrderedPrimitives(const fastgltf::Asset &asset LIFETIMEBOUND)
            : vector {
                std::from_range,
                asset.meshes
                    | std::views::transform(&fastgltf::Mesh::primitives)
                    | std::views::join
                    | ranges::views::addressof,
            }
            , indices {
                std::from_range,
                *this
                    | ranges::views::enumerate
                    | std::views::transform(decomposer([](std::size_t index, const fastgltf::Primitive *pPrimitive) noexcept {
                        return std::pair { pPrimitive, index };
                    })),
            } { }

        [[nodiscard]] std::size_t getIndex(const fastgltf::Primitive &primitive) const {
            return indices.at(&primitive);
        }

    private:
        std::unordered_map<const fastgltf::Primitive*, std::size_t> indices;
    };
}