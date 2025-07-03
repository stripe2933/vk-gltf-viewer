module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.gltf.OrderedPrimitives;

import std;
export import fastgltf;

import vk_gltf_viewer.helpers.functional;
import vk_gltf_viewer.helpers.ranges;

namespace vk_gltf_viewer::gltf {
    /**
     * @brief Ordered asset primitives.
     *
     * glTF asset primitives are stored inside the mesh, and they have no asset-wide ordering. This vector is constructed by traversing the asset meshes in order and collecting their primitives.
     */
    export class OrderedPrimitives : public std::vector<const fastgltf::Primitive*> {
    public:
        explicit OrderedPrimitives(const fastgltf::Asset &asset LIFETIMEBOUND);

        [[nodiscard]] std::size_t getIndex(const fastgltf::Primitive &primitive) const;

    private:
        std::unordered_map<const fastgltf::Primitive*, std::size_t> indices;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

vk_gltf_viewer::gltf::OrderedPrimitives::OrderedPrimitives(const fastgltf::Asset &asset)
    : vector {
        std::from_range,
        asset.meshes
            | std::views::transform(&fastgltf::Mesh::primitives)
            | std::views::join
            | std::views::transform(LIFT(std::addressof)),
    }
    , indices {
        std::from_range,
        ranges::views::enumerate(*this)
            | std::views::transform(decomposer([](std::size_t index, const fastgltf::Primitive *pPrimitive) noexcept {
                return std::pair { pPrimitive, index };
            })),
    } { }

std::size_t vk_gltf_viewer::gltf::OrderedPrimitives::getIndex(const fastgltf::Primitive &primitive) const {
    return indices.at(&primitive);
}