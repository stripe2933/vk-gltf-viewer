module;

#include <imgui.h>
#include <ImGuizmo.h>

export module vk_gltf_viewer:AppState;

import std;
export import fastgltf;
export import glm;
export import :control.Camera;
export import :helpers.full_optional;
import :helpers.functional;
import :helpers.optional;
import :helpers.ranges;

namespace vk_gltf_viewer {
    export class AppState {
    public:
        struct Outline {
            float thickness;
            glm::vec4 color;
        };

        struct ImageBasedLighting {
            struct EquirectangularMap {
                std::filesystem::path path;
                glm::u32vec2 dimension;
            } eqmap;

            struct Cubemap {
                std::uint32_t size;
            } cubemap;

            struct DiffuseIrradiance {
                std::array<glm::vec3, 9> sphericalHarmonicCoefficients;
            } diffuseIrradiance;

            struct Prefilteredmap {
                std::uint32_t size;
                std::uint32_t roughnessLevels;
                std::uint32_t sampleCount;
            } prefilteredmap;
        };

        class GltfAsset {
        public:
            fastgltf::Asset &asset;
            std::variant<std::vector<std::optional<bool>>, std::vector<bool>> nodeVisibilities { std::in_place_index<0>, asset.nodes.size(), true };
            std::optional<std::size_t> assetInspectorMaterialIndex = asset.materials.empty() ? std::optional<std::size_t>{} : std::optional<std::size_t> { 0 };

            std::unordered_set<std::size_t> selectedNodeIndices;
            std::optional<std::size_t> hoveringNodeIndex;

            explicit GltfAsset(fastgltf::Asset &asset) noexcept
                : asset { asset } { }

            [[nodiscard]] auto getSceneIndex() const noexcept -> std::size_t { return sceneIndex; }
            [[nodiscard]] auto getScene() const noexcept -> fastgltf::Scene& { return asset.scenes[sceneIndex]; }
            auto setScene(std::size_t _sceneIndex) noexcept -> void {
                sceneIndex = _sceneIndex;
                visit([](auto &visibilities) {
                    std::ranges::fill(visibilities, true);
                }, nodeVisibilities);
                selectedNodeIndices.clear();
                hoveringNodeIndex.reset();
            }

            /**
             * From <tt>nodeVisibilities</tt>, get the unique indices of the visible nodes.
             * @return <tt>std::unordered_set</tt> of the visible node indices.
             * @note Since the result only contains node which is visible, nodes without mesh are excluded regardless of
             * its corresponding <tt>nodeVisibilities</tt> is <tt>true</tt>.
             */
            [[nodiscard]] auto getVisibleNodeIndices() const noexcept -> std::unordered_set<std::size_t> {
                return visit(multilambda {
                    [this](std::span<const std::optional<bool>> tristateVisibilities) {
                        return tristateVisibilities
                            | ranges::views::enumerate
                            | std::views::filter(decomposer([this](std::size_t nodeIndex, std::optional<bool> visibility) {
                                return visibility.value_or(false) && asset.nodes[nodeIndex].meshIndex.has_value();
                            }))
                            | std::views::keys
                            // Explicit value type must be specified, because std::views::enumerate's index type is range_difference_t<R> (!= std::size_t).
                            | std::ranges::to<std::unordered_set<std::size_t>>();
                    },
                    [this](const std::vector<bool> &visibilities) {
                        return visibilities
                            | ranges::views::enumerate
                            | std::views::filter(decomposer([this](std::size_t nodeIndex, bool visibility) {
                                return visibility && asset.nodes[nodeIndex].meshIndex.has_value();
                            }))
                            | std::views::keys
                            // Explicit value type must be specified, because std::views::enumerate's index type is range_difference_t<R> (!= std::size_t).
                            | std::ranges::to<std::unordered_set<std::size_t>>();
                    }
                }, nodeVisibilities);
            }

            /**
             * Get index of the parent node of the given node.
             * @param nodeIndex Index of the node.
             * @return Index of the parent node if the current node is not root node, otherwise <tt>std::nullopt</tt>.
             */
            [[nodiscard]] std::optional<std::size_t> getParentNodeIndex(std::size_t nodeIndex) const noexcept {
                const std::size_t parentNodeIndex = parentNodeIndices[nodeIndex];
                return value_if(parentNodeIndex != nodeIndex, parentNodeIndex);
            }

        private:
            std::size_t sceneIndex = asset.defaultScene.value_or(0);
            std::vector<std::size_t> parentNodeIndices = createParentNodeIndices();

            [[nodiscard]] auto createParentNodeIndices() const noexcept -> std::vector<std::size_t> {
                std::vector indices{ std::from_range, std::views::iota(std::size_t{ 0 }, asset.nodes.size()) };
                for (const auto &[i, node] : asset.nodes | ranges::views::enumerate) {
                    for (std::size_t childIndex : node.children) {
                        indices[childIndex] = i;
                    }
                }
                return indices;
            }
        };

        control::Camera camera;
        bool automaticNearFarPlaneAdjustment = true;
        bool useFrustumCulling = false;
        std::optional<glm::vec2> hoveringMousePosition;
        full_optional<Outline> hoveringNodeOutline { std::in_place, 2.f, glm::vec4 { 1.f, 0.5f, 0.2f, 1.f } };
        full_optional<Outline> selectedNodeOutline { std::in_place, 2.f, glm::vec4 { 0.f, 1.f, 0.2f, 1.f } };
        bool canSelectSkyboxBackground = false; // TODO: bad design... this and background should be handled in a single field.
        full_optional<glm::vec3> background { std::in_place, 0.f, 0.f, 0.f }; // nullopt -> use cubemap from the given equirectangular map image.
        std::optional<ImageBasedLighting> imageBasedLightingProperties;
        std::optional<GltfAsset> gltfAsset;
        ImGuizmo::OPERATION imGuizmoOperation = ImGuizmo::OPERATION::TRANSLATE;

        AppState() noexcept;
        ~AppState();

        [[nodiscard]] auto getRecentGltfPaths() const -> const std::list<std::filesystem::path>& { return recentGltfPaths; }
        auto pushRecentGltfPath(const std::filesystem::path &path) -> void;
        [[nodiscard]] auto getRecentSkyboxPaths() const -> const std::list<std::filesystem::path>& { return recentSkyboxPaths; }
        auto pushRecentSkyboxPath(const std::filesystem::path &path) -> void;

        [[nodiscard]] auto canManipulateImGuizmo() const -> bool { return gltfAsset && gltfAsset->selectedNodeIndices.size() == 1; }

    private:
        std::list<std::filesystem::path> recentGltfPaths;
        std::list<std::filesystem::path> recentSkyboxPaths;
    };
}