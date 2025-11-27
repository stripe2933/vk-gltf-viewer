export module vk_gltf_viewer.gltf.AssetExtended;

import std;
export import fastgltf;

export import vk_gltf_viewer.gltf.Animation;
import vk_gltf_viewer.gltf.algorithm.miniball;
export import vk_gltf_viewer.gltf.AssetExternalBuffers;
export import vk_gltf_viewer.gltf.SceneHierarchy;
import vk_gltf_viewer.gltf.util;
export import vk_gltf_viewer.imgui.ColorSpaceAndUsageCorrectedTextures;
import vk_gltf_viewer.helpers.fastgltf;
import vk_gltf_viewer.helpers.functional;
import vk_gltf_viewer.helpers.ranges;
export import vk_gltf_viewer.helpers.Lazy;

namespace vk_gltf_viewer::gltf {
    export class AssetExtended : public imgui::ColorSpaceAndUsageCorrectedTextures {
        fastgltf::GltfDataBuffer dataBuffer;

	public:
    	// ----- Asset data -----

        /**
         * @brief The directory where the glTF file is located.
         */
        std::filesystem::path directory;

        /**
         * @brief The glTF asset that is loaded from the file.
         *
         * This MUST not be assigned (<tt>AssetExtended</tt> and <tt>fastgltf::Asset</tt> is one-to-one relationship).
         */
        fastgltf::Asset asset;

    	/**
		 * @brief Association of primitive -> original material index.
		 */
        std::unordered_map<const fastgltf::Primitive*, std::optional<std::size_t>> originalMaterialIndexByPrimitive;

        /**
         * @brief Map of (material index, texture usage flags) for each texture.
         */
        std::vector<std::unordered_map<std::size_t, Flags<fastgltf::TextureUsage>>> textureUsages;

        /// All <tt>fastgltf::TextureInfo</tt> (or its derivatives like <tt>fastgltf::NormalTextureInfo</tt>) that have been transformed by KHR_texture_transform extension.
        std::unordered_set<const fastgltf::TextureInfo*> transformedTextureInfos;

        /**
         * @brief Indices of glTF asset materials whose emissive strength is greater than 1.0.
         *
         * This is used for determine whether enable renderer's bloom effect or not (by checking this container is empty).
         */
        std::unordered_set<std::size_t> bloomMaterials;

        /**
		 * @brief External buffers that are not embedded in the glTF file, such like .bin files.
         */
        AssetExternalBuffers externalBuffers { asset, directory };

		/**
		 * @brief Pairs of (animation, enabled) that are currently loaded in the asset.
		 */
        std::vector<std::pair<Animation, bool /* enabled? */>> animations;

        /**
         * @brief Index of the material that is currently selected in the ImGui Material Editor.
         */
        std::optional<std::size_t> imGuiSelectedMaterialIndex;

        /**
         * @brief Index of the material variants that is currently used.
         *
         * If <tt>KHR_materials_variants</tt> extension is used, it is attempted to be calculated by iterating over all
         * primitives in the asset and check the material index is used by the material variants. However, it may failed
         * and the value will be <tt>std::nullopt</tt> in that case.
         */
        std::optional<std::size_t> imGuiSelectedMaterialVariantsIndex;

    	// ----- Scene data -----

        /**
         * @brief Index of the currently enabled scene in the asset.
         */
        std::size_t sceneIndex;

    	/// Node name search text in scene hierarchy window.
    	std::string nodeNameSearchText;

    	/// Occurrence positions of <tt>nodeNameSearchText</tt> in the scene node names.
    	/// If the occurrence position is equal to the node name length, it means the node has no occurrence but it is
    	/// an ancestor node of a node that has the occurrence.
    	std::unordered_map<std::size_t, std::size_t> nodeNameSearchTextOccurrencePosByNode;

        SceneHierarchy sceneHierarchy;

        /**
         * @brief Indices of the nodes that are currently selected in the scene.
         */
        std::unordered_set<std::size_t> selectedNodes;

        /**
         * @brief Index of the node that is currently hovered by either mouse cursor in viewport or by the node inspector.
         */
        std::optional<std::size_t> hoveringNode;

