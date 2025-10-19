export module vk_gltf_viewer.gltf.AssetExtended;

import std;
export import fastgltf;

export import vk_gltf_viewer.gltf.Animation;
import vk_gltf_viewer.gltf.algorithm.miniball;
export import vk_gltf_viewer.gltf.AssetExternalBuffers;
export import vk_gltf_viewer.gltf.TextureUsage;
export import vk_gltf_viewer.gltf.SceneInverseHierarchy;
export import vk_gltf_viewer.gltf.StateCachedNodeVisibilityStructure;
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

    	bool isTextureTransformUsed;

    	/**
		 * @brief Association of primitive -> original material index.
		 */
        std::unordered_map<const fastgltf::Primitive*, std::optional<std::size_t>> originalMaterialIndexByPrimitive;

        /**
         * @brief Map of (material index, texture usage flags) for each texture.
         */
        std::vector<std::unordered_map<std::size_t, Flags<TextureUsage>>> textureUsages;

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

        /**
         * @brief World transform matrices of nodes in the scene, ordered by node indices in the asset.
         *
         * Only element at the index of node that is belonged to the currently enabled scene will have the meaningful value.
         */
        std::vector<fastgltf::math::fmat4x4> nodeWorldTransforms;

        /**
         * @brief Scene inverse hierarchy.
         */
        SceneInverseHierarchy sceneInverseHierarchy;

        /**
         * @brief Level (distance to the root) of each node in the scene, ordered by node indices in the asset.
         *
         * Only element at the index of node that is belonged to the currently enabled scene will have the meaningful value.
         */
        std::vector<std::size_t> sceneNodeLevels;

        /**
         * @brief State cached node visibility structure.
         */
        StateCachedNodeVisibilityStructure sceneNodeVisibilities;

        /**
         * @brief Indices of the nodes that are currently selected in the scene.
         */
        std::unordered_set<std::size_t> selectedNodes;

        /**
         * @brief Index of the node that is currently hovered by either mouse cursor in viewport or by the node inspector.
         */
        std::optional<std::size_t> hoveringNode;

		/**
		 * @brief Smallest enclosing sphere of all meshes, cameras and lights (a.k.a. miniball) in the scene.
         *
		 * The first of the pair is the center, and the second is the radius of the miniball.
		 */
        Lazy<std::tuple<fastgltf::math::dvec3, double, std::vector<fastgltf::math::fvec3>>> sceneMiniball;

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
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

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
	, isTextureTransformUsed { ranges::contains(asset.extensionsUsed, "KHR_texture_transform"sv) }
    , sceneIndex { asset.defaultScene.value_or(0) }
    , sceneInverseHierarchy { asset, sceneIndex }
    , sceneNodeVisibilities { asset, sceneIndex, sceneInverseHierarchy }
    , sceneMiniball { [this] {
        return algorithm::getMiniball(asset, sceneIndex, nodeWorldTransforms, externalBuffers);
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
        if (const auto &textureInfo = material.pbrData.baseColorTexture) {
            textureUsages[textureInfo->textureIndex][i] |= TextureUsage::BaseColor;
        }
        if (const auto &textureInfo = material.pbrData.metallicRoughnessTexture) {
            textureUsages[textureInfo->textureIndex][i] |= TextureUsage::MetallicRoughness;
        }
        if (const auto &textureInfo = material.normalTexture) {
            textureUsages[textureInfo->textureIndex][i] |= TextureUsage::Normal;
        }
        if (const auto &textureInfo = material.occlusionTexture) {
            textureUsages[textureInfo->textureIndex][i] |= TextureUsage::Occlusion;
        }
        if (const auto &textureInfo = material.emissiveTexture) {
            textureUsages[textureInfo->textureIndex][i] |= TextureUsage::Emissive;
        }
    }

    // bloomMaterials
	if (ranges::contains(asset.extensionsUsed, "KHR_materials_emissive_strength"sv)) {
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
	if (ranges::contains(asset.extensionsUsed, "KHR_materials_variants"sv)) {
		imGuiSelectedMaterialVariantsIndex = getActiveMaterialVariantIndex(asset, [this](const fastgltf::Primitive &primitive) {
			return originalMaterialIndexByPrimitive.at(&primitive);
		});
	}

	// nodeWorldTransforms
	nodeWorldTransforms.resize(asset.nodes.size());
	traverseScene(asset, getScene(), [this](std::size_t nodeIndex, const fastgltf::math::fmat4x4 &nodeWorldTransform) {
		nodeWorldTransforms[nodeIndex] = nodeWorldTransform;
	});

	// sceneNodeLevels
	sceneNodeLevels.resize(asset.nodes.size());
    for (std::size_t nodeIndex : getScene().nodeIndices) {
        [this](this const auto &self, std::size_t nodeIndex, std::size_t level) -> void {
            sceneNodeLevels[nodeIndex] = level;

            for (std::size_t childNodeIndex : asset.nodes[nodeIndex].children) {
                self(childNodeIndex, level + 1);
            }
        }(nodeIndex, 0);
    }
}

void vk_gltf_viewer::gltf::AssetExtended::setScene(std::size_t _sceneIndex) {
	sceneIndex = _sceneIndex;

	// nodeWorldTransforms
	traverseScene(asset, getScene(), [this](std::size_t nodeIndex, const fastgltf::math::fmat4x4 &nodeWorldTransform) {
		nodeWorldTransforms[nodeIndex] = nodeWorldTransform;
	});

	sceneInverseHierarchy = { asset, sceneIndex };

	// sceneNodeLevels
	for (std::size_t nodeIndex : getScene().nodeIndices) {
		[this](this const auto &self, std::size_t nodeIndex, std::size_t level) -> void {
			sceneNodeLevels[nodeIndex] = level;

			for (std::size_t childNodeIndex : asset.nodes[nodeIndex].children) {
				self(childNodeIndex, level + 1);
			}
		}(nodeIndex, 0);
	}

    sceneNodeVisibilities = { asset, sceneIndex, sceneInverseHierarchy };
    selectedNodes.clear();
    hoveringNode.reset();
    sceneMiniball.invalidate();
}
