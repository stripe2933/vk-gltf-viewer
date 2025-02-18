export module vk_gltf_viewer:gltf.algorithm.bounding_box;

import std;
export import fastgltf;

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
            if (accessor.normalized) {
                // Unless KHR_mesh_quantization extension is enabled, POSITION accessor must be VEC3/FLOAT type.
                // Normalized accessor means that extension is used.
                throw std::runtime_error { "Sorry, unimplemented." };
            }

            const auto pMin = std::get_if<std::pmr::vector<double>>(&accessor.min)->data();
            const auto pMax = std::get_if<std::pmr::vector<double>>(&accessor.max)->data();
            return std::array {
                fastgltf::math::vec<T, 3> { static_cast<T>(pMin[0]), static_cast<T>(pMin[1]), static_cast<T>(pMin[2]) },
                fastgltf::math::vec<T, 3> { static_cast<T>(pMax[0]), static_cast<T>(pMax[1]), static_cast<T>(pMax[2]) },
            };
        };

        const fastgltf::Accessor &accessor = asset.accessors[primitive.findAttribute("POSITION")->accessorIndex];
        std::array bound = getAccessorMinMax(accessor);

        std::span<const float> morphTargetWeights = node.weights;
        if (node.meshIndex && !asset.meshes[*node.meshIndex].weights.empty()) {
            morphTargetWeights = asset.meshes[*node.meshIndex].weights;
        }

        for (const auto &[weight, attributes] : std::views::zip(morphTargetWeights, primitive.targets)) {
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