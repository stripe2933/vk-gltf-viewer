module;

#include <cassert>

export module vk_gltf_viewer:gltf.algorithm.bounding_box;

import std;
export import fastgltf;
import :helpers.fastgltf;
import :helpers.functional;

namespace vk_gltf_viewer::gltf::algorithm {
    /**
     * @brief Get min/max points of \p primitive's bounding box.
     *
     * @tparam T Floating point type for calculate position, default: <tt>double</tt>.
     * @param primtiive primitive to get the bounding box corner points.
     * @param node Node that owns \p primitive.
     * @param asset Asset that owns \p node.
     * @return Array of (min, max) of the bounding box.
     */
    export template <std::floating_point T = double>
    [[nodiscard]] std::array<fastgltf::math::vec<T, 3>, 2> getBoundingBoxMinMax(
        const fastgltf::Primitive &primitive,
        const fastgltf::Node &node,
        const fastgltf::Asset &asset
    ) {
        constexpr auto getAccessorMinMax = [](const fastgltf::Accessor &accessor) {
            constexpr auto fetchVec3 = multilambda {
                []<typename U>(const std::pmr::vector<U> &v) {
                    assert(v.size() == 3);
                    return fastgltf::math::vec<T, 3> { static_cast<T>(v[0]), static_cast<T>(v[1]), static_cast<T>(v[2]) };
                },
                [](const auto&) -> fastgltf::math::vec<T, 3> {
                    throw std::invalid_argument { "Accessor min/max is not number" };
                },
            };

            fastgltf::math::vec<T, 3> min = visit(fetchVec3, accessor.min);
            fastgltf::math::vec<T, 3> max = visit(fetchVec3, accessor.max);

            if (accessor.normalized) {
                switch (accessor.componentType) {
                case fastgltf::ComponentType::Byte:
                    min = cwiseMax(min / 127, fastgltf::math::vec<T, 3>(-1));
                    max = cwiseMax(max / 127, fastgltf::math::vec<T, 3>(-1));
                    break;
                case fastgltf::ComponentType::UnsignedByte:
                    min /= 255;
                    max /= 255;
                    break;
                case fastgltf::ComponentType::Short:
                    min = cwiseMax(min / 32767, fastgltf::math::vec<T, 3>(-1));
                    max = cwiseMax(max / 32767, fastgltf::math::vec<T, 3>(-1));
                    break;
                case fastgltf::ComponentType::UnsignedShort:
                    min /= 65535;
                    max /= 65535;
                    break;
                default:
                    throw std::logic_error { "Normalized accessor must be either BYTE, UNSIGNED_BYTE, SHORT, or UNSIGNED_SHORT" };
                }
            }
            return std::array { min, max };
        };

        const fastgltf::Accessor &accessor = asset.accessors[primitive.findAttribute("POSITION")->accessorIndex];
        std::array bound = getAccessorMinMax(accessor);

        for (const auto &[weight, attributes] : std::views::zip(getTargetWeights(node, asset), primitive.targets)) {
            for (const auto &[attributeName, accessorIndex] : attributes) {
                using namespace std::string_view_literals;
                if (attributeName == "POSITION"sv) {
                    const fastgltf::Accessor &accessor = asset.accessors[accessorIndex];
                    std::array offset = getAccessorMinMax(accessor);

                    // TODO: is this code valid? Need investigation.
                    if (weight < 0) {
                        std::swap(get<0>(offset), get<1>(offset));
                    }
                    get<0>(bound) += get<0>(offset) * weight;
                    get<1>(bound) += get<1>(offset) * weight;

                    break;
                }
            }
        }

        return bound;
    }

    /**
     * @brief Get 8 corner points of \p primitive's bounding box, which are ordered by:
     * - (minX, minY, minZ)
     * - (minX, minY, maxZ)
     * - (minX, maxY, minZ)
     * - (minX, maxY, maxZ)
     * - (maxX, minY, minZ)
     * - (maxX, minY, maxZ)
     * - (maxX, maxY, minZ)
     * - (maxX, maxY, maxZ)
     *
     * @param primtiive primitive to get the bounding box corner points.
     * @param node Node that owns \p primitive.
     * @param asset Asset that owns \p node.
     * @return Array of 8 corner points of the bounding box.
     */
    export
    [[nodiscard]] std::array<fastgltf::math::dvec3, 8> getBoundingBoxCornerPoints(
        const fastgltf::Primitive &primitive,
        const fastgltf::Node &node,
        const fastgltf::Asset &asset
    ) {
        const auto [min, max] = getBoundingBoxMinMax(primitive, node, asset);
        return {
            min,
            fastgltf::math::dvec3 { min[0], min[1], max[2] },
            fastgltf::math::dvec3 { min[0], max[1], min[2] },
            fastgltf::math::dvec3 { min[0], max[1], max[2] },
            fastgltf::math::dvec3 { max[0], min[1], min[2] },
            fastgltf::math::dvec3 { max[0], min[1], max[2] },
            fastgltf::math::dvec3 { max[0], max[1], min[2] },
            max,
        };
    }
}