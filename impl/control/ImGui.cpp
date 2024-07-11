module;

#include <cassert>
#include <version>

#include <fastgltf/types.hpp>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <ImGuizmo.h>

module vk_gltf_viewer;
import :control.ImGui;

import std;
import glm;
import :helpers.enum_to_string;
import :helpers.formatters.joiner;
import :helpers.ranges;

[[nodiscard]] auto getAccessorByteStride(
    const fastgltf::Asset &asset,
    const fastgltf::Accessor &accessor
) -> std::size_t {
    return asset.bufferViews[*accessor.bufferViewIndex].byteStride
        .value_or(getElementByteSize(accessor.type, accessor.componentType));
}

auto vk_gltf_viewer::control::imgui::hdriEnvironments(
    ImTextureID eqmapTextureId,
    AppState &appState
) -> void {
    if (ImGui::Begin("HDRI environments info") && appState.imageBasedLightingProperties) {
        const auto &iblProps = *appState.imageBasedLightingProperties;

        ImGui::SeparatorText("Equirectangular map");

        const ImVec2 eqmapTexturePosition = ImGui::GetCursorScreenPos();
        const float eqmapAspectRatio = static_cast<float>(iblProps.eqmap.dimension.y) / iblProps.eqmap.dimension.x;
        const ImVec2 eqmapTextureSize = ImVec2 { 1.f, eqmapAspectRatio } * ImGui::GetContentRegionAvail().x;
        ImGui::Image(eqmapTextureId, eqmapTextureSize);

        // Show zoomed texture when hovering mouse over the eqmap texture.
        if (ImGui::BeginItemTooltip()) {
            const ImGuiIO &io = ImGui::GetIO();

            constexpr ImVec2 zoomedPortionSize { 32.f, 32.f };
            ImVec2 region = io.MousePos - eqmapTexturePosition - zoomedPortionSize * 0.5f;
            region.x = std::clamp(region.x, 0.f, eqmapTextureSize.x - zoomedPortionSize.x);
            region.y = std::clamp(region.y, 0.f, eqmapTextureSize.y - zoomedPortionSize.y);

            constexpr float zoomScale = 4.0f;
            ImGui::Image(eqmapTextureId, zoomedPortionSize * zoomScale, region / eqmapTextureSize, (region + zoomedPortionSize) / eqmapTextureSize);
            ImGui::Text("Showing: [%.0f, %.0f]x[%.0f, %.0f]", region.x, region.y, region.x + zoomedPortionSize.y, region.y + zoomedPortionSize.y);

            ImGui::EndTooltip();
        }

        ImGui::Text("File: %s", iblProps.eqmap.path.string().c_str());
        ImGui::Text("Dimension: %ux%u", iblProps.eqmap.dimension.x, iblProps.eqmap.dimension.y);

        ImGui::SeparatorText("Cubemap");
        ImGui::Text("Size: %u", iblProps.cubemap.size);
        ImGui::Checkbox("Use blurred skybox", &appState.useBlurredSkybox);

        ImGui::SeparatorText("Diffuse irradiance");
        ImGui::TextUnformatted("Spherical harmonic coefficients (up to 3rd band)");
        if (ImGui::BeginTable("spherical_harmonic_coeffs", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
            // Headers.
            ImGui::TableSetupColumn("Band", ImGuiTableColumnFlags_None);
            ImGui::TableSetupColumn("x", ImGuiTableColumnFlags_None);
            ImGui::TableSetupColumn("y", ImGuiTableColumnFlags_None);
            ImGui::TableSetupColumn("z", ImGuiTableColumnFlags_None);
            ImGui::TableHeadersRow();

            // Rows.
            static constexpr auto bandLabels = { "L0", "L1_1", "L10", "L11", "L2_2", "L2_1", "L20", "L21", "L22" };
            for (const auto &[label, coefficients] : std::views::zip(bandLabels, iblProps.diffuseIrradiance.sphericalHarmonicCoefficients)) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(label);
                ImGui::TableSetColumnIndex(1); ImGui::Text("%.3f", coefficients.x);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.3f", coefficients.y);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%.3f", coefficients.z);
            }

            ImGui::EndTable();
        }

        ImGui::SeparatorText("Prefiltered map");
        ImGui::Text("Size: %u", iblProps.prefilteredmap.size);
        ImGui::Text("Roughness levels: %u", iblProps.prefilteredmap.roughnessLevels);
        ImGui::Text("Samples: %u", iblProps.prefilteredmap.sampleCount);
    }
    ImGui::End();
}

