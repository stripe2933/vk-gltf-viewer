export module vk_gltf_viewer.gltf.util;

import std;
export import fastgltf;

import vk_gltf_viewer.helpers.concepts;
import vk_gltf_viewer.helpers.fastgltf;
import vk_gltf_viewer.helpers.ranges;

namespace vk_gltf_viewer::gltf {
    /**
     * @brief Get what material variant is used for given \p asset.
     *
     * You may need to pass your own \p originalMaterialIndexGetter, if asset primitive material is modified. In the
     * case, you can first store the primitive's material index at the asset loading and use it.
     *
     * @param asset Asset to determine.
     * @param originalMaterialIndexGetter Functor that returns the material index for given primitive.
     * @return Index of the active material variant, or std::nullopt if there's no matching material variant.
     */
    export
    [[nodiscard]] std::optional<std::size_t> getActiveMaterialVariantIndex(
        const fastgltf::Asset &asset,
        concepts::signature_of<std::optional<std::size_t>(const fastgltf::Primitive&)> auto &&originalMaterialIndexGetter = [](const fastgltf::Primitive &primitive) noexcept {
            return to_optional(primitive.materialIndex);
        }
    ) {
        // Primitives that are affected by KHR_materials_variants.
        auto variantPrimitives
            = asset.meshes
            | std::views::transform(&fastgltf::Mesh::primitives)
            | std::views::join
            | std::views::filter([](const fastgltf::Primitive &primitive) noexcept {
                return !primitive.mappings.empty();
            });

        for (std::size_t variantIndex : ranges::views::upto(asset.materialVariants.size())) {
            if (std::ranges::all_of(variantPrimitives, [&](const fastgltf::Primitive &primitive) {
                if (const auto &variantMaterialIndex = primitive.mappings.at(variantIndex)) {
                    return primitive.materialIndex == *variantMaterialIndex;
                }
                else {
                    return to_optional(primitive.materialIndex) == std::invoke(originalMaterialIndexGetter, primitive);
                }
            })) {
                return variantIndex;
            }
        }
        return std::nullopt;
    }

    /**
     * @brief Check whether the given material is compatible with the primitive.
     *
     * A material is compatible with a primitive if all the following conditions are met:
     * - If the material has a normal texture, the primitive must be either non-indexed or having NORMAL attribute.
     *   - Note: If non-indexed, NORMAL and TANGENT are generated in shader.
     *   - Note: For indexed primitives, TANGENT attribute is optional, as it is generated at the asset loading time.
     * - All TEXCOORD_<i>s referenced by the material textures must be presented in the primitive attributes.
     *
     * @param material Material to check.
     * @param primitive Primitive to check against.
     * @return <true</true> if the material is compatible with the primitive, <false>false</false> otherwise.
     */
    export
    [[nodiscard]] bool isMaterialCompatible(const fastgltf::Material &material, const fastgltf::Primitive &primitive) noexcept;
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

using namespace std::string_view_literals;

bool vk_gltf_viewer::gltf::isMaterialCompatible(const fastgltf::Material &material, const fastgltf::Primitive &primitive) noexcept {
	// Refer the function doxygen comment for the logic detail.
	bool canHaveNormalTexture = !primitive.indicesAccessor.has_value();
	std::size_t texcoordCount = 0;
	for (const auto &[attributeName, _] : primitive.attributes) {
		if (attributeName == "NORMAL"sv) {
			canHaveNormalTexture = true;
		}
		else if (attributeName.starts_with("TEXCOORD_"sv)) {
			++texcoordCount;
		}
	}

	if (const auto &texture = material.pbrData.baseColorTexture) {
		if (getTexcoordIndex(*texture) >= texcoordCount) {
			return false;
		}
	}
	if (const auto &texture = material.pbrData.metallicRoughnessTexture) {
		if (getTexcoordIndex(*texture) >= texcoordCount) {
			return false;
		}
	}
	if (const auto &texture = material.normalTexture) {
		if (!canHaveNormalTexture || getTexcoordIndex(*texture) >= texcoordCount) {
			return false;
		}
	}
	if (const auto &texture = material.occlusionTexture) {
		if (getTexcoordIndex(*texture) >= texcoordCount) {
			return false;
		}
	}
	if (const auto &texture = material.emissiveTexture) {
		if (getTexcoordIndex(*texture) >= texcoordCount) {
			return false;
		}
	}

	return true;

}