		/**
		 * @brief Scene mesh miniball and camera/light world positions.
         *
		 * The first and second tuple elements represent the center and radius of the miniball, respectively.
		 * The third tuple element is a vector of world space positions of camera or light nodes in the scene.
		 */
        Lazy<std::tuple<fastgltf::math::fvec3, float, std::vector<fastgltf::math::fvec3>>> sceneMiniball;

    	explicit AssetExtended(const std::filesystem::path &path);

        /**
         * @brief Return whether the data of the image at the given index is loaded or not.
         * @param imageIndex Index of the image in the <tt>fastgltf::Asset::images</tt> vector.
         * @return <tt>true</tt> if the image data is loaded, <tt>false</tt> otherwise.
         */
        [[nodiscard]] virtual bool isImageLoaded(std::size_t imageIndex) const noexcept = 0;

		[[nodiscard]] fastgltf::Scene &getScene() noexcept { return asset.scenes[sceneIndex]; }
		[[nodiscard]] const fastgltf::Scene &getScene() const noexcept { return asset.scenes[sceneIndex]; }

    	void setScene(std::size_t sceneIndex);

		/**
		 * @brief Update <tt>nodeNameSearchTextOccurrencePosByNode</tt> with the current <tt>nodeNameSearchText</tt>.
		 *
		 * You should pass <tt>true</tt> to \p newResultWithinCurrent for optimization if the new result is always
		 * within the current result. For example, the case of the new search text contains the previously used
		 * non-empty search text. If it is <tt>false</tt>, all nodes in the scene will be checked.
		 *
		 * @param newResultWithinCurrent <tt>true</tt> if the new result is always within the current result, <tt>false</tt> otherwise.
		 */
    	void updateNodeNameSearchTextOccurrencePosByNode(bool newResultWithinCurrent);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

#define FWD(...) static_cast<decltype(__VA_ARGS__) &&>(__VA_ARGS__)
#define LIFT(...) [&](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

using namespace std::string_view_literals;

fastgltf::Parser parser {
	fastgltf::Extensions::KHR_materials_emissive_strength
		| fastgltf::Extensions::KHR_materials_ior
		| fastgltf::Extensions::KHR_materials_unlit
		| fastgltf::Extensions::KHR_materials_variants
		| fastgltf::Extensions::KHR_mesh_quantization
	#ifdef SUPPORT_KHR_TEXTURE_BASISU
		| fastgltf::Extensions::KHR_texture_basisu
	#endif
		| fastgltf::Extensions::KHR_texture_transform
		| fastgltf::Extensions::EXT_meshopt_compression
		| fastgltf::Extensions::EXT_mesh_gpu_instancing
};

vk_gltf_viewer::gltf::AssetExtended::AssetExtended(const std::filesystem::path &path)
	: dataBuffer { get_checked(fastgltf::GltfDataBuffer::FromPath(path)) }
    , directory { path.parent_path() }
    , asset { get_checked(parser.loadGltf(dataBuffer, directory)) }
    , sceneIndex { asset.defaultScene.value_or(0) }
    , sceneHierarchy { asset, sceneIndex }
    , sceneMiniball { [this] {
        return algorithm::getMiniball<true>(
        	asset,
        	asset.scenes[sceneIndex].nodeIndices,
        	sceneHierarchy.getWorldTransforms(),
        	externalBuffers,
        	LIFT(sceneHierarchy.isObjectlessRecursive));
    } } {
	// originalMaterialIndexByPrimitive
	for (fastgltf::Mesh &mesh: asset.meshes) {
		for (fastgltf::Primitive &primitive: mesh.primitives) {
			originalMaterialIndexByPrimitive.try_emplace(&primitive, primitive.materialIndex);
		}
	}

    // textureUsages
    textureUsages.resize(asset.textures.size());
    for (const auto &[i, material] : asset.materials | ranges::views::enumerate) {
    	enumerateTextureInfos(material, [&](const fastgltf::TextureInfo &textureInfo, Flags<fastgltf::TextureUsage> usage) noexcept {
            textureUsages[textureInfo.textureIndex][i] |= usage;
    	});
    }

	// transformedTextureInfos
	if (std::ranges::contains(asset.extensionsUsed, "KHR_texture_transform"sv)) {
		for (const fastgltf::Material &material : asset.materials) {
			enumerateTextureInfos(material, [&](const fastgltf::TextureInfo &textureInfo) noexcept {
				if (textureInfo.transform) {
					transformedTextureInfos.emplace(&textureInfo);
				}
			});
		}
	}

    // bloomMaterials
	if (std::ranges::contains(asset.extensionsUsed, "KHR_materials_emissive_strength"sv)) {
		for (const auto &[i, material] : asset.materials | ranges::views::enumerate) {
			if (material.emissiveStrength > 1.f) {
				bloomMaterials.emplace(i);
			}
		}
	}

	// animations
	animations.reserve(asset.animations.size());
	for (std::size_t animationIndex : ranges::views::upto(asset.animations.size())) {
		animations.emplace_back(
			std::piecewise_construct,
			std::tie(asset, animationIndex, externalBuffers),
			std::tuple { false });
	}

	// imGuiSelectedMaterialVariantsIndex
	if (std::ranges::contains(asset.extensionsUsed, "KHR_materials_variants"sv)) {
		imGuiSelectedMaterialVariantsIndex = getActiveMaterialVariantIndex(asset, [this](const fastgltf::Primitive &primitive) {
			return originalMaterialIndexByPrimitive.at(&primitive);
		});
	}
}

void vk_gltf_viewer::gltf::AssetExtended::setScene(std::size_t _sceneIndex) {
	sceneIndex = _sceneIndex;

	nodeNameSearchText.clear();
	nodeNameSearchTextOccurrencePosByNode.clear();

	sceneHierarchy = { asset, sceneIndex };

    selectedNodes.clear();
    hoveringNode.reset();
    sceneMiniball.invalidate();
}

void vk_gltf_viewer::gltf::AssetExtended::updateNodeNameSearchTextOccurrencePosByNode(
	bool newResultWithinCurrent
) {
	// Collect nodes that will be searched.
	std::vector<std::size_t> orderedNodes;
	if (newResultWithinCurrent) {
		orderedNodes.append_range(nodeNameSearchTextOccurrencePosByNode | std::views::keys);
	}
	else {
		traverseScene(asset, getScene(), [&](std::size_t nodeIndex) {
			orderedNodes.push_back(nodeIndex);
		});
	}

    // Sort nodes by their levels in descending order.
    std::ranges::sort(orderedNodes, std::ranges::greater{}, LIFT(sceneHierarchy.getNodeLevel));

    nodeNameSearchTextOccurrencePosByNode.clear();
    std::vector visited(asset.nodes.size(), false);
    for (std::size_t nodeIndex : orderedNodes) {
        std::string_view haystack = asset.nodes[nodeIndex].name;
        if (auto found = std::ranges::search(haystack, nodeNameSearchText, {}, LIFT(std::tolower), LIFT(std::tolower))) {
            nodeNameSearchTextOccurrencePosByNode[nodeIndex] = std::ranges::distance(haystack.begin(), found.begin());

            // If an occurrence is found in the node, all of its ancestor must be visible to the scene hierarchy
            // tree, regardless of they have occurrences or not.
            std::optional parentNodeIndex = sceneHierarchy.getParentNodeIndex(nodeIndex);
            while (parentNodeIndex && !visited[*parentNodeIndex]) {
                haystack = asset.nodes[*parentNodeIndex].name;
                found = std::ranges::search(haystack, nodeNameSearchText, {}, LIFT(std::tolower), LIFT(std::tolower));

                // If found == false, haystack.size() will be assigned.
                nodeNameSearchTextOccurrencePosByNode[*parentNodeIndex] = std::ranges::distance(haystack.begin(), found.begin());

                visited[*parentNodeIndex] = true;
                parentNodeIndex = sceneHierarchy.getParentNodeIndex(*parentNodeIndex);
            }
        }
    }
}