auto vk_gltf_viewer::control::imgui::assetSceneHierarchies(
	const fastgltf::Asset &asset,
	AppState &appState
) -> void {
	if (ImGui::Begin("Asset scene hierarchies")) {
		ImGui::SeparatorText("View options");
		// Option whether to merge single child node.
		// If true, a node that has only one child will be merged into the parent node with slash(/) separator.
		static bool mergeSingleChildNode = true;
		ImGui::Checkbox("Merge single child node", &mergeSingleChildNode);

        ImGui::SeparatorText("Asset scenes");
        const auto sceneNames = asset.scenes | std::views::transform([](const auto &scene) { return scene.name.c_str(); });
        static int selectedSceneIndex = asset.defaultScene.value_or(0);
        if (ImGui::BeginCombo("Scene", sceneNames[selectedSceneIndex])) {
            for (auto [sceneIndex, sceneName]: sceneNames | ranges::views::enumerate) {
                const bool isSelected = selectedSceneIndex == sceneIndex;
                ImGui::PushID(sceneIndex);
                if (ImGui::Selectable(sceneName, isSelected)) selectedSceneIndex = sceneIndex;
                ImGui::PopID();
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        for (std::stack dfs { std::from_range, asset.scenes[selectedSceneIndex].nodeIndices | std::views::reverse }; !dfs.empty();) {
            std::size_t nodeIndex = dfs.top(); dfs.pop();
            const std::size_t ancestorNodeIndex = nodeIndex;
            const fastgltf::Node *node = &asset.nodes[nodeIndex];

            std::vector nodeNames { node->name.c_str() };
            if (mergeSingleChildNode) {
                while (node->children.size() == 1) {
                    nodeIndex = node->children[0];
                    node = &asset.nodes[nodeIndex];
                    nodeNames.push_back(node->name.c_str());
                }
            }

            // Why use ancestorNodeIndex for here?
            // If use nodeIndex (which is the descendent for mergeSingleChildNode == true), all nodes will be collapsed
            // when set mergeSingleChildNode from true to false, because the ancestor node index is never pushed to the
            // ImGui ID stack. To prevent this, ancestorNodeIndex is used.
            ImGui::PushID(ancestorNodeIndex);
            const ImGuiTreeNodeFlags flags
                = ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_OpenOnArrow
                | (appState.selectedNodeIndex && *appState.selectedNodeIndex == nodeIndex ? ImGuiTreeNodeFlags_Selected : 0)
                | (node->children.empty() ? ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet : 0);
#if __cpp_lib_format_ranges >= 202207L
            if (ImGui::TreeNodeEx("", flags, "%s", std::format("{::s}", make_joiner<" / ">(nodeNames)).c_str())) {
#else
            // It will use stopgap std::formatter<joiner<T, Delimiter>>, therefore format specification for T is not supported.
            if (ImGui::TreeNodeEx("", flags, "%s", std::format("{}", make_joiner<" / ">(nodeNames)).c_str())) {
#endif
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                    appState.selectedNodeIndex = static_cast<std::uint32_t>(nodeIndex);
                }
                dfs.push_range(node->children | std::views::reverse);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
	}
	ImGui::End();
}

auto vk_gltf_viewer::control::imgui::nodeInspector(
    const fastgltf::Asset &asset,
    AppState &appState
) -> void {
    if (ImGui::Begin("Node inspector") && appState.selectedNodeIndex) {
        const fastgltf::Node &node = asset.nodes[*appState.selectedNodeIndex];
        ImGui::Text("Node name: %s", node.name.c_str());

        ImGui::SeparatorText("Transform");

        ImGui::TextUnformatted("Local transform");
        std::visit(fastgltf::visitor {
            [](fastgltf::TRS trs) {
                ImGui::TextUnformatted("Type: TRS");
                // | operator cannot be chained, because of the short circuit evaulation.
                bool transformChanged = ImGui::DragFloat3("Translation", trs.translation.data());
                transformChanged |= ImGui::DragFloat4("Rotation", trs.rotation.data());
                transformChanged |= ImGui::DragFloat3("Scale", trs.scale.data());

                if (transformChanged) {
                    // TODO.
                }
            },
            [](fastgltf::Node::TransformMatrix matrix) {
                ImGui::TextUnformatted("Type: Matrix4x4");
                // | operator cannot be chained, because of the short circuit evaulation.
                bool transformChanged = ImGui::DragFloat4("Column 0", &matrix[0]);
                transformChanged |= ImGui::DragFloat4("Column 1", &matrix[4]);
                transformChanged |= ImGui::DragFloat4("Column 2", &matrix[8]);
                transformChanged |= ImGui::DragFloat4("Column 3", &matrix[12]);

                if (transformChanged) {
                    // TODO.
                }
            },
        }, node.transform);

        /*ImGui::TextUnformatted("Global transform");
        const glm::mat4 &globalTransform = nodeGlobalTransforms[*selectedNodeIndex][0];
        INDEX_SEQ(Is, 4, {
            (ImGui::Text("Column %zu: (%.2f, %.2f, %.2f, %.2f)",
                Is, globalTransform[Is].x, globalTransform[Is].y, globalTransform[Is].z, globalTransform[Is].w
            ), ...);
        });*/

        ImGui::SeparatorText("Properties");

        if (ImGui::BeginTabBar("Node properties")) {
            if (node.meshIndex && ImGui::BeginTabItem("Mesh")) {
                const fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
                ImGui::Text("Mesh name: %s", mesh.name.c_str());

                for (const auto &[primitiveIndex, primitive]: mesh.primitives | ranges::views::enumerate) {
                    if (ImGui::CollapsingHeader(std::format("Primitive {}", primitiveIndex).c_str())) {
                        static int floatingPointPrecision = 2;

                        ImGui::SeparatorText("Attributes");
                        constexpr ImGuiTableFlags tableFlags
                            = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable
                            | ImGuiTableFlags_Reorderable | ImGuiTableFlags_SizingFixedFit;
                        if (ImGui::BeginTable("Attributes", 8, tableFlags)) {
                            // Headers.
                            ImGui::TableSetupColumn("Attribute");
                            ImGui::TableSetupColumn("Type");
                            ImGui::TableSetupColumn("ComponentType");
                            ImGui::TableSetupColumn("Count");
                            ImGui::TableSetupColumn("Bound");
                            ImGui::TableSetupColumn("Normalized");
                            ImGui::TableSetupColumn("Sparse");
                            ImGui::TableSetupColumn("BufferViewIndex");
                            ImGui::TableHeadersRow();

                            // Rows.
                            constexpr auto addRow = [](const char *attributeName, const fastgltf::Accessor &accessor) {
                                ImGui::TableNextRow();

                                ImGui::TableSetColumnIndex(0);
                                ImGui::TextUnformatted(attributeName);

                                ImGui::TableSetColumnIndex(1);
                                ImGui::TextUnformatted(to_string(accessor.type));

                                ImGui::TableSetColumnIndex(2);
                                ImGui::TextUnformatted(to_string(accessor.componentType));

                                ImGui::TableSetColumnIndex(3);
                                ImGui::Text("%zu", accessor.count);

                                ImGui::TableSetColumnIndex(4);
                                std::visit(fastgltf::visitor {
                                    [](const std::pmr::vector<int64_t> &min, const std::pmr::vector<int64_t> &max) {
                                        assert(min.size() == max.size() && "Different min/max dimension");
                                        if (min.size() == 1) ImGui::Text("[%lld, %lld]", min[0], max[0]);
#if __cpp_lib_format_ranges >= 202207L
                                        else ImGui::TextUnformatted(std::format("{}x{}", min, max).c_str());
#else
                                        else if (min.size() == 2) ImGui::Text("[%lld, %lld]x[%lld, %lld]", min[0], min[1], max[0], max[1]);
                                        else if (min.size() == 3) ImGui::Text("[%lld, %lld, %lld]x[%lld, %lld, %lld]", min[0], min[1], min[2], max[0], max[1], max[2]);
                                        else if (min.size() == 4) ImGui::Text("[%lld, %lld, %lld, %lld]x[%lld, %lld, %lld, %lld]", min[0], min[1], min[2], min[3], max[0], max[1], max[2], max[3]);
                                        else assert(false && "Unsupported min/max dimension");
#endif
                                    },
                                    [](const std::pmr::vector<double> &min, const std::pmr::vector<double> &max) {
                                        assert(min.size() == max.size() && "Different min/max dimension");
                                        if (min.size() == 1) ImGui::Text("[%.*lf, %.*lf]", floatingPointPrecision, min[0], floatingPointPrecision, max[0]);
#if __cpp_lib_format_ranges >= 202207L
                                        else ImGui::TextUnformatted(std::format("{::.{}f}x{::.{}f}", min, floatingPointPrecision, max, floatingPointPrecision).c_str());
#else
                                        else if (min.size() == 2) ImGui::Text("[%.*lf, %.*lf]x[%.*lf, %.*lf]", floatingPointPrecision, min[0], floatingPointPrecision, min[1], floatingPointPrecision, max[0], floatingPointPrecision, max[1]);
										else if (min.size() == 3) ImGui::Text("[%.*lf, %.*lf, %.*lf]x[%.*lf, %.*lf, %.*lf]", floatingPointPrecision, min[0], floatingPointPrecision, min[1], floatingPointPrecision, min[2], floatingPointPrecision, max[0], floatingPointPrecision, max[1], floatingPointPrecision, max[2]);
										else if (min.size() == 4) ImGui::Text("[%.*lf, %.*lf, %.*lf, %.*lf]x[%.*lf, %.*lf, %.*lf, %.*lf]", floatingPointPrecision, min[0], floatingPointPrecision, min[1], floatingPointPrecision, min[2], min[1], floatingPointPrecision, min[3], floatingPointPrecision, max[0], floatingPointPrecision, max[1], floatingPointPrecision, max[2], floatingPointPrecision, max[3]);
										else assert(false && "Unsupported min/max dimension");
#endif
                                    },
                                    [](const auto&...) {
                                        ImGui::TextUnformatted("-");
                                    }
                                }, accessor.min, accessor.max);

                                ImGui::TableSetColumnIndex(5);
                                ImGui::TextUnformatted(accessor.normalized ? "Yes" : "No");

                                ImGui::TableSetColumnIndex(6);
                                ImGui::TextUnformatted(accessor.sparse ? "Yes" : "No");

                                ImGui::TableSetColumnIndex(7);
                                if (accessor.bufferViewIndex) ImGui::Text("%zu", *accessor.bufferViewIndex);
                                else ImGui::TextUnformatted("-");
                            };
                            if (primitive.indicesAccessor) {
                                addRow("Index", asset.accessors[*primitive.indicesAccessor]);
                            }
                            for (const auto &[attributeName, accessorIndex] : primitive.attributes) {
                                addRow(attributeName.c_str(), asset.accessors[accessorIndex]);
                            }
                            ImGui::EndTable();
                        }

                        ImGui::SeparatorText("View option");
                        if (ImGui::InputInt("Bound fp precision", &floatingPointPrecision)) {
                            floatingPointPrecision = std::clamp(floatingPointPrecision, 0, 9);
                        }

                        ImGui::SeparatorText("Material");
                        if (primitive.materialIndex) ImGui::Text("Material index: %zu", *primitive.materialIndex);
                        else ImGui::TextUnformatted("Material index: -");
                    }
                }
                ImGui::EndTabItem();
            }
            if (node.cameraIndex && ImGui::BeginTabItem("Camera")) {
                const fastgltf::Camera &camera = asset.cameras[*node.cameraIndex];
                ImGui::Text("Camera name: %s", camera.name.c_str());
                ImGui::EndTabItem();
            }
            if (node.lightIndex && ImGui::BeginTabItem("Light")) {
                const fastgltf::Light &light = asset.lights[*node.lightIndex];
                ImGui::Text("Light name: %s", light.name.c_str());
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

auto vk_gltf_viewer::control::imgui::inputControlSetting(
    AppState &appState
) -> void {
	if (ImGui::Begin("Input control")){
	    ImGui::SeparatorText("Node selection");

	    bool showHoveringNodeOutline = appState.hoveringNodeOutline.has_value();
	    if (ImGui::Checkbox("Hovering node outline", &showHoveringNodeOutline)) {
            appState.hoveringNodeOutline.set_active(showHoveringNodeOutline);
	    }
	    if (showHoveringNodeOutline){
	        ImGui::DragFloat("Thickness##hoveringNodeOutline", &appState.hoveringNodeOutline->thickness, 1.f, 1.f, FLT_MAX);
            ImGui::ColorEdit4("Color##hoveringNodeOutline", glm::gtc::value_ptr(appState.hoveringNodeOutline->color));
	    }

	    bool showSelectedNodeOutline = appState.selectedNodeOutline.has_value();
	    if (ImGui::Checkbox("Selected node outline", &showSelectedNodeOutline)) {
            appState.selectedNodeOutline.set_active(showSelectedNodeOutline);
	    }
	    if (showSelectedNodeOutline){
	        ImGui::DragFloat("Thickness##selectedNodeOutline", &appState.selectedNodeOutline->thickness, 1.f, 1.f, FLT_MAX);
            ImGui::ColorEdit4("Color##selectedNodeOutline", glm::gtc::value_ptr(appState.selectedNodeOutline->color));
	    }
	}
	ImGui::End();
}

auto vk_gltf_viewer::control::imgui::viewManipulate(
    AppState &appState,
    const ImVec2 &passthruRectBR
) -> void {
	constexpr ImVec2 size { 64.f, 64.f };
	constexpr ImU32 background = 0x00000000; // Transparent.
	const glm::mat4 oldView = appState.camera.view;
	ImGuizmo::ViewManipulate(
		glm::gtc::value_ptr(appState.camera.view),
		distance(appState.camera.getEye(), glm::vec3 { 0.f, 0.35f, 0.f } /* TODO: match to appState.camera */),
		passthruRectBR - size, size,
		background);

	if (appState.camera.view != oldView) {
	    appState.isUsingImGuizmo = true;
	}
}
